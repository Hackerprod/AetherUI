#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <cassert>
#include <array>
#include <iostream>
#include <new>
#include <limits>
#include <cstdlib>
static long fail=-1;
void* operator new(std::size_t n){if(fail==0)throw std::bad_alloc();if(fail>0)--fail;void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
using namespace aether::rendering;namespace p=aether::rendering::preparation;namespace s=aether::rendering::software;
p::PreparedFrame prep(std::vector<p::Command> c){auto r=p::prepare(p::Snapshot{std::move(c)});assert(std::holds_alternative<p::PreparedFrame>(r));return std::move(std::get<p::PreparedFrame>(r));}
Frame get(FrameStore& store){std::optional<Frame> f;assert(store.copy_confirmed(f)==FrameStatus::ok);assert(f);return *f;}
void px(const Frame& f,int x,int y,std::array<int,4> v){for(int c=0;c<4;++c)assert(f.pixels[y*f.stride+x*4+c]==v[c]);}
struct Cancel:p::Cancellation{int left;explicit Cancel(int n):left(n){}bool cancelled()noexcept override{return --left==0;}};
int main(){
 std::array<std::uint8_t,16> data{0,0,255,255,0,255,0,255,255,0,0,255,255,255,255,255};
 auto ref=std::get<p::ImageRef>(p::ImageResource::copy(2,2,data));p::Image image{ref,{0,0,2,2},{0,0,2,2},1};
 FrameStore store;assert(store.resize({4,4,1})==FrameStatus::ok);
 auto draw=[&](std::vector<p::Command> c){auto f=prep(std::move(c));assert(s::render(store,f).status==s::Status::ok);return get(store);};
 auto f=draw({image});px(f,0,0,{0,0,255,255});px(f,1,0,{0,255,0,255});px(f,0,1,{255,0,0,255});px(f,1,1,{255,255,255,255});
 image.destination={0,0,4,4};f=draw({image});px(f,1,0,{0,64,191,255});px(f,2,0,{0,191,64,255});
 image.source={1,0,1,1};f=draw({image});px(f,0,0,{0,255,0,255});px(f,3,3,{0,255,0,255});
 image.source={0,0,2,2};image.destination={0,0,1,1};f=draw({image});px(f,0,0,{128,128,128,255});
 image.source={0,0,1,1};image.destination={0,0,4,4};f=draw({image});px(f,3,3,{0,0,255,255});
 image.destination={.5f,.5f,1,1};f=draw({image});px(f,0,0,{0,0,64,64});px(f,1,1,{0,0,64,64});
 image.source={0,0,2,2};image.destination={0,0,1,1};f=draw({p::PushClip{{.5f,0,.5f,1},{}},image,p::PopClip{}});px(f,0,0,{64,64,64,128});
 image.source={0,0,1,1};image.destination={0,0,1,1};image.opacity=.5f;
 f=draw({p::Fill{{0,0,4,4},{},{0,0,1,1}},image});px(f,0,0,{127,0,128,255});
 f=draw({p::BeginOpacity{1,0,.5f},image,p::EndOpacity{1}});px(f,0,0,{0,0,64,64});
 f=draw({p::BeginOpacity{1,0,.5f},p::BeginOpacity{2,1,.5f},image,p::EndOpacity{2},p::EndOpacity{1}});px(f,0,0,{0,0,32,32});
 auto semi=std::get<p::ImageRef>(p::ImageResource::copy(2,1,std::array<std::uint8_t,8>{0,0,128,128,0,0,0,0}));
 f=draw({p::Image{semi,{0,0,2,1},{0,0,1,1},1}});px(f,0,0,{0,0,64,64});
 image.destination={2,2,1,1};image.opacity=1;
 f=draw({p::PushScroll{1,1,1},image,p::PopScroll{}});px(f,1,1,{0,0,255,255});px(f,2,2,{0,0,0,0});
 image.destination={0,0,1,1};
 auto matrix=p::identity;matrix.dx=1;matrix.dy=1;image.opacity=1;
 f=draw({p::PushTransform{matrix},image,p::PopTransform{},image});px(f,1,1,{0,0,255,255});px(f,0,0,{0,0,255,255});
 p::Radii radii{1,1,1,1,1,1,1,1};image.destination={0,0,2,2};f=draw({p::PushClip{{0,0,2,2},radii},image,p::PopClip{}});assert(f.pixels[3]>190&&f.pixels[3]<215);
 auto saved=get(store);image.source={.5f,0,1,1};auto bad=prep({image});assert(s::render(store,bad).status==s::Status::unsupported);assert(get(store).pixels==saved.pixels&&get(store).sequence==saved.sequence);
 image.source={1,0,std::numeric_limits<float>::denorm_min(),1};auto tiny=prep({image});assert(s::render(store,tiny).status==s::Status::unsupported);assert(get(store).pixels==saved.pixels&&get(store).sequence==saved.sequence);
 image.source={0,0,1,1};image.destination={65535,0,1,1};matrix.dx=2;matrix.dy=0;auto badTranslation=prep({p::PushTransform{matrix},image,p::PopTransform{}});assert(s::render(store,badTranslation).status==s::Status::unsupported);
 image.destination={0,0,4,4};auto normal=prep({image});int oom=0;
 for(int n=0;n<20;++n){fail=n;auto r=s::render(store,normal);fail=-1;if(r.status==s::Status::ok)break;assert(r.status==s::Status::allocation_failed);assert(get(store).pixels==saved.pixels&&get(store).sequence==saved.sequence);++oom;}assert(oom>=2);
 saved=get(store);for(int n=1;n<8;++n){Cancel c(n);assert(s::render(store,normal,&c).status==s::Status::cancelled);assert(get(store).pixels==saved.pixels&&get(store).sequence==saved.sequence);}
 FrameStore large;assert(large.resize({2048,2048,1})==FrameStatus::ok);image.destination={0,0,2048,2048};auto badLarge=prep({image});assert(s::render(large,badLarge).status==s::Status::limit_exceeded);
 std::cout<<"Image raster PASS OOM="<<oom<<"\n";
}
