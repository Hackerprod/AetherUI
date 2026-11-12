#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include "../src/rendering/border_painter.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <new>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
p::Radii radii(float r){return {r,r,r,r,r,r,r,r};}
p::PreparedFrame prepare(std::vector<p::Command> c){auto r=p::prepare({std::move(c)});assert(std::holds_alternative<p::PreparedFrame>(r));return std::move(std::get<p::PreparedFrame>(r));}
Frame get(FrameStore& s){std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
struct Case {p::Rect rect;float radius,width;p::Rect clip;};
// FIXED BEFORE FIRST EXECUTION. No tuning of cases or 16/255 threshold.
// Includes thin/fractional, radius zero inner, collapsed inner, clips and offscreen.
constexpr std::array<Case,10> corpus{{
 {{1.5f,1.5f,9,9},2.5f,1,{0,0,14,14}},
 {{1.25f,1.75f,8,9},.25f,.25f,{0,0,14,14}},
 {{1,1,9,9},.5f,1,{0,0,14,14}},
 {{1,1,9,9},1,3,{0,0,14,14}},
 {{3,3,4,4},2,5,{0,0,14,14}},
 {{1,1,9,9},4.5f,1,{0,0,14,14}},
 {{.2f,.7f,6.5f,7.25f},3,.75f,{.75f,.125f,5.5f,6.25f}},
 {{-2.25f,-1.5f,8,8},3,2,{0,0,14,14}},
 {{3.125f,2.25f,.5f,.5f},.25f,.25f,{0,0,14,14}},
 {{1.3f,1.7f,8,8},.5f,.125f,{1.5f,1.75f,6.25f,6.125f}}
}};
double shape(double l,double t,double r,double b,double rad,p::Rect clip,int x,int y,int steps){
 if(r<=l||b<=t)return 0;
 const double xl=std::max({double(x),l,double(clip.x)}),xr=std::min({double(x+1),r,double(clip.x)+clip.width});
 const double yt=std::max({double(y),t,double(clip.y)}),yb=std::min({double(y+1),b,double(clip.y)+clip.height});
 if(xr<=xl||yb<=yt)return 0;
 double sum=0;
 for(int i=0;i<steps;++i){
  const double u=xl+(i+.5)*(xr-xl)/steps;
  const double dx=std::max({l+rad-u,0.0,u-(r-rad)});
  const double inset=rad>0?rad-std::sqrt(std::max(0.0,rad*rad-dx*dx)):0;
  sum+=std::max(0.0,std::min(yb,b-inset)-std::max(yt,t+inset));
 }
 return sum*(xr-xl)/steps;
}
double reference(Case c,int x,int y,int steps){
 double l=c.rect.x,t=c.rect.y,r=l+c.rect.width,b=t+c.rect.height,h=double(c.width)/2;
 return std::clamp(shape(l-h,t-h,r+h,b+h,c.radius+h,c.clip,x,y,steps)-
  shape(l+h,t+h,r-h,b-h,std::max(c.radius-h,0.0),c.clip,x,y,steps),0.0,1.0);
}
struct Cancel:p::Cancellation{int count{},at;explicit Cancel(int n):at(n){}bool cancelled() noexcept override{return ++count==at;}};
int main(){
 FrameStore store;assert(store.resize({14,14,1})==FrameStatus::ok);double worst=0,convergence=0;
 for(auto c:corpus){
  auto f=prepare({p::PushClip{{0,0,14,14},{}},p::PushClip{c.clip,{}},
   p::Stroke{c.rect,radii(c.radius),c.width,{1,1,1,1}},p::PopClip{},p::PopClip{}});
  auto result=s::render(store,f);assert(result.status==s::Status::ok);
  assert(result.sampled_pixels<=result.corner_pixel_bound);
  assert(result.inner_sampled_pixels<=result.inner_corner_pixel_bound);
  const auto out=get(store);
  for(int y=0;y<14;++y)for(int x=0;x<14;++x){
   const double a=reference(c,x,y,4096),b=reference(c,x,y,8192);
   convergence=std::max(convergence,std::abs(a-b));
   worst=std::max(worst,std::abs(out.pixels[std::size_t(y)*out.stride+std::size_t(x)*4+3]/255.0-b));
  }
 }
 assert(convergence<.1/255);assert(worst<=16.0/255);
 // Real uniform border emitter for .badge: exterior r3, interior r2.
 aether::style::StyleScalars style{};style.opacity=1;
 style.border_styles={AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID};
 style.border_colors={{1,0,0,.5f},{1,0,0,.5f},{1,0,0,.5f},{1,0,0,.5f}};
 std::vector<aether_paint_command_t> commands;
 aether_paint_border(style,{1,1,10,10},radii(3),{1,1,1,1},commands);
 assert(commands.size()==1&&commands[0].tag==AETHER_PAINT_STROKE_ROUNDED_RECT);
 auto v=commands[0].data.stroke_rounded_rect;
 assert(v.rect.x==1.5f&&v.rect.width==9&&v.radii.top_left_x==2.5f);
 p::Stroke stroke{v.rect,v.radii,v.stroke_width,v.color};
 auto f=prepare({stroke});assert(s::render(store,f).status==s::Status::ok);
 auto out=get(store);
 for(std::size_t i=3;i<out.pixels.size();i+=4)assert(out.pixels[i]<=128);
 assert(out.pixels[6*out.stride+6*4+3]==0);
 assert(out.pixels[1*out.stride+6*4+3]==128);
 auto background=p::Fill{{0,0,14,14},{},{0,0,1,1}};
 assert(s::render(store,prepare({background,stroke})).status==s::Status::ok);
 auto opaque=get(store);assert(opaque.pixels[opaque.stride+6*4]==127&&opaque.pixels[opaque.stride+6*4+2]==128);
 assert(s::render(store,prepare({stroke,background})).status==s::Status::ok);assert(get(store).pixels[opaque.stride+6*4+2]==0);
 auto saved=get(store);
 auto bad=stroke;bad.radii.top_left_x=1;
 assert(s::render(store,prepare({stroke,bad})).status==s::Status::unsupported);
 bad=stroke;bad.rect.width=0;
 assert(s::render(store,prepare({stroke,bad})).status==s::Status::unsupported);
 bad=stroke;bad.radii=radii(10);
 assert(s::render(store,prepare({bad})).status==s::Status::unsupported);
 assert(get(store).pixels==saved.pixels&&get(store).sequence==saved.sequence);
 auto zero=stroke;zero.stroke_width=0;assert(s::render(store,prepare({zero})).status==s::Status::ok);
 for(auto b:get(store).pixels)assert(b==0);
 Cancel count(100000);assert(s::render(store,f,&count).status==s::Status::ok);saved=get(store);
 for(int i=1;i<=count.count;++i){Cancel c(i);assert(s::render(store,f,&c).status==s::Status::cancelled);assert(get(store).pixels==saved.pixels&&get(store).sequence==saved.sequence);}
 bool fail=false;FrameStore oom([&]{if(fail)throw std::bad_alloc{};});assert(oom.resize({14,14,1})==FrameStatus::ok);
 assert(s::render(oom,f).status==s::Status::ok);auto old=get(oom);fail=true;
 assert(s::render(oom,f).status==s::Status::allocation_failed);fail=false;assert(get(oom).pixels==old.pixels);
 assert(store.resize({500,500,1})==FrameStatus::ok);
 auto huge=p::Stroke{{1,1,498,498},radii(249),1,{1,1,1,1}};
 assert(s::render(store,prepare({huge})).status==s::Status::limit_exceeded);
 std::cout<<"coverage_max_error="<<worst<<" reference_convergence="<<convergence<<"\n";
}
