#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <aether/rendering/preparation/aether_command_source.hpp>
#include <cassert>
#include <iostream>
namespace p=aether::rendering::preparation;
// No text in this fixture. A call would mean its intended scope was violated.
aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){assert(false);return {};}
int main(){
 aether_doc_t doc{};aether_style_map_t styles{};aether_style_runtime_t runtime{};
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};aether_display_list_t list{};
 assert(aether_doc_create(&doc)==AETHER_OK);
 assert(aether_doc_parse_html(doc,"<body><div id='box'></div></body>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{40,40,1,16,0,0,0};
 assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
 int64_t sheet=0;int32_t rules=0;
 assert(aether_style_runtime_add_stylesheet(runtime,"body{display:block;margin:0}div{display:block}#box{width:12px;height:12px;margin:12px;box-shadow:0 2px 8px rgba(255,0,0,0.5),0 2px 7px rgba(0,0,255,0.5)}",0,"r11",&sheet,&rules)==AETHER_OK);
 aether_style_update_t* updates=nullptr;int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);
 assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);
 assert(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);
 assert(aether_layout_layout(engine,doc,styles,40,40,nullptr,nullptr,&layout)==AETHER_OK);
 assert(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK);
 assert(aether_display_list_build(builder,layout,nullptr,&list)==AETHER_OK);
 p::AetherApi api{&aether_abi_version,&aether_abi_capabilities,&aether_paint_command_size,&aether_display_list_get_count,&aether_display_list_get_command,&aether_paint_command_free};
 auto snap=p::snapshot_commands(api,list);assert(std::holds_alternative<p::Snapshot>(snap));
 int shadows=0;for(auto& cmd:std::get<p::Snapshot>(snap).commands)if(auto* c=std::get_if<p::BoxShadow>(&cmd)){assert(c->radii.top_left_x==0);assert(c->shadow.offset_y==2);assert(c->shadow.blur_radius==(shadows==0?7:8));++shadows;}assert(shadows==2);
 auto f=p::prepare(std::get<p::Snapshot>(snap));assert(std::holds_alternative<p::PreparedFrame>(f));
 aether::rendering::FrameStore store;assert(store.resize({40,40,1})==aether::rendering::FrameStatus::ok);
 auto result=aether::rendering::software::render(store,std::get<p::PreparedFrame>(f));assert(result.status==aether::rendering::software::Status::ok);
 std::optional<aether::rendering::Frame> out;assert(store.copy_confirmed(out)==aether::rendering::FrameStatus::ok&&out);
 const auto& shadow=std::get<p::Snapshot>(snap).commands;
 (void)shadow;bool visible=false;for(std::size_t i=3;i<out->pixels.size();i+=4)visible|=out->pixels[i]>0;assert(visible);
 // Reversing the two actual commands changes source-over order.
 auto reversed=std::get<p::Snapshot>(snap);std::vector<std::size_t> ids;for(std::size_t i=0;i<reversed.commands.size();++i)if(std::holds_alternative<p::BoxShadow>(reversed.commands[i]))ids.push_back(i);
 std::swap(reversed.commands[ids[0]],reversed.commands[ids[1]]);auto rf=p::prepare(reversed);assert(std::holds_alternative<p::PreparedFrame>(rf));assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(rf)).status==aether::rendering::software::Status::ok);std::optional<aether::rendering::Frame> other;assert(store.copy_confirmed(other)==aether::rendering::FrameStatus::ok);assert(other->pixels!=out->pixels);
 aether_display_list_destroy(list);aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
 std::cout<<"PASS HTML no-text document -> actual box-shadow order emitter -> raster\n";
}
