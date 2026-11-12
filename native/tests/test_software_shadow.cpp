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
namespace p=aether::rendering::preparation;namespace s=aether::rendering::software;
p::PreparedFrame prep(std::vector<p::Command> c){auto r=p::prepare({std::move(c)});assert(std::holds_alternative<p::PreparedFrame>(r));return std::move(std::get<p::PreparedFrame>(r));}
Frame get(FrameStore& s){std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
struct Case{p::Rect box;float blur,spread,dx,dy;p::Rect clip;};
// Fixed before execution, threshold 2/255 includes infinite-tail reference.
constexpr std::array<Case,12> corpus{{
 {{4,4,8,8},0,2,1,2,{0,0,20,20}},{{4,4,8,8},1,0,0,2,{0,0,20,20}},
 {{4,4,8,8},7,0,0,2,{0,0,20,20}},{{4,4,8,8},8,0,0,2,{0,0,20,20}},
 {{4,4,8,8},64,0,0,2,{0,0,20,20}},{{4.25f,4.5f,7,8},1,.25f,.5f,-.5f,{1.25f,1.5f,16,16}},
 {{4,4,8,8},7,-3,-2,-1,{3.5f,1.25f,12,12}},{{4,4,8,8},8,-4,1,2,{0,0,20,20}},
 {{-12,-4,10,10},8,1,1,2,{0,0,20,20}},{{8,8,1,1},1,-.25f,.25f,.25f,{0,0,20,20}},
 {{2.25f,2.25f,14,14},1,0,0,0,{0,0,20,20}},{{4,4,8,8},64,20,2,2,{.125f,.125f,19,19}}
}};
// Independent composite midpoint integration of the continuous infinite Gaussian.
long double axis(long double lo,long double hi,long double a,long double b,long double sigma,int n){
 if(hi<=lo||b<=a)return 0;
 if(sigma==0)return std::max(0.L,std::min(hi,b)-std::max(lo,a));
 long double sum=0;for(int i=0;i<n;++i){auto x=lo+(i+.5L)*(hi-lo)/n;sum+=(std::erf((x-a)/(sigma*std::sqrt(2.L)))-std::erf((x-b)/(sigma*std::sqrt(2.L))))/2;}return sum*(hi-lo)/n;
}
double reference(Case c,int x,int y,int n){long double l=std::max(double(x),double(c.clip.x)),r=std::min(double(x+1),double(c.clip.x)+c.clip.width),t=std::max(double(y),double(c.clip.y)),b=std::min(double(y+1),double(c.clip.y)+c.clip.height);
 long double sl=double(c.box.x)-c.spread+c.dx,sr=double(c.box.x)+c.box.width+c.spread+c.dx,st=double(c.box.y)-c.spread+c.dy,sb=double(c.box.y)+c.box.height+c.spread+c.dy,sigma=c.blur/2.L;
 return double(axis(l,r,sl,sr,sigma,n)*axis(t,b,st,sb,sigma,n)-axis(std::max(l,(long double)c.box.x),std::min(r,(long double)c.box.x+c.box.width),sl,sr,sigma,n)*axis(std::max(t,(long double)c.box.y),std::min(b,(long double)c.box.y+c.box.height),st,sb,sigma,n));}
p::BoxShadow shadow(Case c){return {c.box,{}, {c.dx,c.dy,c.blur,c.spread,{1,1,1,1},AETHER_FALSE}};}
struct Cancel:p::Cancellation{int count{},at;explicit Cancel(int n):at(n){}bool cancelled()noexcept override{return ++count==at;}};
int main(){FrameStore store;assert(store.resize({20,20,1})==FrameStatus::ok);double worst=0,convergence=0;
 for(auto c:corpus){assert(s::render(store,prep({p::PushClip{c.clip,{}},shadow(c),p::PopClip{}})).status==s::Status::ok);auto out=get(store);for(int y=0;y<20;++y)for(int x=0;x<20;++x){double a=reference(c,x,y,256),b=reference(c,x,y,512);convergence=std::max(convergence,std::abs(a-b));worst=std::max(worst,std::abs(out.pixels[y*out.stride+4*x+3]/255.-b));}}
 assert(convergence<.01/255);assert(worst<=2./255);
 auto sh=shadow(corpus[3]);auto f=prep({sh});assert(s::render(store,f).status==s::Status::ok);auto saved=get(store);for(int y=4;y<12;++y)for(int x=4;x<12;++x)assert(saved.pixels[y*saved.stride+x*4+3]==0);
 for(int i=0;i<6;++i){auto bad=sh;if(i==0)bad.shadow.inset=AETHER_TRUE;if(i==1)bad.shadow.blur_radius=.5f;if(i==2)bad.rect.x=65536;if(i==3)bad.shadow.spread_radius=8193;if(i==4)bad.radii={1,1,1,1,1,1,1,1};if(i==5)bad.rect.width=8193;
 assert(s::render(store,prep({sh,bad})).status==s::Status::unsupported);assert(get(store).pixels==saved.pixels&&get(store).sequence==saved.sequence);}
 auto rr=p::Radii{1,1,1,1,1,1,1,1};assert(s::render(store,prep({p::PushClip{{0,0,20,20},rr},sh,p::PopClip{}})).status==s::Status::unsupported);
 Cancel count(100000);assert(s::render(store,f,&count).status==s::Status::ok);saved=get(store);for(int i=1;i<=count.count;++i){Cancel c(i);assert(s::render(store,f,&c).status==s::Status::cancelled);assert(get(store).pixels==saved.pixels&&get(store).sequence==saved.sequence);}
 bool fail=false;FrameStore oom([&]{if(fail)throw std::bad_alloc{};});assert(oom.resize({20,20,1})==FrameStatus::ok);assert(s::render(oom,f).status==s::Status::ok);saved=get(oom);fail=true;assert(s::render(oom,f).status==s::Status::allocation_failed);fail=false;assert(get(oom).pixels==saved.pixels);
 std::vector<p::Command> many(20000,sh);assert(s::render(store,prep(many)).status==s::Status::limit_exceeded);
 // Geometry endpoints at supported bounds, all offscreen: no conversion overflow.
 auto edge=sh;edge.rect={-65536,-65536,8192,8192};edge.shadow={0,0,0,0,{1,1,1,1},AETHER_FALSE};assert(s::render(store,prep({edge})).status==s::Status::ok);
 std::cout<<"coverage_max_error="<<worst<<" reference_convergence="<<convergence<<"\n";
}
