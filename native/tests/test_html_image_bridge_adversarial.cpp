// Independent adversarial review; reuse HTML setup, never synthesize layout output.
#define main positive_bridge_main
#include "test_html_image_bridge.cpp"
#undef main
#include "../src/rendering/display_list_types.h"
static int legacy_calls=0;
static aether_bool_t legacy_dimensions(const char*,aether_sizef* size,void*) {++legacy_calls;*size={9,9};return AETHER_TRUE;}
int main(){
 const std::array<std::uint8_t,16> pixels{0,0,255,255,0,255,0,255,255,0,0,255,255,255,255,255};
 auto image=std::get<p::ImageRef>(p::ImageResource::copy(2,2,pixels));
 Html h("background-repeat:no-repeat;");
 aether_display_list_builder_set_image_dimensions_callback(h.builder,legacy_dimensions,nullptr);
 NativeImageCatalog catalog{{"hero",image}};
 std::unique_ptr<NativePaintList> out;
 assert(build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},catalog,out)==AETHER_OK);
 assert(legacy_calls==0);
 auto* identity=out.get();
 for(auto bad: {NativeImageCatalog{},NativeImageCatalog{{"hero",image},{"hero",image}},NativeImageCatalog{{"hero",{}}}}){
  assert(build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},bad,out)!=AETHER_OK);
  assert(out.get()==identity);
 }
 for(const auto& key: {std::string(4097,'x'),std::string("he\0ro",5)}) {NativeImageCatalog invalid{{key,image}};assert(build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},invalid,out)!=AETHER_OK);assert(out.get()==identity);}
 NativeImageCatalog too_many(4097,NativeImageInput{"hero",image});assert(build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},too_many,out)!=AETHER_OK);assert(out.get()==identity);
 // Native entry must not permanently replace or consume the legacy callback.
 aether_display_list_t legacy{};
 assert(aether_display_list_build(h.builder,h.layout,nullptr,&legacy)==AETHER_OK);
 assert(legacy_calls>0);aether_display_list_destroy(legacy);
 assert(out->images.size()==1);
 auto entry=out->images[0];
 auto rejected=[&](){auto snap=p::snapshot_native_opacity(*out);assert(std::holds_alternative<p::Error>(snap));};
 out->images[0].command_index+=1;rejected();out->images[0]=entry;
 out->images[0].source="other";rejected();out->images[0]=entry;
 out->images[0].source=std::string(4096,'x');rejected();out->images[0]=entry;
 out->images[0].source_identity=nullptr;rejected();out->images[0]=entry;
 out->images[0].crop.width=1;rejected();out->images[0]=entry;
 out->images.push_back(entry);rejected();out->images.pop_back();
 out->images.clear();rejected();out->images.push_back(entry);
 // Original resource owner and catalog are destroyed before snapshot and render.
 std::weak_ptr<const p::ImageResource> weak=image;
 catalog.clear();too_many.clear();image.reset();entry.resource.reset();
 assert(!weak.expired());
 auto snap=p::snapshot_native_opacity(*out);assert(std::holds_alternative<p::Snapshot>(snap));out.reset();
 auto prepared=p::prepare(std::get<p::Snapshot>(snap));assert(std::holds_alternative<p::PreparedFrame>(prepared));snap=p::Snapshot{};
 FrameStore store;assert(store.resize({8,8,1})==FrameStatus::ok);assert(s::render(store,std::get<p::PreparedFrame>(prepared)).status==s::Status::ok);px(frame(store),0,0,{0,0,255,255});
 // background-size scales 2→3; clipping at x=0 yields a fractional source edge.
 Html fractional("background-repeat:no-repeat;background-size:3px 3px;background-position:-1px 0;");
 auto img=std::get<p::ImageRef>(p::ImageResource::copy(2,2,pixels));NativeImageCatalog source{{"hero",img}};
 std::unique_ptr<NativePaintList> frac;
 auto status=build_native_opacity(fractional.builder,fractional.layout,fractional.styles,nullptr,NativeScrollInput{},source,frac);
 assert(status==AETHER_OK);auto ss=p::snapshot_native_opacity(*frac);assert(std::holds_alternative<p::Snapshot>(ss));
 auto pp=p::prepare(std::get<p::Snapshot>(ss));assert(std::holds_alternative<p::PreparedFrame>(pp));assert(s::render(store,std::get<p::PreparedFrame>(pp)).status==s::Status::ok);
 px(frame(store),0,0,{0,128,128,255});px(frame(store),2,0,{0,0,0,0});
 std::cout<<"HTML_IMAGE_INDEPENDENT_ADVERSARIAL PASS ownership callback_restore catalog_atomicity sidecar fractional_crop\n";
}
