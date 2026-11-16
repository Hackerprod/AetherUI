#ifdef NDEBUG
#error assertions required
#endif
#include <aether/aether.h>
#include "../src/rendering/native_paint_list.h"
#include "../src/rendering/preparation/native_command_source.h"
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
static aether_text_metrics_t no_text(const char*,const aether_computed_style_t*,float,void*){return {};}
int main(){
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t styles{};
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 assert(aether_doc_create(&doc)==AETHER_OK);
 assert(aether_doc_parse_html(doc,"<body><div id='outer'><div id='inner'></div></div></body>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{16,16,1,16,0,0,0};
 assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
 int64_t sheet{};int32_t rules{};
 assert(aether_style_runtime_add_stylesheet(runtime,"body{display:block;margin:0}#outer{display:block;width:4px;height:4px;overflow:hidden}#inner{display:block;width:8px;height:8px;background-image:url(hero);background-repeat:repeat}",0,"scroll",&sheet,&rules)==AETHER_OK);
 aether_style_update_t* updates{};auto count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>0);aether_style_updates_free(updates,count);
 assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);
 assert(aether_layout_create(no_text,nullptr,&engine)==AETHER_OK);
 assert(aether_layout_layout(engine,doc,styles,16,16,nullptr,nullptr,&layout)==AETHER_OK);
 assert(aether_display_list_builder_create(no_text,nullptr,&builder)==AETHER_OK);
 const std::array<uint8_t,4> red{0,0,255,255};auto copied=p::ImageResource::copy(1,1,red);assert(std::holds_alternative<p::ImageRef>(copied));
 NativeImageCatalog catalog{{"hero",std::get<p::ImageRef>(copied)}};std::unique_ptr<NativePaintList> list;
 // Omitted scroll input intentionally supplies zero for every emitted ID.
 assert(build_native_opacity(builder,layout,styles,nullptr,catalog,list)==AETHER_OK);
 assert(!list->scroll.empty()&&!list->images.empty());NativeScrollInput valid;
 for(const auto& state:list->scroll){assert(state.x==0&&state.y==0);valid.push_back({state.id,0,0});}
 assert(std::holds_alternative<p::Snapshot>(p::snapshot_native_geometry(*list,p::ValidationDomain::ResolvedGeometryOnly)));
 auto* confirmed=list.get();
 // Explicit empty remains distinct from omitted, and rejection is transactional.
 assert(build_native_opacity(builder,layout,styles,nullptr,NativeScrollInput{},catalog,list)!=AETHER_OK);assert(list.get()==confirmed);
 assert(build_native_opacity(builder,layout,styles,nullptr,valid,catalog,list)==AETHER_OK);
 auto invalid=valid;invalid.front().id=0;confirmed=list.get();
 assert(build_native_opacity(builder,layout,styles,nullptr,invalid,catalog,list)!=AETHER_OK);assert(list.get()==confirmed);
 invalid=valid;invalid.front().x=std::numeric_limits<double>::quiet_NaN();
 assert(build_native_opacity(builder,layout,styles,nullptr,invalid,catalog,list)!=AETHER_OK);assert(list.get()==confirmed);
 invalid=valid;invalid.push_back(valid.front());
 assert(build_native_opacity(builder,layout,styles,nullptr,invalid,catalog,list)!=AETHER_OK);assert(list.get()==confirmed);
 invalid=valid;invalid.front().x=65537;
 assert(build_native_opacity(builder,layout,styles,nullptr,invalid,catalog,list)!=AETHER_OK);assert(list.get()==confirmed);
 aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
 std::cout<<"image_catalog_scroll_auto_zero=PASS\nexplicit_scroll_validation_unchanged=PASS\n";
}
