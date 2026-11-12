#ifdef NDEBUG
#error assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <cassert>
#include <iostream>
namespace p=aether::rendering::preparation;
namespace r=aether::rendering;
p::Fill fill(float x,float y,float w,float h,p::Color c){return {{x,y,w,h},{},c};}
std::vector<unsigned char> pixels(r::FrameStore& store){std::optional<r::Frame> f;assert(store.copy_confirmed(f)==r::FrameStatus::ok&&f);return f->pixels;}
int main(){
 r::FrameStore store;assert(store.resize({8,8,1})==r::FrameStatus::ok);
 const auto render=[&](p::Snapshot s){auto f=p::prepare(s);assert(std::holds_alternative<p::PreparedFrame>(f));return r::software::render(store,std::get<p::PreparedFrame>(f));};
 p::Snapshot s{{p::BeginOpacity{1,0,.5f},fill(0,0,6,6,{1,0,0,1}),fill(2,2,6,6,{0,1,0,1}),p::EndOpacity{1}}};
 assert(render(s).status==r::software::Status::ok);auto a=pixels(store);assert(a[3]==128 && a[(3*8+3)*4+3]==128 && a[(3*8+3)*4+1]==128);
 p::Snapshot nested{{p::BeginOpacity{1,0,.5f},p::BeginOpacity{2,1,.5f},fill(0,0,8,8,{1,0,0,1}),p::EndOpacity{2},p::EndOpacity{1}}};
 assert(render(nested).status==r::software::Status::ok);assert(pixels(store)[3]==64);
 // Clips applied once to children, never again while compositing layer.
 p::Snapshot clip{{p::PushClip{{.5f,0,4,4},{}},p::BeginOpacity{1,0,.5f},fill(0,0,8,8,{1,0,0,1}),p::EndOpacity{1},p::PopClip{}}};
 assert(render(clip).status==r::software::Status::ok);assert(pixels(store)[3]==64);
 auto baseline=pixels(store);
 p::Snapshot bad{{p::BeginOpacity{1,0,0},p::Text{"x",{0,0,2,2},"test",12,400,AETHER_TEXT_ALIGN_LEFT,{1,1,1,1},0},p::EndOpacity{1}}};
 assert(render(bad).status==r::software::Status::unsupported);assert(pixels(store)==baseline);
 for(auto malformed: {p::Snapshot{{p::BeginOpacity{1,0,.5f},p::EndOpacity{2}}},p::Snapshot{{p::BeginOpacity{1,7,.5f}}},p::Snapshot{{p::PushClip{{0,0,4,4},{}},p::BeginOpacity{1,0,.5f},p::PopClip{},p::PushClip{{0,0,4,4},{}},p::EndOpacity{1},p::PopClip{}}}})assert(std::holds_alternative<p::Error>(p::prepare(malformed)));
 for(float alpha:{0.f,1.f}){s.commands[0]=p::BeginOpacity{1,0,alpha};assert(render(s).status==r::software::Status::ok);assert(pixels(store)[3]==unsigned(alpha*255));}
 struct Cancel:p::Cancellation{int remaining;explicit Cancel(int n):remaining(n){}bool cancelled()noexcept override{return remaining--==0;}};
 auto f=p::prepare(nested);for(int n=0;n<50;++n){auto before=pixels(store);Cancel cancel(n);auto result=r::software::render(store,std::get<p::PreparedFrame>(f),&cancel);if(result.status==r::software::Status::cancelled)assert(pixels(store)==before);}
 r::FrameStore big;assert(big.resize({4096,4096,1})==r::FrameStatus::ok);assert(r::software::render(big,std::get<p::PreparedFrame>(f)).status==r::software::Status::limit_exceeded);
 // Maximum depth is structural even for invisible groups.
 p::Snapshot deep;for(unsigned n=1;n<=9;++n)deep.commands.push_back(p::BeginOpacity{n,n-1,0});
 for(unsigned n=9;n>0;--n)deep.commands.push_back(p::EndOpacity{n});
 assert(std::holds_alternative<p::Error>(p::prepare(deep)));
 bool fail=false;r::FrameStore allocation([&]{if(fail)throw std::bad_alloc();});
 assert(allocation.resize({8,8,1})==r::FrameStatus::ok);
 assert(r::software::render(allocation,std::get<p::PreparedFrame>(f)).status==r::software::Status::ok);
 auto confirmed=pixels(allocation);fail=true;
 assert(r::software::render(allocation,std::get<p::PreparedFrame>(f)).status==r::software::Status::allocation_failed);fail=false;assert(pixels(allocation)==confirmed);
 std::cout<<"PASS opacity nesting/clip/zero validation/limits/cancellation\n";
}
