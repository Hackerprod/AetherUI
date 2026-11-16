#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/preparation/native_command_source.h"
#include "../src/rendering/display_list_types.h"
#include <cassert>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
using namespace aether::rendering;
using namespace aether::rendering::preparation;
static int checks=0;
#define CHECK(x) do{++checks;assert(x);}while(0)
static bool bad(const SnapshotResult& result,ErrorCode expected=ErrorCode::INVALID_PAYLOAD){auto* e=std::get_if<Error>(&result);return e&&e->code==expected;}
struct Cancel:Cancellation { bool cancelled()noexcept override{return true;} };
static void init(NativePaintList& n,ImageRef image){
 auto* l=new aether_display_list_s;NativeListAccess::list(n)=l;
 aether_paint_command_t cmd{};cmd.tag=AETHER_PAINT_DRAW_BACKGROUND_IMAGE;
 auto& p=cmd.data.draw_background_image;p.source=strdup("hero");p.source_rect={0,0,2,2};p.destination_rect={0,0,4,4};p.opacity=1;p.has_resolved_geometry=AETHER_TRUE;
 l->commands.push_back(cmd);n.images.push_back({0,p.source,"hero",image,p.source_rect,p.destination_rect,1});
}
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*) {return {0,1,.8f};}
static aether_bool_t fake_dimensions(const char*,aether_sizef* size,void* opaque){++*static_cast<int*>(opaque);*size={999,999};return AETHER_TRUE;}
struct Html {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t styles{};
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 Html(const char* declarations) {
  CHECK(aether_doc_create(&doc)==AETHER_OK);
  CHECK(aether_doc_parse_html(doc,"<body><div id='image'></div></body>")==AETHER_OK);
  aether_style_runtime_options_t options{};aether_style_environment_t env{8,8,1,16,0,0,0};
  CHECK(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
  std::string css="body{display:block;margin:0;}#image{display:block;width:4px;height:4px;background-image:url(hero);background-repeat:no-repeat;"+std::string(declarations)+"}";
  int64_t sheet{};int32_t rules{};CHECK(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"image.css",&sheet,&rules)==AETHER_OK);
  aether_style_update_t* updates{};auto n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);CHECK(n>0);aether_style_updates_free(updates,n);
  CHECK(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);
  CHECK(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);
  CHECK(aether_layout_layout(engine,doc,styles,8,8,nullptr,nullptr,&layout)==AETHER_OK);
  CHECK(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK);
 }
 ~Html(){aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
static void builder_tests(ImageRef image) {
 Html h("");int calls=0;
 aether_display_list_builder_set_image_dimensions_callback(h.builder,fake_dimensions,&calls);
 NativeImageCatalog catalog{{"hero",image}};std::unique_ptr<NativePaintList> out;
 CHECK(build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},catalog,out)==AETHER_OK);
 CHECK(calls==0);CHECK(out->images.size()==1);CHECK(out->images[0].crop.width==2);
 auto* original=out.get();
 const auto attempt=[&](const NativeImageCatalog& c){
  auto status=build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},c,out);
  CHECK(status!=AETHER_OK);CHECK(out.get()==original);
  CHECK(h.builder->native_image_catalog==nullptr&&h.builder->native_images==nullptr);
  CHECK(h.builder->image_dimensions==fake_dimensions&&h.builder->image_dimensions_data==&calls);
 };
 attempt({});attempt({{"hero",nullptr}});attempt({{"hero",image},{"hero",image}});
 attempt({{std::string(4097,'x'),image}});attempt({{std::string("hero\0bad",8),image}});
 NativeImageCatalog many(4097,{"hero",image});attempt(many);
 NativeImageCatalog long_keys;for(int i=0;i<257;++i)long_keys.push_back({std::to_string(i)+std::string(4090,'x'),image});attempt(long_keys);
 Html fractional("background-position:-0.5px 0;");std::unique_ptr<NativePaintList> fractional_out;
 CHECK(build_native_opacity(fractional.builder,fractional.layout,fractional.styles,nullptr,NativeScrollInput{},catalog,fractional_out)==AETHER_OK);
 CHECK(fractional_out->images[0].crop.x==0&&fractional_out->images[0].crop.width==2&&fractional_out->images[0].destination.x==-.5f);
 auto fraction_snapshot=snapshot_native_opacity(*fractional_out);CHECK(std::holds_alternative<Snapshot>(fraction_snapshot));
 bool full_resource=false;for(const auto& c:std::get<Snapshot>(fraction_snapshot).commands)if(const auto* im=std::get_if<Image>(&c))full_resource|=im->sampling_bounds==ImageSamplingBounds::FullResource;CHECK(full_resource);
 Html subtexel("background-position:-1.5px 0;");
 CHECK(build_native_opacity(subtexel.builder,subtexel.layout,subtexel.styles,nullptr,NativeScrollInput{},catalog,out)!=AETHER_OK);CHECK(out.get()==original);
 int failures=0;bool success=false;
 for(int i=0;i<2000;++i){
  fail_after=i;auto status=build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},catalog,out);fail_after=-1;
  CHECK(h.builder->native_image_catalog==nullptr&&h.builder->native_images==nullptr);
  CHECK(h.builder->image_dimensions==fake_dimensions&&h.builder->image_dimensions_data==&calls);
  if(status==AETHER_OK){success=true;break;}
  ++failures;CHECK(out.get()==original);
 }
 CHECK(success&&failures>0);CHECK(calls==0);
 std::cout<<"BUILDER_IMAGE_BRIDGE constructor_oom_sites="<<failures<<" callback_ignored=PASS atomic_output=PASS limits=PASS\n";
}
int main(){
 std::array<std::uint8_t,16> pixels{0,0,255,255,0,255,0,255,255,0,0,255,255,255,255,255};
 auto image=std::get<ImageRef>(ImageResource::copy(2,2,pixels));builder_tests(image);Snapshot retained;
 {NativePaintList n;init(n,image);auto* list=NativeListAccess::list(n);
  auto result=snapshot_native_opacity(n);CHECK(std::holds_alternative<Snapshot>(result));retained=std::get<Snapshot>(result);
  CHECK(std::get<Image>(retained.commands[0]).resource==image);
  CHECK(bad(snapshot_native_commands(list),ErrorCode::UNSUPPORTED_TAG));
  AetherApi api{aether_abi_version,aether_abi_capabilities,aether_paint_command_size,aether_display_list_get_count,aether_display_list_get_command,aether_paint_command_free};
  CHECK(bad(snapshot_commands(api,list),ErrorCode::UNSUPPORTED_TAG));
  auto saved=n.images; n.images.clear();CHECK(bad(snapshot_native_opacity(n),ErrorCode::UNSUPPORTED_TAG));n.images=saved;
  n.images.push_back(saved[0]);CHECK(bad(snapshot_native_opacity(n)));n.images=saved;
  n.images[0].command_index=1;CHECK(bad(snapshot_native_opacity(n)));n.images=saved;
  n.images[0].resource.reset();CHECK(bad(snapshot_native_opacity(n)));n.images=saved;
  n.images[0].source_identity="different";CHECK(bad(snapshot_native_opacity(n)));n.images=saved;
  n.images[0].source=std::string("hero\0extra",10);CHECK(bad(snapshot_native_opacity(n)));n.images=saved;
  n.images[0].source="evil";CHECK(bad(snapshot_native_opacity(n)));n.images=saved;
  n.images[0].source=std::string(100,'x');CHECK(bad(snapshot_native_opacity(n)));n.images=saved;
  n.images[0].source=std::string(4097,'x');CHECK(bad(snapshot_native_opacity(n)));n.images=saved;
  auto& raw=list->commands[0].data.draw_background_image;
  raw.has_resolved_geometry=AETHER_FALSE;CHECK(bad(snapshot_native_opacity(n)));raw.has_resolved_geometry=AETHER_TRUE;
  raw.destination_rect.width=5;CHECK(bad(snapshot_native_opacity(n)));raw.destination_rect.width=4;
  raw.opacity=.5f;CHECK(bad(snapshot_native_opacity(n)));raw.opacity=1;
  raw.source_rect.x=.5f;n.images[0].crop.x=.5f;CHECK(bad(snapshot_native_opacity(n)));raw.source_rect.x=0;n.images=saved;
  raw.source_rect.width=3;n.images[0].crop.width=3;CHECK(bad(snapshot_native_opacity(n)));raw.source_rect.width=2;n.images=saved;
  list->commands[0].tag=AETHER_PAINT_POP_CLIP;CHECK(bad(snapshot_native_opacity(n)));list->commands[0].tag=AETHER_PAINT_DRAW_BACKGROUND_IMAGE;
  Cancel cancel;CHECK(bad(snapshot_native_opacity(n,&cancel),ErrorCode::CANCELLED));
  int failures=0;bool succeeded=false;
  for(int i=0;i<100;++i){fail_after=i;auto r=snapshot_native_opacity(n);fail_after=-1;
   if(auto* e=std::get_if<Error>(&r)){CHECK(e->code==ErrorCode::OUT_OF_MEMORY);++failures;}else{succeeded=true;break;}
   CHECK(n.images[0].resource==image);CHECK(raw.source==saved[0].source_identity);
  }CHECK(failures>0&&succeeded);
 }
 image.reset();pixels.fill(0);CHECK(std::get<Image>(retained.commands[0]).resource->pixels()[2]==255);
 auto prepared=prepare(retained);CHECK(std::holds_alternative<PreparedFrame>(prepared));retained={};
 std::cout<<"NATIVE_IMAGE_BRIDGE PASS checks="<<checks<<" cancellation=PASS snapshot_oom=PASS legacy_rejection=PASS\n";
}
