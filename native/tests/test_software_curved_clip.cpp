#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <array>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <new>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
p::Radii radii(float r){return {r,r,r,r,r,r,r,r};}
p::PreparedFrame prep(std::vector<p::Command> c){auto r=p::prepare({std::move(c)});assert(std::holds_alternative<p::PreparedFrame>(r));return std::move(std::get<p::PreparedFrame>(r));}
Frame get(FrameStore& s){std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
struct Shape{double l,t,r,b,rad;};
struct Span{double t,b;};
Span span(Shape s,double x){if(s.r<=s.l||s.b<=s.t||x<s.l||x>s.r)return {0,0};double dx=std::max({s.l+s.rad-x,0.,x-(s.r-s.rad)});double inset=s.rad?s.rad-std::sqrt(std::max(0.,s.rad*s.rad-dx*dx)):0;return {s.t+inset,s.b-inset};}
Span overlap(Span a,Span b){return {std::max(a.t,b.t),std::min(a.b,b.b)};}
double length(Span a){return std::max(0.,a.b-a.t);}
struct Case{p::Rect draw;float radius,width;p::Rect clip;float clip_radius;p::Rect rect_clip;};
// Corpus fixed before execution, threshold 16/255. Independent 1D vertical
// integration, not the renderer's 16x16 membership algorithm.
constexpr std::array<Case,10> corpus{{
 {{1,1,10,10},3,0,{1,1,10,10},3,{0,0,14,14}},
 {{0,0,14,14},0,0,{1.25f,1.75f,8,9},3,{0,0,14,14}},
 {{1.25f,1.5f,9,9},2.5f,1,{2,2,8,8},3,{0,0,14,14}},
 {{-2,-1,12,12},3,2,{.25f,.75f,8,8},4,{1.125f,.8f,7,8}},
 {{1,1,10,10},.25f,.125f,{1,1,10,10},.25f,{0,0,14,14}},
 {{1.25f,1.5f,8,9},0,1,{1.75f,2.25f,7,8},.5f,{0,0,14,14}},
 {{1,1,10,10},5,0,{2,2,8,8},4,{0,0,14,14}},
 {{3,3,4,4},2,5,{1,1,9,9},3,{0,0,14,14}},
 {{1,1,10,10},1,3,{.5f,.5f,11,11},3,{0,0,14,14}},
 {{1,1,10,10},3,0,{1,1,10,10},3,{1.25f,1.75f,9,8}}
}};
double ref(Case c,int x,int y,int steps){
 const double h=c.width/2.;Shape outer{c.draw.x-h,c.draw.y-h,c.draw.x+c.draw.width+h,c.draw.y+c.draw.height+h,c.radius>0?c.radius+h:0};
 Shape inner{c.draw.x+h,c.draw.y+h,c.draw.x+c.draw.width-h,c.draw.y+c.draw.height-h,std::max(c.radius-h,0.)};
 Shape clip{c.clip.x,c.clip.y,c.clip.x+c.clip.width,c.clip.y+c.clip.height,c.clip_radius};
 double l=std::max({double(x),outer.l,clip.l,double(c.rect_clip.x)}),r=std::min({double(x+1),outer.r,clip.r,double(c.rect_clip.x+c.rect_clip.width)});
 if(r<=l)return 0;
 double sum=0;
 for(int i=0;i<steps;++i){double u=l+(i+.5)*(r-l)/steps;Span limit{std::max(double(y),double(c.rect_clip.y)),std::min(double(y+1),double(c.rect_clip.y+c.rect_clip.height))};auto a=overlap(overlap(span(outer,u),span(clip,u)),limit);sum+=length(a)-(c.width?length(overlap(a,span(inner,u))):0);}
 return sum*(r-l)/steps;
}
struct Cancel:p::Cancellation{int count{},at;explicit Cancel(int n):at(n){}bool cancelled() noexcept override{return ++count==at;}};
int main(){FrameStore store;assert(store.resize({14,14,1})==FrameStatus::ok);double worst=0,convergence=0;
 for(auto c:corpus){p::Command draw=c.width?p::Command(p::Stroke{c.draw,radii(c.radius),c.width,{1,1,1,1}}):p::Command(p::Fill{c.draw,radii(c.radius),{1,1,1,1}});
 auto f=prep({p::PushClip{{0,0,14,14},{}},p::PushClip{c.clip,radii(c.clip_radius)},p::PushClip{c.rect_clip,{}},draw,p::PopClip{},p::PopClip{},p::PopClip{}});auto result=s::render(store,f);assert(result.status==s::Status::ok);assert(result.joint_sampled_pixels<=result.clip_corner_pixel_bound);auto out=get(store);
 for(int y=0;y<14;++y)for(int x=0;x<14;++x){double a=ref(c,x,y,4096),b=ref(c,x,y,8192);convergence=std::max(convergence,std::abs(a-b));worst=std::max(worst,std::abs(out.pixels[y*out.stride+x*4+3]/255.-b));}}
 assert(convergence<.1/255);assert(worst<=16./255);
 p::Fill fill{{1,1,10,10},radii(3),{1,0,0,.5f}};p::PushClip clip{fill.rect,fill.radii};
 assert(s::render(store,prep({fill})).status==s::Status::ok);auto no_clip=get(store);assert(s::render(store,prep({clip,fill,p::PopClip{}})).status==s::Status::ok);assert(get(store).pixels==no_clip.pixels);
 // Curved state and rectangle state restore together; disjoint subsequent fill
 // must not retain stale clip. Multiple non-overlapping lifetimes are supported.
 p::Fill last{{12,12,2,2},{},{0,1,0,1}};
 auto f=prep({clip,fill,p::PushClip{{4,4,1,1},{}},fill,p::PopClip{},fill,p::PopClip{},last});
 assert(s::render(store,f).status==s::Status::ok);assert(get(store).pixels[13*get(store).stride+13*4+3]==255);
 auto saved=get(store);assert(s::render(store,prep({fill,clip,clip,fill,p::PopClip{},p::PopClip{}})).status==s::Status::unsupported);assert(get(store).pixels==saved.pixels);
 auto bad=clip;bad.radii.top_left_y=2;assert(s::render(store,prep({fill,bad,fill,p::PopClip{}})).status==s::Status::unsupported);assert(get(store).pixels==saved.pixels);
 auto small=clip;small.rect={0,0,0,0};assert(s::render(store,prep({small,p::PopClip{}})).status==s::Status::unsupported);
 auto empty=prep({p::PushClip{{20,20,10,10},radii(3)},fill,p::PopClip{}});assert(s::render(store,empty).status==s::Status::ok);for(auto v:get(store).pixels)assert(v==0);
 Cancel count(100000);assert(s::render(store,f,&count).status==s::Status::ok);saved=get(store);for(int i=1;i<=count.count;++i){Cancel c(i);assert(s::render(store,f,&c).status==s::Status::cancelled);assert(get(store).pixels==saved.pixels&&get(store).sequence==saved.sequence);}
 bool fail=false;FrameStore oom([&]{if(fail)throw std::bad_alloc{};});assert(oom.resize({14,14,1})==FrameStatus::ok);assert(s::render(oom,f).status==s::Status::ok);saved=get(oom);fail=true;assert(s::render(oom,f).status==s::Status::allocation_failed);fail=false;assert(get(oom).pixels==saved.pixels);
 assert(store.resize({500,500,1})==FrameStatus::ok);assert(s::render(store,prep({p::PushClip{{0,0,500,500},radii(250)},p::Fill{{0,0,500,500},{},{1,1,1,1}},p::PopClip{}})).status==s::Status::limit_exceeded);
 std::cout<<"coverage_max_error="<<worst<<" reference_convergence="<<convergence<<"\n";
}
