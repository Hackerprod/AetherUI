#ifdef NDEBUG
#undef NDEBUG
#endif
#ifndef AETHER_HTML_LTR_TESTING
#define AETHER_HTML_LTR_TESTING
#endif
#include <aether/aether_b0_source.h>
#include "rendering/html_ltr/b2_session_provenance.h"
#include "rendering/html_ltr/session.h"
#include "rendering/html_ltr/admission.h"
#include "layout/layout_internal.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <iterator>
#include <map>
#include <string>
#include <vector>
namespace b2=aether::layout::b2;
struct Geometry { aether_rectf border{},content{}; uint64_t box=0,generation=0;};
static std::map<std::string,Geometry> captured;
static std::map<std::string,std::vector<aether_rectf>> text_boxes;
static size_t inspect_count=0,ledger_count=0,checks=0;
static std::string scenario;
namespace aether::rendering::html_ltr::testing {
void stage(Stage){}
void b2_result_inspect(B2InlineResult&){}
void b2_inspect(b2::Ledger&){++ledger_count;}
void inspect(aether_layout_result_s* layout,aether::rendering::NativePaintList* paint){
 if(paint)return;
 ++inspect_count;assert(layout&&layout->root&&layout->b3_result_id);
 std::vector<aether_layout_box_s*> todo{layout->root};
 while(!todo.empty()) {auto* box=todo.back();todo.pop_back();
  if(box->element){std::string key;auto id=box->element->attributes.find("id");if(id!=box->element->attributes.end())key=id->second;
   else if(auto cls=box->element->attributes.find("class");cls!=box->element->attributes.end())for(const char* target:{"page","shell","story","panel"})if(cls->second==target)key="class:"+cls->second;
   if(!key.empty()){
    if(box->kind==LAYOUT_BOX_TEXT)text_boxes[key].push_back(box->content_box);
    else {assert(box->b3_box_id&&box->b3_generation_id);assert(captured.emplace(key,Geometry{box->border_box,box->content_box,box->b3_box_id,box->b3_generation_id}).second);}
   }
  }
  for(auto it=box->children.rbegin();it!=box->children.rend();++it)todo.push_back(*it);
 }
}
}
static auto read(const char* path){std::ifstream f(path,std::ios::binary);assert(f);return std::vector<uint8_t>{std::istreambuf_iterator<char>(f),{}};}
static std::vector<uint8_t> regular,bold;
static void near(float actual,float expected,const char* what){++checks;if(!std::isfinite(actual)||std::abs(actual-expected)>.001f){std::fprintf(stderr,"FAIL %s %s actual=%.9g expected=%.9g\n",scenario.c_str(),what,actual,expected);std::abort();}}
static Geometry box(const char* id){assert(captured.contains(id));return captured.at(id);}
static std::string doc(const std::string& body,const std::string& css){return "<html><head><style>body{margin:0px}"+css+"#activation{display:flex;width:0px;height:0px;min-width:0px}</style></head><body>"+body+"<div id='activation'></div></body></html>";}
static void run(const char* name,std::string html,int w=640,int h=480,aether_status_t expected=AETHER_OK,uint32_t expected_stage=0){
 if(const char* out=std::getenv("B3_WIDTH_FIXTURE_DIR")){
  std::filesystem::create_directories(out);std::ofstream frozen(std::filesystem::path(out)/(std::string(name)+".html"),std::ios::binary);assert(frozen);frozen.write(html.data(),html.size());frozen.close();assert(frozen);
 }
 scenario=name;captured.clear();text_boxes.clear();inspect_count=ledger_count=0;
 aether_b0_source_t session=nullptr;aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;
 aether_status_t status;
 {auto r=regular,b=bold;aether_b0_source_options_v1 o{};o.struct_size=sizeof o;o.version=1;o.regular_font=r.data();o.regular_font_size=r.size();o.bold_font=b.data();o.bold_font_size=b.size();status=aether_b0_source_create(w,h,html.data(),html.size(),&o,&session,&d);}
 std::printf("CASE %s status=%d stage=%u reason=%u node=%llu session=%d inspect=%zu ledger=%zu\n",name,status,d.stage,d.reason,(unsigned long long)d.node_ordinal,session!=nullptr,inspect_count,ledger_count);std::fflush(stdout);
 assert(status==expected);
 if(expected!=AETHER_OK){assert(!session&&d.stage==expected_stage);return;}
 assert(session&&inspect_count==1&&ledger_count==1);
 html.clear();html.shrink_to_fit();aether_b0_source_frame_info_v1 info{};info.struct_size=sizeof info;info.version=1;
 assert(aether_b0_source_copy_frame(session,&info,nullptr,0)==AETHER_ERROR_NO_FRAME);
 for(const auto& [id,g]:captured)std::printf("GEOMETRY %s %s border %.9g %.9g %.9g %.9g content %.9g %.9g %.9g %.9g\n",name,id.c_str(),g.border.x,g.border.y,g.border.width,g.border.height,g.content.x,g.content.y,g.content.width,g.content.height);
 assert(aether_b0_source_destroy(session)==AETHER_OK);
}

// Internal evidence probe, not a claim that these construction inputs pass
// the public B3 source subset. Numeric geometry is deliberately held fixed.
static void internal_width_probe(const char* mode,aether_status_t wanted) {
 using namespace aether::rendering::html_ltr;
 aether_doc_t dom{};aether_style_runtime_t runtime{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t layout{};
 assert(aether_doc_create(&dom)==AETHER_OK);assert(aether_doc_parse_html(dom,"<html><body><div id='p'><div id='c'></div></div></body></html>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{640,480,1,16,0,0,0};assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
 int64_t sheet{};assert(aether_style_runtime_add_stylesheet(runtime,"html,body,div{display:block;margin:0;padding:0}#p{width:400px}#c{width:50%;height:20px}",0,"internal-width-evidence",&sheet,nullptr)==AETHER_OK);
 aether_style_update_t* updates{};int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(dom),&updates);assert(count>0);aether_style_updates_free(updates,count);assert(aether_style_runtime_create_resolved_style_map(runtime,dom,&map)==AETHER_OK);
 auto measure=+[](const char*,const aether_computed_style_t*,float,void*)->aether_text_metrics_t{return {0,0,0};};assert(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);assert(aether_layout_layout(engine,dom,map,640,480,nullptr,nullptr,&layout)==AETHER_OK);
 auto* root=layout->root;auto* parent=aether_layout_result_find_primary_box(layout,aether_doc_get_element_by_id(dom,"p"));auto* child=aether_layout_result_find_primary_box(layout,aether_doc_get_element_by_id(dom,"c"));assert(parent&&child&&parent->content_box.width==400&&child->content_box.width==200);
 aether_status_t status=AETHER_OK;
 {
  auto r=std::make_shared<const aether::rendering::text::FontSource>(regular),b=std::make_shared<const aether::rendering::text::FontSource>(bold);
  B2Provenance provenance(dom,map,r,b,true);layout->b3_result_id=123;provenance.b3_begin_result(layout,123);
  uint64_t id=0;for(auto* box:{root,parent,child}){box->b3_box_id=++id;box->b3_generation_id=id;box->b3_parent_generation_id=id-1;provenance.b3_box(layout,box,id,id,id-1);provenance.b3_complete(box);}
  provenance.b3_width(layout,root,nullptr,640,640,B3WidthReason::block_fill,false,true);
  const bool cycle=std::string(mode)!="numeric-indefinite";
  const bool forged=std::string(mode)=="forged-circular-certificate";
  provenance.b3_width(layout,parent,cycle?child:root,cycle?200:640,400,forged?B3WidthReason::block_fill:B3WidthReason::indefinite,false,forged);
  provenance.b3_width(layout,child,parent,400,200,forged?B3WidthReason::block_fill:B3WidthReason::indefinite,true,forged);
  try{provenance.validate_final(layout);}catch(const Error& e){status=e.status;}
 }
 std::printf("INTERNAL_WIDTH %s numeric_parent=400 numeric_child=200 status=%d expected=%d\n",mode,status,wanted);assert(status==wanted);
 aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(runtime);aether_doc_destroy(dom);
}
int main(int argc,char**argv){
 assert(argc==4);regular=read(argv[1]);bold=read(argv[2]);
 const auto frozen_bytes=read(argv[3]);run("frozen-two-panel",std::string(frozen_bytes.begin(),frozen_bytes.end()),1280,800);
 near(box("class:page").border.width,1280,"page width");near(box("class:page").border.height,800,"page height");
 const auto shell=box("class:shell");near(shell.border.x,80,"shell x");near(shell.border.y,60,"shell y");near(shell.border.width,1120,"shell width");near(shell.border.height,680,"shell height");near(shell.content.width,1118,"shell content width");near(shell.content.height,678,"shell content height");
 const auto story=box("class:story"),panel=box("class:panel");near(story.border.x,81,"story x");near(story.border.y,61,"story y");near(story.border.width,581.36f,"story width");near(story.border.height,678,"story height");near(story.content.width,485.36f,"story content width");near(story.content.height,582,"story content height");near(story.content.x,129,"story content x");near(story.content.y,109,"story content y");
 near(panel.border.x,662.36f,"panel x");near(panel.border.y,61,"panel y");near(panel.border.width,536.64f,"panel width");near(panel.border.height,678,"panel height");near(panel.content.width,432.64f,"panel content width");near(panel.content.height,570,"panel content height");near(panel.content.x,714.36f,"panel content x");near(panel.content.y,115,"panel content y");
 const std::string tree="<div id='p'><div id='c'></div></div>";
 run("px-content",doc(tree,"#p{width:400px}#c{width:50%;height:20px}"));near(box("c").content.width,200,"child");
 run("border-content",doc(tree,"#p{width:400px;box-sizing:border-box;padding:20px;border:10px solid #000000}#c{width:50%;box-sizing:border-box;padding:10px;border:2px solid #000000;height:20px}"));near(box("p").content.width,340,"parent");near(box("c").border.width,170,"child border");near(box("c").content.width,146,"child content");
 run("auto-fill",doc(tree,"#p{width:auto}#c{width:50%;height:20px}"));near(box("c").content.width,320,"child");
 const std::string row="<div id='offset'><div id='row'> \t\n<!--gap--><div id='a'><div id='aa'></div></div> \n<div id='b'><div id='bb'></div></div></div></div>";
 run("shrink-final-percent",doc(row,"#offset{margin:17px;padding:11px}#row{display:flex;width:600px;align-items:flex-start}#a,#b{width:400px;min-width:0px;box-sizing:border-box}#aa,#bb{width:50%;height:20px}"),800,600);for(auto id:{"a","b"})near(box(id).border.width,300,"item");for(auto id:{"aa","bb"})near(box(id).content.width,150,"percentage descendant");near(box("a").border.x,28,"offset a");near(box("b").border.x,328,"offset b");
 run("row-stretch-control",doc("<div id='row'><div id='a'><p id='text'>Native text.</p></div><input id='b' type='text' value='X'></div>","#row{display:flex;width:400px;height:100px;align-items:stretch}#a,#b{width:200px;min-width:0px;box-sizing:border-box}"));near(box("a").border.height,100,"text item stretched");near(box("b").border.height,100,"control item stretched");
 // Independent hmtx oracle frozen in font-oracle.json: 40*2025*16/2048=632.8125>400.
 const std::string minword="<div id='row'><div id='a'>"+std::string(40,'W')+"</div><div id='b'></div></div>";
 const std::string mincss="#row{display:flex;width:600px;align-items:start}#a,#b{width:400px;box-sizing:border-box}#b{min-width:0px}";
 run("min-auto-cap",doc(minword,mincss+"#a{min-width:auto}"));near(box("a").border.width,400,"auto cap first");near(box("b").border.width,200,"auto cap second");
 run("min-zero-shrink",doc(minword,mincss+"#a{min-width:0px}"));near(box("a").border.width,300,"zero first");near(box("b").border.width,300,"zero second");
 const std::string col="<div id='col'><div id='a'></div><div id='b'></div><div id='c'></div></div>";
 for(const char* justify:{"space-between","center"}){run(justify,doc(col,"#col{display:flex;flex-direction:column;height:300px;width:100px;justify-content:"+std::string(justify)+"}#a,#b,#c{height:40px}"));int n=0;for(auto id:{"a","b","c"}){near(box(id).border.y,std::string(justify)=="center"?90.f+n*40.f:n*130.f,id);++n;}}
 const std::string stretch="<div id='col'><div id='item'><p id='text'>Wide words create several wrapped lines of native text for the stretch comparison.</p></div></div>";
 const std::string stretch_css="#col{display:flex;flex-direction:column;width:200px;align-items:stretch}p{white-space:normal}";
 run("stretch-auto",doc(stretch,stretch_css));const auto auto_item=box("item");const auto auto_text=box("text");const auto auto_fragments=text_boxes.at("text");
 run("stretch-explicit",doc(stretch,stretch_css+"#item{width:200px}"));near(box("item").content.width,auto_item.content.width,"stretch width equivalent");near(box("text").content.height,auto_text.content.height,"wrap height equivalent");assert(auto_fragments.size()==text_boxes.at("text").size());for(size_t i=0;i<auto_fragments.size();++i){near(text_boxes.at("text")[i].width,auto_fragments[i].width,"fragment width");near(text_boxes.at("text")[i].y-box("text").content.y,auto_fragments[i].y-auto_text.content.y,"fragment line");}
 for(const char* css:{"width:101%","height:50%","min-width:400px;max-width:300px","max-width:auto","min-width:none"})run(css,doc(tree,"#p{"+std::string(css)+"}"),640,480,AETHER_ERROR_UNSUPPORTED,AETHER_B0_STAGE_STYLE);
 internal_width_probe("numeric-indefinite",AETHER_ERROR_UNSUPPORTED);
 internal_width_probe("semantic-circular-indefinite",AETHER_ERROR_UNSUPPORTED);
 internal_width_probe("forged-circular-certificate",AETHER_ERROR_INTERNAL);
 std::printf("PASS B3 source-session width geometry and admission negatives checks=%zu (no raster oracle)\n",checks);
}
