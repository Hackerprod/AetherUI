#define main original_main
#include "test_html_image_bridge.cpp"
#include "../src/rendering/display_list_types.h"
#undef main
#include <limits>
#include <cmath>
int main(){std::array<std::uint8_t,64> pixels{};for(int i=0;i<16;++i)pixels[i*4+3]=255;auto im=std::get<p::ImageRef>(p::ImageResource::copy(4,4,pixels));Html h("background-repeat:no-repeat;");NativeImageCatalog cat{{"hero",im}};std::unique_ptr<NativePaintList> list;assert(build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},cat,list)==AETHER_OK);auto e=list->images[0];auto* raw=NativeListAccess::list(*list);auto &cmd=raw->commands[e.command_index].data.draw_background_image;int checks=0;auto test=[&](aether_rectf r,bool good){list->images[0].crop=r;cmd.source_rect=r;auto snap=p::snapshot_native_opacity(*list);assert(std::holds_alternative<p::Snapshot>(snap)==good);if(good){bool found=false;for(auto& c:std::get<p::Snapshot>(snap).commands)if(auto v=std::get_if<p::Image>(&c)){assert(v->sampling_bounds==p::ImageSamplingBounds::FullResource);found=true;}assert(found);}++checks;};
test({.25f,.75f,1,1},true);test({3,3,1,1},true);test({0,0,4,4},true);test({.25f,0,3.75f,4},true);test({.25f,0,4,4},false);test({0,0,std::nextafter(1.f,0.f),1},false);test({1,1,std::numeric_limits<float>::denorm_min(),1},false);test({-1,0,2,2},false);test({0,0,0,1},false);test({std::numeric_limits<float>::quiet_NaN(),0,1,1},false);test({0,0,std::numeric_limits<float>::infinity(),1},false);test({0,std::numeric_limits<float>::infinity(),1,1},false);std::cout<<"independent sidecar PASS checks="<<checks<<"\n";}
