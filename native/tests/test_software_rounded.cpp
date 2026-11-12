#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <new>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
p::Radii radii(float r) {return {r,r,r,r,r,r,r,r};}
p::PreparedFrame prepare(std::vector<p::Command> c) {auto r=p::prepare({std::move(c)});assert(std::holds_alternative<p::PreparedFrame>(r));return std::move(std::get<p::PreparedFrame>(r));}
Frame get(FrameStore& s) {std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
// Fixed corpus, recorded before first execution. Reference integrates exact
// vertical circle intersections, unlike the production two-dimensional grid.
struct Case {p::Rect rect;float radius;p::Rect clip;};
constexpr std::array<Case,10> corpus{{
 {{1,1,8,8},0,{0,0,12,12}},{{1,1,8,8},.25f,{0,0,12,12}},
 {{1,1,8,8},.5f,{0,0,12,12}},{{1,1,8,8},1,{0,0,12,12}},
 {{1,1,8,8},3,{0,0,12,12}},{{1,1,8,8},4,{0,0,12,12}},
 {{.2f,.7f,6.5f,7.25f},3,{.75f,.125f,5.5f,6.25f}},
 {{-2.25f,-1.5f,8,8},3,{0,0,12,12}},
 {{3.125f,2.25f,.5f,.5f},.25f,{0,0,12,12}},
 {{1.3f,1.7f,8,8},.5f,{1.5f,1.75f,6.25f,6.125f}}
}};
double reference(Case c,int x,int y,int steps) {
 const double l=c.rect.x,t=c.rect.y,r=l+c.rect.width,b=t+c.rect.height,rad=c.radius;
 const double xl=std::max({double(x),l,double(c.clip.x)}),xr=std::min({double(x+1),r,double(c.clip.x)+c.clip.width});
 const double yt=std::max({double(y),t,double(c.clip.y)}),yb=std::min({double(y+1),b,double(c.clip.y)+c.clip.height});
 if(xr<=xl||yb<=yt)return 0;
 double sum=0;
 for(int i=0;i<steps;++i) {
  const double u=xl+(i+.5)*(xr-xl)/steps;
  const double dx=std::max({l+rad-u,0.0,u-(r-rad)});
  const double inset=rad>0?rad-std::sqrt(std::max(0.0,rad*rad-dx*dx)):0;
  sum+=std::max(0.0,std::min(yb,b-inset)-std::max(yt,t+inset));
 }
 return sum*(xr-xl)/steps;
}
struct Cancel:p::Cancellation {int count{},at;explicit Cancel(int n):at(n){}bool cancelled() noexcept override{return ++count==at;}};
int main(){
 FrameStore store;assert(store.resize({12,12,1})==FrameStatus::ok);double worst=0,ref_error=0;
 for(auto c:corpus){
  auto frame=prepare({p::PushClip{{0,0,12,12},{}},p::PushClip{c.clip,{}},p::Fill{c.rect,radii(c.radius),{1,1,1,1}},p::PopClip{},p::PopClip{}});
  assert(s::render(store,frame).status==s::Status::ok);const auto f=get(store);
  for(int y=0;y<12;++y)for(int x=0;x<12;++x){
   const double expected=reference(c,x,y,4096),fine=reference(c,x,y,8192);
   ref_error=std::max(ref_error,std::abs(expected-fine));
   worst=std::max(worst,std::abs(double(f.pixels[std::size_t(y)*f.stride+std::size_t(x)*4+3])/255-fine));
  }
 }
 assert(ref_error<.1/255);assert(worst<=8.0/255);
 auto rounded=p::Fill{{1,1,8,8},radii(3),{1,0,0,.5f}};
 auto simple=prepare({rounded});assert(s::render(store,simple).status==s::Status::ok);auto f=get(store);
 for(int y=0;y<10;++y)for(int x=0;x<10;++x)assert(f.pixels[std::size_t(y)*f.stride+x*4+3]==f.pixels[std::size_t(9-y)*f.stride+(9-x)*4+3]);
 assert(f.pixels[4*f.stride+4*4+3]==128);assert(f.pixels[3]==0);

 auto opaque=p::Fill{{0,0,12,12},{},{0,0,1,1}};
 assert(s::render(store,prepare({opaque,rounded})).status==s::Status::ok);
 f=get(store);assert(f.pixels[4*f.stride+4*4]==127 && f.pixels[4*f.stride+4*4+2]==128);
 assert(s::render(store,prepare({rounded,opaque})).status==s::Status::ok);
 f=get(store);assert(f.pixels[4*f.stride+4*4]==255 && f.pixels[4*f.stride+4*4+2]==0);
 auto old=f;
 auto bad=rounded;bad.radii.top_left_x=2;
 assert(s::render(store,prepare({rounded,bad})).status==s::Status::unsupported);
 bad=rounded;bad.radii=radii(5);assert(s::render(store,prepare({bad})).status==s::Status::unsupported);
 bad=rounded;bad.rect.width=0;assert(s::render(store,prepare({bad})).status==s::Status::unsupported);
 assert(get(store).sequence==old.sequence&&get(store).pixels==old.pixels);
 Cancel count(100000);assert(s::render(store,simple,&count).status==s::Status::ok);old=get(store);
 for(int i=1;i<=count.count;++i){Cancel c(i);assert(s::render(store,simple,&c).status==s::Status::cancelled);assert(get(store).pixels==old.pixels&&get(store).sequence==old.sequence);}
 bool fail=false;FrameStore oom([&]{if(fail)throw std::bad_alloc{};});assert(oom.resize({12,12,1})==FrameStatus::ok);
 assert(s::render(oom,simple).status==s::Status::ok);auto saved=get(oom);fail=true;
 assert(s::render(oom,simple).status==s::Status::allocation_failed);fail=false;assert(get(oom).pixels==saved.pixels);
 assert(store.resize({500,500,1})==FrameStatus::ok);
 assert(s::render(store,prepare({p::Fill{{0,0,500,500},radii(3),{1,0,0,1}}})).status==s::Status::ok);
 std::cout<<"coverage_max_error="<<worst<<" reference_convergence="<<ref_error<<'\n';
}
