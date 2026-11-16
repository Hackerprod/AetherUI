#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <cassert>
#include <array>
#include <iostream>
#include <limits>
#include <cmath>
#include <cstdlib>
#include <new>
static long fail=-1;
void* operator new(std::size_t n){if(fail==0)throw std::bad_alloc();if(fail>0)--fail;auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
namespace p=aether::rendering::preparation;namespace s=aether::rendering::software;using namespace aether::rendering;
p::PreparedFrame prepare(std::vector<p::Command> c){auto r=p::prepare(p::Snapshot{std::move(c)});assert(std::holds_alternative<p::PreparedFrame>(r));return std::move(std::get<p::PreparedFrame>(r));}
Frame frame(FrameStore& st){std::optional<Frame> f;assert(st.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
struct Cancel:p::Cancellation{int n;explicit Cancel(int v):n(v){}bool cancelled()noexcept override{return --n==0;}};
int main(){
 std::array<std::uint8_t,64> data{};for(int i=0;i<16;++i){data[i*4]=i*3;data[i*4+1]=i*5;data[i*4+2]=i*7;data[i*4+3]=128;}
 auto ref=std::get<p::ImageRef>(p::ImageResource::copy(4,4,data));
 p::Image full{ref,{0,0,4,4},{0,0,16,16},.75f,p::ImageSamplingBounds::FullResource};
 FrameStore st;assert(st.resize({24,24,1})==FrameStatus::ok);
 auto draw=[&](std::vector<p::Command> commands){auto f=prepare(std::move(commands));assert(s::render(st,f).status==s::Status::ok);return frame(st);};
 int equivalences=0;
 for(float edge:{0.f,.25f,.5f,.75f})for(int wrapper=0;wrapper<4;++wrapper){
  p::Rect clip{3+edge,5+edge,9.5f,7.5f};auto crop=full;crop.source={clip.x/4,clip.y/4,clip.width/4,clip.height/4};crop.destination=clip;
  std::vector<p::Command> prefix{p::BeginOpacity{1,0,.5f}},suffix{p::EndOpacity{1}};
  if(wrapper&1){auto m=p::identity;m.dx=1.25f;m.dy=.75f;prefix.push_back(p::PushTransform{m});suffix.insert(suffix.begin(),p::PopTransform{});}
  if(wrapper&2){prefix.push_back(p::PushScroll{2,.5,.25});suffix.insert(suffix.begin(),p::PopScroll{});}
  auto a=prefix;a.push_back(p::PushClip{clip,{}});a.push_back(full);a.push_back(p::PopClip{});a.insert(a.end(),suffix.begin(),suffix.end());
  auto b=prefix;b.push_back(crop);b.insert(b.end(),suffix.begin(),suffix.end());
  auto expected=draw(a),actual=draw(b);assert(expected.pixels==actual.pixels);++equivalences;
 }
 // Legacy aggregate construction remains StrictCrop; explicit mode is byte identical.
 p::Image legacy{ref,{1,1,2,2},{.25f,.75f,7,5},1};assert(legacy.sampling_bounds==p::ImageSamplingBounds::StrictCrop);
 auto expected=draw({legacy});auto explicitLegacy=legacy;explicitLegacy.sampling_bounds=p::ImageSamplingBounds::StrictCrop;assert(draw({explicitLegacy}).pixels==expected.pixels);
 // FullResource deliberately retains neighbors at a subimage boundary.
 explicitLegacy.sampling_bounds=p::ImageSamplingBounds::FullResource;assert(draw({explicitLegacy}).pixels!=expected.pixels);
 auto bad=full;bad.sampling_bounds=static_cast<p::ImageSamplingBounds>(999);auto rejected=p::prepare(p::Snapshot{{bad}});assert(std::get<p::Error>(rejected).code==p::ErrorCode::INVALID_PAYLOAD);
 // Mode and immutable resource survive source mutation/destruction.
 auto crop=full;crop.source={.25f,.5f,2,2};auto prepared=prepare({crop});crop.sampling_bounds=p::ImageSamplingBounds::StrictCrop;crop.resource.reset();
 assert(std::get<p::Image>(prepared.events()[0].command).sampling_bounds==p::ImageSamplingBounds::FullResource);
 assert(s::render(st,prepared).status==s::Status::ok);
 p::Snapshot snapshot{{std::get<p::Image>(prepared.events()[0].command)}};
 int prepare_oom=0;for(int n=0;n<30;++n){fail=n;auto r=p::prepare(snapshot);fail=-1;if(auto* e=std::get_if<p::Error>(&r)){assert(e->code==p::ErrorCode::OUT_OF_MEMORY);++prepare_oom;}else break;}assert(prepare_oom>=5);
 for(int n=1;n<=4;++n){Cancel c(n);assert(std::get<p::Error>(p::prepare(snapshot,&c)).code==p::ErrorCode::CANCELLED);}
 bad=full;bad.source={.25f,.5f,1,1};assert(s::render(st,prepare({bad})).status==s::Status::ok);
 auto saved=frame(st);bad=full;bad.source={1,1,std::nextafter(1.f,0.f),1};auto tiny=prepare({bad});assert(s::render(st,tiny).status==s::Status::unsupported);assert(frame(st).pixels==saved.pixels&&frame(st).sequence==saved.sequence);
 bad=full;bad.source={1,1,std::numeric_limits<float>::denorm_min(),1};auto denorm=prepare({bad});assert(s::render(st,denorm).status==s::Status::unsupported);
 for(float invalid:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),-1.f,4.f}){bad=full;bad.source.x=invalid;assert(std::holds_alternative<p::Error>(p::prepare(p::Snapshot{{bad}})));}
 int oom=0;for(int n=0;n<30;++n){fail=n;auto r=s::render(st,prepared);fail=-1;if(r.status==s::Status::ok)break;assert(r.status==s::Status::allocation_failed);assert(frame(st).pixels==saved.pixels&&frame(st).sequence==saved.sequence);++oom;}assert(oom>=2);
 saved=frame(st);for(int n=1;n<8;++n){Cancel c(n);assert(s::render(st,prepared,&c).status==s::Status::cancelled);assert(frame(st).pixels==saved.pixels&&frame(st).sequence==saved.sequence);}
 FrameStore large;assert(large.resize({2048,2048,1})==FrameStatus::ok);full.destination={0,0,2048,2048};auto expensive=prepare({full});assert(s::render(large,expensive).status==s::Status::limit_exceeded);
 std::cout<<"Fractional image PASS equivalences="<<equivalences<<" OOM="<<oom<<"\n";
}
