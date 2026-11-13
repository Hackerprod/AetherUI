#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/rendering/software/software_renderer.h"
#include "../../native/src/rendering/preparation/native_command_source.h"
#include "../../native/src/rendering/display_list_types.h"
#include <cassert>
#include <iostream>
namespace p=aether::rendering::preparation;
using namespace aether::rendering;
aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){assert(false);return {};}
struct Scene{
 aether_doc_t doc{};aether_style_map_t styles{};aether_style_runtime_t runtime{};aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};std::unique_ptr<NativePaintList> native;
 explicit Scene(const char* css){assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,"<body><div id='parent'><div id='child'></div></div></body>")==AETHER_OK);aether_style_environment_t env{32,24,1,16,0,0,0};assert(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK);int64_t sheet{};int32_t rules{};assert(aether_style_runtime_add_stylesheet(runtime,css,0,"g1-native",&sheet,&rules)==AETHER_OK);aether_style_update_t* updates{};int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);assert(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);assert(aether_layout_layout(engine,doc,styles,32,24,nullptr,nullptr,&layout)==AETHER_OK);assert(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK);}
 auto build(){auto status=build_native_opacity(builder,layout,styles,nullptr,native);if(status!=AETHER_OK)return p::SnapshotResult{p::Error{p::ErrorCode::UNSUPPORTED_TAG}};return p::snapshot_native_opacity(*native);}
 ~Scene(){native.reset();aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
int main(){
 const std::string common="body{display:block;margin:0}div{display:block}#parent{width:16px;height:12px}#child{width:8px;height:6px}";
 p::Snapshot owned;
 {
  Scene scene((common+"#parent{background-image:linear-gradient(to right in srgb,red,blue)}#child{background-image:inherit}").c_str());
  auto result=scene.build();assert(std::holds_alternative<p::Snapshot>(result));owned=std::move(std::get<p::Snapshot>(result));
  size_t gradients=0;for(const auto& command:owned.commands)if(auto* g=std::get_if<p::LinearGradientFill>(&command)){++gradients;assert(g->stops.size()==2);}
  assert(gradients==2);
 }
 // The original native list, DOM, computed values and layout are all destroyed.
 auto prepared=p::prepare(owned);assert(std::holds_alternative<p::PreparedFrame>(prepared));owned.commands.clear();
 FrameStore store;assert(store.resize({32,24,1})==FrameStatus::ok);assert(software::render(store,std::get<p::PreparedFrame>(prepared)).status==software::Status::ok);
 std::optional<Frame> f;assert(store.copy_confirmed(f)==FrameStatus::ok&&f);
 // Child ramp is 8 wide, parent ramp16: ownership plus effective inheritance.
 auto red=[&](int x,int y){return f->pixels[(y*32+x)*4+2];};assert(red(3,2)>=142&&red(3,2)<=145);assert(red(3,8)>=198&&red(3,8)<=201);
 // Unsupported effective declaration cannot reuse an earlier valid certificate.
 {Scene scene((common+"#parent{background-image:linear-gradient(to right in srgb,red,blue);background-image:linear-gradient(to right,red,blue)}").c_str());auto result=scene.build();assert(std::holds_alternative<p::Error>(result));}
 // Later supported declaration replaces earlier unproven gradient at core level.
 {Scene scene((common+"#parent{background-image:linear-gradient(to right,red,blue);background-image:linear-gradient(to right in srgb,red,blue)}").c_str());auto result=scene.build();assert(std::holds_alternative<p::Snapshot>(result));}
 for(const auto* hidden:{"opacity:0", "visibility:hidden", "display:none"}){Scene scene((common+"#parent{"+hidden+";background-image:linear-gradient(to right in srgb,red -10%,blue)}").c_str());auto result=scene.build();assert(std::holds_alternative<p::Error>(result));}
 std::cout<<"G1 native owning lifetime, inherited evidence, effective overrides, hidden unsupported PASS\n";
}
