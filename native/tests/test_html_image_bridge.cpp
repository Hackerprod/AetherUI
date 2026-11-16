#ifdef NDEBUG
#error Assertions required
#endif
#include <aether/aether.h>
#include "../src/rendering/native_paint_list.h"
#include "../src/rendering/preparation/native_command_source.h"
#include "../src/rendering/software/software_renderer.h"
#include "../../tests/software_render_smoke/png_writer.h"
#include <cassert>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*) {return {0,1,.8f};}
struct Html {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t styles{};
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 Html(const std::string& declaration, const char* markup="<body><div id='image'></div></body>", const std::string& additional="") {
 assert(aether_doc_create(&doc)==AETHER_OK);
 assert(aether_doc_parse_html(doc,markup)==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{8,8,1,16,0,0,0};
 assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
 std::string css="body{display:block;margin:0;}#image{display:block;width:4px;height:4px;background-image:url(hero);"+declaration+"}"+additional;
 int64_t sheet{};int32_t rules{};assert(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"image.css",&sheet,&rules)==AETHER_OK);
 aether_style_update_t* updates{};auto n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(n>0);aether_style_updates_free(updates,n);
 assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);
 assert(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);
 assert(aether_layout_layout(engine,doc,styles,8,8,nullptr,nullptr,&layout)==AETHER_OK);
 assert(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK);
 }
 ~Html(){aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
static Frame frame(FrameStore& store){std::optional<Frame> f;assert(store.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
static void px(const Frame& f,int x,int y,std::array<int,4> expected){for(int c=0;c<4;++c)assert(f.pixels[y*f.stride+x*4+c]==expected[c]);}
int main(int argc,char**argv){
 const std::string output=argc>1?argv[1]:"/tmp";
 const std::array<std::uint8_t,16> pixels{0,0,255,255,0,255,0,255,255,0,0,255,255,255,255,255};
 auto encoded=smoke_png::png(smoke_png::Bytes{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255},2,2);
 auto decode_start=std::chrono::steady_clock::now();
 int32_t width=0,height=0;uint8_t* decoded=nullptr;
 assert(aether_image_decode(encoded.data(),encoded.size(),&width,&height,&decoded)==AETHER_OK&&width==2&&height==2);
 assert(std::equal(pixels.begin(),pixels.end(),decoded));
 auto image=std::get<p::ImageRef>(p::ImageResource::copy(width,height,{decoded,16}));
 aether_image_pixels_free(decoded);encoded.clear();
 std::cout<<"decode_and_copy_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-decode_start).count()<<" source_bytes=16\n";
 auto capture=[&](const std::string& css,const std::string& name){
  auto start=std::chrono::steady_clock::now();p::Snapshot snap;
  {Html h(css);NativeImageCatalog resources{{"hero",image}};std::unique_ptr<NativePaintList> list;
   assert(build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},resources,list)==AETHER_OK);
   auto r=p::snapshot_native_opacity(*list);assert(std::holds_alternative<p::Snapshot>(r));snap=std::move(std::get<p::Snapshot>(r));}
  image.reset(); // Only snapshot/prepared frame retain pixels after inputs die.
  auto prepared=p::prepare(snap);assert(std::holds_alternative<p::PreparedFrame>(prepared));snap={};
  FrameStore store;assert(store.resize({8,8,1})==FrameStatus::ok);
  auto result=s::render(store,std::get<p::PreparedFrame>(prepared));assert(result.status==s::Status::ok);auto f=frame(store);
  auto bytes=smoke_png::png(smoke_png::rgba(f),8,8);std::ofstream png(output+"/"+name+".png",std::ios::binary);png.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(png.good());
  auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
  std::cout<<name<<" pipeline_ns="<<elapsed<<" render_prepare_ns="<<result.preparation_ns<<" composition_ns="<<result.composition_ns<<" pixel_budget="<<result.pixel_budget<<" curved_samples="<<result.sampled_pixels<<" unique_source_bytes=16\n";
  auto before=f;assert(s::render(store,std::get<p::PreparedFrame>(prepared)).status==s::Status::ok);assert(frame(store).pixels==before.pixels);image=std::get<p::ImageRef>(p::ImageResource::copy(2,2,pixels));return f;
 };
 auto f=capture("background-repeat:no-repeat;","no_repeat");px(f,0,0,{0,0,255,255});px(f,1,0,{0,255,0,255});px(f,0,1,{255,0,0,255});px(f,2,2,{0,0,0,0});
 f=capture("background-repeat:repeat;","repeat");px(f,2,0,{0,0,255,255});px(f,3,3,{255,255,255,255});
 f=capture("background-repeat:no-repeat;background-size:4px 4px;","scaled");px(f,1,0,{0,64,191,255});
 f=capture("background-repeat:repeat;opacity:.5;","opacity");px(f,0,0,{0,0,128,128});
 f=capture("background-repeat:repeat;border-radius:2px;","rounded");assert(f.pixels[3]<255&&f.pixels[3]>0);
 f=capture("background-repeat:no-repeat;transform:translate(1px,1px);","translated");px(f,0,0,{0,0,0,0});px(f,1,1,{0,0,255,255});
 {Html h("background-repeat:no-repeat;");std::unique_ptr<NativePaintList> out;NativeImageCatalog empty;assert(build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},empty,out)!=AETHER_OK);assert(!out);}
 std::cout<<"HTML_BACKGROUND_IMAGE_E2E PASS cases=6 missing=reject Chromium=NOT_RUN img_element=NOT_SUPPORTED\n";
 return 0;
}
