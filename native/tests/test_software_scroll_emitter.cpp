#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <aether/rendering/preparation/aether_command_source.hpp>
#include "../src/rendering/preparation/native_command_source.h"
#include "../src/rendering/display_list_types.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include "../../tests/software_render_smoke/png_writer.h"
namespace p=aether::rendering::preparation;
// No text in this fixture. A call would mean its intended scope was violated.
aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){assert(false);return {};}

void run(int argc,char** argv,bool nested,bool chrome_only){
 using namespace aether::rendering;
 const auto started=std::chrono::steady_clock::now();
 aether_doc_t doc{};aether_style_map_t styles{};aether_style_runtime_t runtime{};
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 assert(aether_doc_create(&doc)==AETHER_OK);
 assert(aether_doc_parse_html(doc,nested?"<body><div id='scroller'><div id='content'><div id='inner'><div id='deep'></div></div></div></div></body>":"<body><div id='scroller'><div id='content'></div></div></body>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{100,100,1,16,0,0,0};
 assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
 int64_t sheet=0;int32_t rules=0;
 const std::string css=std::string("body{display:block;margin:0}div{display:block}#scroller{width:40px;height:40px;overflow:auto;background:blue}#content{width:80px;height:80px;background:red}#inner{width:50px;height:50px;overflow:auto;opacity:.5}#deep{width:90px;height:90px;background:green}")+(chrome_only?"#scroller{overflow:scroll}#content{width:20px;height:20px}":"");
 assert(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"r15",&sheet,&rules)==AETHER_OK);
 aether_style_update_t* updates=nullptr;int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);
 assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);
 assert(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);
 assert(aether_layout_layout(engine,doc,styles,100,100,nullptr,nullptr,&layout)==AETHER_OK);
 assert(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK);
 std::unique_ptr<NativePaintList> zero,scrolled,subtree;
 assert(build_native_opacity(builder,layout,styles,nullptr,zero)==AETHER_OK);
 assert(zero->scroll.size()==(nested?2:1));
 const auto meta=zero->scroll[0];
 assert(chrome_only?(meta.range_x==0&&meta.range_y==0):(meta.range_x>0&&meta.range_y>0));
 NativeScrollInput input;for(const auto& m:zero->scroll)input.push_back({m.id,chrome_only?0:3.25,chrome_only?0:4.5});
 assert(build_native_opacity(builder,layout,styles,nullptr,input,scrolled)==AETHER_OK);
 assert(scrolled->scroll[0].x==input[0].x&&scrolled->scroll[0].y==input[0].y);
 if(!chrome_only)assert(scrolled->scroll[0].chrome.horizontal_thumb.x>meta.chrome.horizontal_thumb.x);
 if(!chrome_only)assert(scrolled->scroll[0].chrome.vertical_thumb.y>meta.chrome.vertical_thumb.y);
 const auto node=aether_doc_get_element_by_id(doc,"scroller");
 assert(build_native_opacity(builder,layout,styles,node,input,subtree)==AETHER_OK);
 auto snap=p::snapshot_native_geometry(*scrolled,p::ValidationDomain::ResolvedGeometryOnly);
 auto sub=p::snapshot_native_geometry(*subtree,p::ValidationDomain::ResolvedGeometryOnly);
 assert(std::holds_alternative<p::Snapshot>(snap)&&std::holds_alternative<p::Snapshot>(sub));
 auto unsupported=p::snapshot_native_geometry(*scrolled,p::ValidationDomain::AuthoredCss);
 assert(std::get<p::Error>(unsupported).code==p::ErrorCode::UnsupportedValidationDomain);
 static_assert(!p::GeometryValidationReport::flexbox_semantic_acceptance);
 for(auto bad:{NativeScrollInput{},NativeScrollInput{{meta.id,0,0},{meta.id,0,0}},NativeScrollInput{{meta.id+100,0,0}},NativeScrollInput{{meta.id,-1,0}},NativeScrollInput{{meta.id,meta.range_x+1,0}}}){
  auto before=scrolled.get();assert(build_native_opacity(builder,layout,styles,node,bad,scrolled)!=AETHER_OK&&scrolled.get()==before);
 }
 NativeScrollInput maximum;for(const auto& m:zero->scroll)maximum.push_back({m.id,m.range_x,m.range_y});
 assert(build_native_opacity(builder,layout,styles,node,maximum,subtree)==AETHER_OK);
 assert(std::holds_alternative<p::Error>(p::snapshot_native_commands(NativeListAccess::list(*scrolled))));
 auto saved=scrolled->scroll;
 scrolled->scroll.push_back({9999,0,0,0,0,{}});assert(std::holds_alternative<p::Error>(p::snapshot_native_opacity(*scrolled)));scrolled->scroll=saved;
 scrolled->scroll[0].x=-1;assert(std::holds_alternative<p::Error>(p::snapshot_native_opacity(*scrolled)));scrolled->scroll=saved;
 auto f=p::prepare(std::get<p::Snapshot>(snap)),sf=p::prepare(std::get<p::Snapshot>(sub));
 assert(std::holds_alternative<p::PreparedFrame>(f)&&std::holds_alternative<p::PreparedFrame>(sf));
 // Reject observable computed fixed under scroll. Author-only sticky/RTL are
 // deliberately NOT asserted: the core does not preserve those semantics.
 if(!nested&&!chrome_only){
  for(const char* css_case:{"#content{position:fixed;top:0;left:0}","#content{position:static}body{width:100px;height:100px;overflow:scroll}"}){
   int64_t added=0;int32_t nr=0;assert(aether_style_runtime_add_stylesheet(runtime,css_case,0,"gate",&added,&nr)==AETHER_OK);
   updates=nullptr;count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);
   aether_style_map_t changed{};aether_layout_result_t new_layout{};
   assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&changed)==AETHER_OK);
   assert(aether_layout_layout(engine,doc,changed,100,100,nullptr,nullptr,&new_layout)==AETHER_OK);
   std::unique_ptr<NativePaintList> rejected;
   assert(build_native_opacity(builder,new_layout,changed,nullptr,rejected)!=AETHER_OK&&!rejected);
   aether_layout_result_destroy(new_layout);aether_style_map_destroy(changed);
  }
 }
 // No builder/layout/style/DOM lifetime dependence after capture.
 scrolled.reset();subtree.reset();zero.reset();
 aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);
 aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
 const auto raster_started=std::chrono::steady_clock::now();
 FrameStore store;assert(store.resize({100,100,1})==FrameStatus::ok);
 assert(software::render(store,std::get<p::PreparedFrame>(f)).status==software::Status::ok);
 std::optional<Frame> image;assert(store.copy_confirmed(image)==FrameStatus::ok&&image);
 auto full=image->pixels;
 assert(software::render(store,std::get<p::PreparedFrame>(sf)).status==software::Status::ok);
 assert(store.copy_confirmed(image)==FrameStatus::ok&&image);assert(full==image->pixels);
 if(argc>1){const auto png=smoke_png::png(smoke_png::rgba(*image),100,100); std::ofstream output(std::string(argv[1])+"/scroll.png",std::ios::binary);output.write(reinterpret_cast<const char*>(png.data()),png.size());output.close();assert(output);
 const auto ended=std::chrono::steady_clock::now();
 std::ofstream report(std::string(argv[1])+"/report.log");
 report<<"fixture=resolved physical scroll geometry, no text\nviewport=100x100\nrendered_frames=2\npixel_bytes="<<image->pixels.size()<<"\n";
 report<<"timing_scope=FrameStore setup, two renders, two frame copies, pixel comparison and PNG encoding/write; not raster-only\n";
 report<<"setup_and_tests_ms="<<std::chrono::duration<double,std::milli>(raster_started-started).count()<<"\nrender_capture_compare_png_ms="<<std::chrono::duration<double,std::milli>(ended-raster_started).count()<<"\n";
 report<<"raster_geometry=PASS\nauthored_css_validation="<<p::GeometryValidationReport::authored_css_validation<<"\nflexbox_semantic_acceptance=false\n";}
 std::cout<<"PASS actual HTML/full/node/frozen offsets/chrome/exact table/lifetime/domain gate\n";
}

int main(int argc,char** argv){run(argc,argv,false,false);run(0,nullptr,true,false);run(0,nullptr,false,true);}
