#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <fstream>
namespace p=aether::rendering::preparation;namespace s=aether::rendering::software;using namespace aether::rendering;
static p::Radii radii(float r){return {r,r,r,r,r,r,r,r};}
int main(int argc,char**argv){
 std::ofstream binary;if(argc>1){binary.open(argv[1],std::ios::binary);assert(binary.good());}
 FrameStore store;assert(store.resize({16,16,1})==FrameStatus::ok);
 std::array<std::uint8_t,16> data{0,0,128,128,0,255,0,255,255,0,0,255,64,64,64,64};
 auto ref=std::get<p::ImageRef>(p::ImageResource::copy(2,2,data));
 std::uint64_t digest=1469598103934665603ULL;unsigned frames=0;
 for(int clip=0;clip<2;++clip)for(int kind=0;kind<4;++kind)for(int variant=0;variant<4;++variant){
  std::vector<p::Command> commands;
  if(clip)commands.push_back(p::PushClip{{1.25f,.75f,11.5f,12.5f},radii(3)});
  auto matrix=p::identity;matrix.dx=variant&1?.25f:0;matrix.dy=variant&1?.5f:0;
  commands.push_back(p::PushTransform{matrix});
  commands.push_back(p::PushScroll{1,variant&2?.5:0,variant&2?.25:0});
  commands.push_back(p::BeginOpacity{1,0,.75f});
  const p::Rect box{.5f,1.25f,12,10.5f};
  if(kind==0)commands.push_back(p::Fill{box,radii(2),{.25f,.5f,1,.75f}});
  if(kind==1)commands.push_back(p::Stroke{box,radii(2),1.5f,{1,.25f,.5f,.5f}});
  if(kind==2)commands.push_back(p::Image{ref,{0,0,2,2},box,.75f,p::ImageSamplingBounds::FullResource});
  if(kind==3){p::LinearGradientFill g;g.rect=box;g.gradient_rect=box;g.radii=radii(2);g.direction_x=1;g.direction_y=0;g.opacity=.75f;g.stops={{0,{1,0,0,.5f}},{.375f,{0,1,0,.75f}},{1,{0,0,1,1}}};commands.push_back(g);}
  commands.push_back(p::EndOpacity{1});commands.push_back(p::PopScroll{});commands.push_back(p::PopTransform{});if(clip)commands.push_back(p::PopClip{});
  commands.push_back(p::Fill{{14,14,2,2},{},{0,1,0,1}}); // scope restoration
  auto prepared=p::prepare(p::Snapshot{std::move(commands)});assert(std::holds_alternative<p::PreparedFrame>(prepared));assert(s::render(store,std::get<p::PreparedFrame>(prepared)).status==s::Status::ok);
  std::optional<Frame> frame;assert(store.copy_confirmed(frame)==FrameStatus::ok&&frame);
  if(binary.is_open()){binary.write(reinterpret_cast<const char*>(frame->pixels.data()),frame->pixels.size());assert(binary.good());}
  for(auto byte:frame->pixels){digest^=byte;digest*=1099511628211ULL;}++frames;
 }
 assert(digest==12347732073728140513ULL); // Frozen from c6a659a before N1.
 std::cout<<"clip_baseline frames="<<frames<<" bytes="<<frames*16*16*4<<" digest="<<digest<<"\n";
}
