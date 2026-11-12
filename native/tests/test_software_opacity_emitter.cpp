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
int main(int argc,char** argv){
 aether_doc_t doc{};aether_style_map_t styles{};aether_style_runtime_t runtime{};
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};aether_display_list_t list{};
 assert(aether_doc_create(&doc)==AETHER_OK);
 assert(aether_doc_parse_html(doc,"<body><div id='group'><div id='box'></div><div id='box2'></div></div></body>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{40,40,1,16,0,0,0};
 assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
 int64_t sheet=0;int32_t rules=0;
 assert(aether_style_runtime_add_stylesheet(runtime,"body{display:block;margin:0}div{display:block}#group{position:relative;width:40px;height:40px;opacity:.5}#box,#box2{position:absolute;top:4px;width:24px;height:28px;background:red}#box{left:4px}#box2{left:12px;background:green}",0,"r11",&sheet,&rules)==AETHER_OK);
 aether_style_update_t* updates=nullptr;int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);
 assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);
 assert(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);
 assert(aether_layout_layout(engine,doc,styles,40,40,nullptr,nullptr,&layout)==AETHER_OK);
 assert(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK);
 std::unique_ptr<aether::rendering::NativePaintList> native;
 assert(aether::rendering::build_native_opacity(builder,layout,styles,nullptr,native)==AETHER_OK);
 auto snap=p::snapshot_native_opacity(*native);assert(std::holds_alternative<p::Snapshot>(snap));
 assert(!native->events.empty());
 auto saved=native->events;
 native->events[0].boundary=100000;assert(std::holds_alternative<p::Error>(p::snapshot_native_opacity(*native)));native->events=saved;
 std::unique_ptr<aether::rendering::NativePaintList> subtree;
 assert(aether::rendering::build_native_opacity(builder,layout,styles,aether_doc_get_body(doc),subtree)!=AETHER_OK && !subtree);
 assert(aether::rendering::build_native_opacity(builder,layout,styles,aether_doc_get_document_element(doc),subtree)!=AETHER_OK && !subtree);
 const auto group=aether_doc_get_element_by_id(doc,"group");assert(group);
 assert(aether::rendering::build_native_opacity(builder,layout,styles,group,subtree)==AETHER_OK);
 auto ns=p::snapshot_native_opacity(*subtree);assert(std::holds_alternative<p::Snapshot>(ns));
 auto f=p::prepare(std::get<p::Snapshot>(snap));assert(std::holds_alternative<p::PreparedFrame>(f));
 auto nf=p::prepare(std::get<p::Snapshot>(ns));assert(std::holds_alternative<p::PreparedFrame>(nf));
 aether::rendering::FrameStore store;assert(store.resize({40,40,1})==aether::rendering::FrameStatus::ok);
 assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(f)).status==aether::rendering::software::Status::ok);
 std::optional<aether::rendering::Frame> image;assert(store.copy_confirmed(image)==aether::rendering::FrameStatus::ok&&image);
 assert(image->pixels[(10*40+10)*4+3]==128);
 assert(image->pixels[(10*40+20)*4+3]==128); // overlap remains .5, not .75
 auto native_pixels=image->pixels;
 assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(nf)).status==aether::rendering::software::Status::ok);
 assert(store.copy_confirmed(image)==aether::rendering::FrameStatus::ok&&image);assert(image->pixels==native_pixels);
 // Legacy builder remains independently available after native mode resets.
 assert(aether_display_list_build_with_styles(builder,layout,styles,nullptr,&list)==AETHER_OK);
 auto legacy=p::snapshot_native_commands(list);assert(std::holds_alternative<p::Snapshot>(legacy));
 auto lf=p::prepare(std::get<p::Snapshot>(legacy));assert(std::holds_alternative<p::PreparedFrame>(lf));
 assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(lf)).status==aether::rendering::software::Status::ok);
 assert(store.copy_confirmed(image)==aether::rendering::FrameStatus::ok&&image);assert(image->pixels[(10*40+20)*4+3]==192);
 // Root opacity in current style overrides is rejected, without changing legacy mode.
 int64_t extra=0;int32_t extra_rules=0;
 assert(aether_style_runtime_add_stylesheet(runtime,"body{opacity:.5}",0,"root-opacity",&extra,&extra_rules)==AETHER_OK);
 updates=nullptr;count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);
 aether_style_map_t changed{};assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&changed)==AETHER_OK);
 std::unique_ptr<aether::rendering::NativePaintList> rejected;
 assert(aether::rendering::build_native_opacity(builder,layout,changed,nullptr,rejected)!=AETHER_OK && !rejected);
 assert(aether::rendering::build_native_opacity(builder,layout,changed,aether_doc_get_body(doc),rejected)!=AETHER_OK && !rejected);
 // Isolated descendants must ignore excluded ancestor opacity, including root overrides.
 assert(aether::rendering::build_native_opacity(builder,layout,changed,group,rejected)==AETHER_OK && rejected);
 auto isolated=p::snapshot_native_opacity(*rejected);assert(std::holds_alternative<p::Snapshot>(isolated));
 auto isolated_frame=p::prepare(std::get<p::Snapshot>(isolated));assert(std::holds_alternative<p::PreparedFrame>(isolated_frame));
 assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(isolated_frame)).status==aether::rendering::software::Status::ok);
 assert(store.copy_confirmed(image)==aether::rendering::FrameStatus::ok&&image);assert(image->pixels==native_pixels);
 rejected.reset();
 layout->root->visual_sample_valid=true;layout->root->visual_opacity=.25f;
 assert(aether::rendering::build_native_opacity(builder,layout,styles,nullptr,rejected)!=AETHER_OK && !rejected);
 assert(aether::rendering::build_native_opacity(builder,layout,styles,group,rejected)==AETHER_OK && rejected);
 auto visual_isolated=p::snapshot_native_opacity(*rejected);assert(std::holds_alternative<p::Snapshot>(visual_isolated));
 auto visual_frame=p::prepare(std::get<p::Snapshot>(visual_isolated));assert(std::holds_alternative<p::PreparedFrame>(visual_frame));
 assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(visual_frame)).status==aether::rendering::software::Status::ok);
 assert(store.copy_confirmed(image)==aether::rendering::FrameStatus::ok&&image);assert(image->pixels==native_pixels);
 rejected.reset();layout->root->visual_sample_valid=false;layout->root->visual_opacity=1;
 aether_style_map_destroy(changed);
 aether_display_list_destroy(list);native.reset();subtree.reset();
 // Owned snapshot survives source list and complete DOM/layout destruction.
 aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
 assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(f)).status==aether::rendering::software::Status::ok);
 if(argc==2){
  const auto start=std::chrono::steady_clock::now();
  for(int n=0;n<20;++n)assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(f)).status==aether::rendering::software::Status::ok);
  const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  assert(store.copy_confirmed(image)==aether::rendering::FrameStatus::ok&&image);
  const auto rgba=smoke_png::rgba(*image);const auto png=smoke_png::png(rgba,40,40);
  std::ofstream out(std::string(argv[1])+"/aether-r14.png",std::ios::binary);out.write(reinterpret_cast<const char*>(png.data()),png.size());out.close();assert(out);
  std::ofstream log(std::string(argv[1])+"/report.log");log<<"status=PASS\nscope=HTML minimum group opacity; not full flexbox\nviewport=40x40\nrender_mean_ms="<<elapsed/20<<"\nchromium=NOT_RUN\ntext=UNSUPPORTED\nwindows=NOT_RUN\n";
 }
 std::cout<<"PASS HTML grouped opacity, two builders, legacy baseline, owned lifetime\n";
}
