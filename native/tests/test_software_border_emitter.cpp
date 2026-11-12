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
 assert(aether_doc_parse_html(doc,"<body><div id='box'></div><div id='box2'></div></body>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{40,40,1,16,0,0,0};
 assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
 int64_t sheet=0;int32_t rules=0;
 assert(aether_style_runtime_add_stylesheet(runtime,"body{display:block;margin:0}div{display:block}#box,#box2{position:absolute;left:0;top:0;width:12px;height:12px;margin:8px;border:2px dashed rgba(255,0,0,0.5);border-left:3px solid blue}",0,"r11",&sheet,&rules)==AETHER_OK);
 aether_style_update_t* updates=nullptr;int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);
 assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);
 assert(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);
 assert(aether_layout_layout(engine,doc,styles,40,40,nullptr,nullptr,&layout)==AETHER_OK);
 assert(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK);
 assert(aether_display_list_build_with_styles(builder,layout,styles,nullptr,&list)==AETHER_OK);
 p::AetherApi api{&aether_abi_version,&aether_abi_capabilities,&aether_paint_command_size,&aether_display_list_get_count,&aether_display_list_get_command,&aether_paint_command_free};
 assert(!list->border_groups.empty());
 auto snap=p::snapshot_native_commands(list);assert(std::holds_alternative<p::Snapshot>(snap));
 assert(std::holds_alternative<p::Error>(p::snapshot_commands(api,list)));
 auto count_groups=[](const p::Snapshot& s){int n=0;for(auto& c:s.commands)n+=std::holds_alternative<p::BorderGroup>(c);return n;};
 assert(count_groups(std::get<p::Snapshot>(snap))==2);
 aether_display_list_t node{};
 assert(aether_display_list_build_node_with_styles(builder,layout,styles,aether_doc_get_body(doc),nullptr,&node)==AETHER_OK);
 auto ns=p::snapshot_native_commands(node);assert(std::holds_alternative<p::Snapshot>(ns));assert(count_groups(std::get<p::Snapshot>(ns))==2);aether_display_list_destroy(node);
 auto valid=list->border_groups;
 list->border_groups[0].count=5;assert(std::holds_alternative<p::Error>(p::snapshot_native_commands(list)));list->border_groups=valid;
 list->border_groups.push_back(valid[0]);assert(std::holds_alternative<p::Error>(p::snapshot_native_commands(list)));list->border_groups=valid;
 list->border_groups[0].first=list->commands.size();assert(std::holds_alternative<p::Error>(p::snapshot_native_commands(list)));list->border_groups=valid;
 auto index=valid[0].first;auto saved=list->commands[index];list->commands[index].tag=AETHER_PAINT_PUSH_CLIP;assert(std::holds_alternative<p::Error>(p::snapshot_native_commands(list)));list->commands[index]=saved;
 assert(valid[0].count>=2);auto second=list->commands[index+1];
 list->commands[index+1].data.fill_border_side.side=saved.data.fill_border_side.side;assert(std::holds_alternative<p::Error>(p::snapshot_native_commands(list)));list->commands[index+1]=second;
 list->commands[index+1].data.fill_border_side.outer_rect.x+=1;assert(std::holds_alternative<p::Error>(p::snapshot_native_commands(list)));list->commands[index+1]=second;
 // Two equal emitter results stay two owned groups, never a heuristic fusion.
 auto n=list->commands.size();auto copy=list->commands;list->commands.insert(list->commands.end(),copy.begin(),copy.end());for(auto g:valid){g.first+=n;list->border_groups.push_back(g);}
 auto twice=p::snapshot_native_commands(list);assert(std::holds_alternative<p::Snapshot>(twice));assert(count_groups(std::get<p::Snapshot>(twice))==4);
 aether_display_list_destroy(list);aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
 auto f=p::prepare(std::get<p::Snapshot>(snap));assert(std::holds_alternative<p::PreparedFrame>(f));
 aether::rendering::FrameStore store;assert(store.resize({40,40,1})==aether::rendering::FrameStatus::ok);
 assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(f)).status==aether::rendering::software::Status::ok);
 if(argc==2){
  const auto begin=std::chrono::steady_clock::now();const auto cpu=std::clock();
  for(int n=0;n<12;++n)assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(f)).status==aether::rendering::software::Status::ok);
  const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
  std::optional<aether::rendering::Frame> image;assert(store.copy_confirmed(image)==aether::rendering::FrameStatus::ok&&image);
  auto bytes=smoke_png::png(smoke_png::rgba(*image),40,40);std::string path=argv[1];
  std::ofstream out(path+"/aether-r13.png",std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();assert(out);
  std::ofstream log(path+"/report.log");log<<"status=PASS\nscope=actual HTML side dashed border through native metadata capture; NOT full flexbox page\nviewport=40x40\nrender_calls=12\nrender_mean_ms="<<elapsed/12<<"\nprocess_cpu_ms="<<1000.0*(std::clock()-cpu)/CLOCKS_PER_SEC<<"\nframe_bytes="<<image->pixels.size()<<"\nchromium=NOT_RUN\ntext=UNSUPPORTED\npeak_RSS=NOT_MEASURED\n";
 }
 std::cout<<"PASS real HTML dashed/side border, both builders, metadata rejection, no fusion, owned lifetime\n";
}
