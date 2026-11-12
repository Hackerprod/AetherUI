#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/layout/layout_types.h"
#include "../../native/src/css/transforms.h"
#include "../../native/src/css/identity_translate_z.h"
#include "../../native/src/rendering/display_list_types.h"
#include "../../native/src/rendering/preparation/native_command_source.h"
#include "../../native/src/rendering/software/software_renderer.h"
#include <cassert>
#include <cmath>
#include <iostream>
static aether_text_metrics_t no_text(const char*,const aether_computed_style_t*,float,void*){return {};}
static bool near(float a,float b){return std::abs(a-b)<.01f;}
struct Scene {
 aether_doc_t doc{};aether_style_runtime_t rt{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t result{};
 Scene(const std::string& css,const char* html="<body><div id='outer'><div id='p'><div id='middle'><div id='a'></div></div></div></div></body>",float width=500,float height=400){
  assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,html)==AETHER_OK);
  aether_style_environment_t env{width,height,1,16,0,0,0};aether_style_runtime_options_t options{};
  assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&rt)==AETHER_OK);int64_t sh;int32_t nr;
  auto all=std::string("body{display:block;margin:0}div{display:block}#outer{width:350px;height:300px;padding:20px;border:3px solid red}#p{width:200px;height:100px;padding:10px;border:2px solid green}#a{position:fixed;left:10%;top:20%;width:20px;height:10px}")+css;
  assert(aether_style_runtime_add_stylesheet(rt,all.c_str(),0,"H10",&sh,&nr)==AETHER_OK);assert(aether_layout_create(no_text,nullptr,&engine)==AETHER_OK);resolve(width,height);
 }
 void resolve(float width=500,float height=400){aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(doc),&u);assert(n>=0);aether_style_updates_free(u,n);if(map)aether_style_map_destroy(map);map=nullptr;assert(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==AETHER_OK);if(result)aether_layout_result_destroy(result);result=nullptr;aether_bool_t changed{};assert(aether_layout_prepare_transition_context(engine,nullptr,map,nullptr,&changed)==AETHER_OK);assert(aether_layout_layout(engine,doc,map,width,height,nullptr,nullptr,&result)==AETHER_OK);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}
 aether_layout_box_s* box(const char* id){return result->find_primary_box(node(id));}
 ~Scene(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
};
int main(){size_t checks=0;
 auto check=[&](Scene& s,const char* owner,float fx=.1f,float fy=.2f,float mx=0,float my=0){auto a=s.box("a")->border_box;auto p=owner?s.box(owner)->padding_box:aether_rectf{0,0,s.result->viewport.width,s.result->viewport.height};if(!near(a.x,p.x+p.width*fx+mx)||!near(a.y,p.y+p.height*fy+my)){std::cerr<<"position "<<a.x<<","<<a.y<<" expected "<<p.x+p.width*fx+mx<<","<<p.y+p.height*fy+my<<" owner="<<(owner?owner:"viewport")<<"\n";std::abort();}++checks;};
 for(auto value:{"translateZ(0)","translateZ(0px)","translateZ(-0.0px)","translateZ(+0e2)","TRANSLATEZ(0)",R"(translat\65 Z(0p\78 ))","translateZ(/*x*/0/*y*/)"}){
  aether_transform_list list{};assert(aether_transform_list_try_parse(value,&list)&&list.count==1);aether_transform_list_free(&list);++checks;
  for(auto position:{"fixed","absolute"})for(auto display:{"block","flex"}){
   Scene s(std::string("#p{display:")+display+";transform:"+value+"}#a{position:"+position+"}");check(s,"p");assert(s.result->effective_fixed_container_nodes.count(s.node("p")));++checks;
  }
 }
 for(auto bad:{"translateZ(1px)","translateZ(0%)","translateZ(1e-999px)","translateZ(0em)","translateZ(calc(0px))","translateZ(0 0)","translateZ(0,0)","translateZ()","translateZ(0x0)","translateZ(1e999px)"}){aether_transform_list list{};assert(!aether_transform_list_try_parse(bad,&list)&&list.count==0);++checks;}
 {Scene s("");check(s,nullptr);}
 {Scene s("#outer{transform:translateZ(0)}");check(s,"outer");}
 {Scene s("#outer,#p{transform:translateZ(0)}");check(s,"p");}
 {Scene s("#outer{transform:translateZ(0)}#p{transform:inherit}");check(s,"p");}
 {Scene s("#p{transform:translateZ(0);height:auto}#middle{height:40px}");check(s,"p");}
 {Scene s("#p{transform:translateZ(0)}#a{margin:4px 5px}");check(s,"p",.1f,.2f,5,4);}
 {Scene s("#p{transform:translateZ(0);width:50%;height:50%}");check(s,"p");}
 {Scene s("#outer{transform:translateZ(0)}#p{position:relative}#a{position:absolute}");check(s,"p");}
 for(auto w:{300.f,800.f}){Scene s("#p{transform:translateZ(0)}","<body><div id='outer'><div id='p'><div id='a'></div></div></div></body>",w,250);check(s,"p");}
 for(auto css:{"#p{--t:translateZ(0);transform:var(--t)}","#p{transform:translateZ(0)!important;transform:none}","#p{transform:translateZ(0);transform:translateZ(1px)}"}){Scene s(css);check(s,"p");}
 for(auto kw:{"none","initial","unset","revert"}){Scene s(std::string("#p{transform:translateZ(0);transform:")+kw+"}");check(s,nullptr);}
 {Scene s("#p{transform:translateZ(0)}");check(s,"p");assert(aether_element_set_attribute(s.node("p"),"style","transform:none")==AETHER_OK);s.resolve();check(s,nullptr);assert(aether_element_set_attribute(s.node("p"),"style","transform:translateZ(0)")==AETHER_OK);s.resolve();check(s,"p");}
 // Static identity context is above ordinary in-flow sibling even with z-index:auto.
 {Scene s("#p{transform:translateZ(0)}","<body><div id='outer'><div id='p'></div><div id='a' style='position:static'></div></div></body>");
  StyleLookup lookup(s.result->assets->base,nullptr,{});
  auto items=layout_children_paint_order(s.box("outer"),lookup,BoxStylePolicy::effective_map);
  assert(items.size()==2&&items[0].dom_index==1&&items[1].dom_index==0);++checks;
 }
 // Exercise actual native snapshot/preparation/software raster with scroll input.
 {Scene s("#p{transform:translateZ(0);overflow:hidden;background-color:green}#middle{height:400px}#a{left:20px;top:50px;background-color:red}");
  aether_display_list_builder_t b{};assert(aether_display_list_builder_create(no_text,nullptr,&b)==AETHER_OK);
  std::unique_ptr<aether::rendering::NativePaintList> list;
  assert(aether::rendering::build_native_opacity(b,s.result,s.map,nullptr,list)==AETHER_OK);
  assert(!list->scroll.empty());aether::rendering::NativeScrollInput offsets;
  for(auto& item:list->scroll)offsets.push_back({item.id,0,item.id==s.node("p")->style_node_id?20.:0.});
  assert(aether::rendering::build_native_opacity(b,s.result,s.map,nullptr,offsets,list)==AETHER_OK);
  namespace p=aether::rendering::preparation;
  auto snapshot=p::snapshot_native_geometry(*list,p::ValidationDomain::ResolvedGeometryOnly);assert(std::holds_alternative<p::Snapshot>(snapshot));
  auto prepared=p::prepare(std::get<p::Snapshot>(snapshot));assert(std::holds_alternative<p::PreparedFrame>(prepared));
  aether::rendering::FrameStore store;assert(store.resize({500,400,1})==aether::rendering::FrameStatus::ok);
  assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(prepared)).status==aether::rendering::software::Status::ok);
  std::optional<aether::rendering::Frame> frame;assert(store.copy_confirmed(frame)==aether::rendering::FrameStatus::ok);
  auto red=[&](int x,int y){auto i=y*frame->stride+x*4;return frame->pixels[i]==0&&frame->pixels[i+1]==0&&frame->pixels[i+2]==255&&frame->pixels[i+3]==255;};
  assert(red(46,56)&&!red(46,76));++checks;aether_display_list_builder_destroy(b);
 }
 // Re-finalization must preserve block insertion anchors, not synthesize
 // an inline end from the preceding sibling (independent review regression).
 const char* anchor_html="<body><div id='outer'><div id='p'><div id='before'></div><div id='a'></div><div id='after'></div></div></div></body>";
 for(auto transform:{"none","translateZ(0)"})for(auto pos:{"absolute","fixed"})for(auto display:{"block","flex"}) {
  auto prefix=std::string("#outer{padding:0;border:0}#p{padding:3px;border:2px solid blue;transform:")+transform+"}#before{width:100px;height:40px}#a{position:"+pos+";display:"+display+";left:auto;right:auto;top:auto;bottom:auto}#after{height:20px}";
  Scene s(prefix,anchor_html);const auto p=s.box("p")->content_box,a=s.box("a")->border_box;
  assert(near(a.x,p.x)&&near(a.y,p.y+40));++checks;
  assert(near(s.box("after")->border_box.y,p.y+40));++checks;
 }
 for(auto pos:{"absolute","fixed"})for(auto special:{"", "#before{position:relative;left:13px;top:11px}", "#before{display:inline-block}"}) {
  auto prefix=std::string("#outer{padding:0;border:0;transform:translateZ(0)}#p{padding:0;border:0}#before{width:100px;height:40px}#a{position:")+pos+";left:auto;right:auto;top:auto;bottom:auto}"+special;
  Scene s(prefix,anchor_html);const auto p=s.box("p")->content_box,a=s.box("a")->border_box;
  assert(near(a.x,p.x)&&near(a.y,p.y+40));++checks;
 }
 for(auto pos:{"absolute","fixed"})for(bool explicit_x:{false,true}) {
  Scene s(std::string("#p{transform:translateZ(0)}#before{width:100px;height:40px}#a{position:")+pos+";left:auto;right:auto;top:auto;bottom:auto;"+(explicit_x?"left:8px":"top:8px")+"}",anchor_html);
  const auto a=s.box("a")->border_box,p=s.box("p")->content_box,pad=s.box("p")->padding_box;
  assert(near(a.x,explicit_x?pad.x+8:p.x)&&near(a.y,explicit_x?p.y+40:pad.y+8));++checks;
 }
 std::cout<<"H10 core checks="<<checks<<"\n";
}
