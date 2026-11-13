#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/rendering/software/software_renderer.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
static size_t checks=0;
static void ck(bool b){if(!b)std::cerr<<"G1 failed check "<<checks<<'\n';assert(b);++checks;}
static p::PreparedFrame prepared(std::vector<p::Command> c){auto r=p::prepare({std::move(c)});ck(std::holds_alternative<p::PreparedFrame>(r));return std::move(std::get<p::PreparedFrame>(r));}
static Frame frame(FrameStore& s){std::optional<Frame> f;ck(s.copy_confirmed(f)==FrameStatus::ok&&bool(f));return *f;}
static p::LinearGradientFill gradient(p::Rect r={0,0,16,12},float dx=1,float dy=0){p::LinearGradientFill g;g.rect=g.gradient_rect=r;g.direction_x=dx;g.direction_y=dy;g.stops={{0,{1,0,0,1}},{1,{0,0,1,1}}};return g;}
static std::array<int,4> pixel(const Frame& f,int x,int y){size_t i=y*f.stride+x*4;return {f.pixels[i],f.pixels[i+1],f.pixels[i+2],f.pixels[i+3]};}
// Independent reference: scan stop intervals, integrate with a separate 64x64
// midpoint grid. Does not call renderer geometry, interpolation or search code.
static std::array<int,4> oracle(const p::LinearGradientFill& g,int x,int y,double sx=0,double sy=0,p::Rect clip={-100,-100,1000,1000},double layer=1,double clip_radius=0){
 std::array<double,4> sum{};
 for(int yy=0;yy<64;++yy)for(int xx=0;xx<64;++xx){
  const double px=x+(xx+.5)/64,py=y+(yy+.5)/64;
  if(px<clip.x||px>=clip.x+clip.width||py<clip.y||py>=clip.y+clip.height)continue;
  if(clip_radius){double cx=std::max(double(clip.x)+clip_radius,std::min(px,double(clip.x+clip.width)-clip_radius)),cy=std::max(double(clip.y)+clip_radius,std::min(py,double(clip.y+clip.height)-clip_radius));if((px-cx)*(px-cx)+(py-cy)*(py-cy)>clip_radius*clip_radius)continue;}
  const double u=px-sx,v=py-sy;
  if(u<g.rect.x||u>=g.rect.x+g.rect.width||v<g.rect.y||v>=g.rect.y+g.rect.height)continue;
  double radius=g.radii.top_left_x;
  if(radius){double cx=std::max(double(g.rect.x)+radius,std::min(u,double(g.rect.x+g.rect.width)-radius)),cy=std::max(double(g.rect.y)+radius,std::min(v,double(g.rect.y+g.rect.height)-radius));if((u-cx)*(u-cx)+(v-cy)*(v-cy)>radius*radius)continue;}
  double dx=g.direction_x,dy=g.direction_y,n=std::hypot(dx,dy);dx/=n;dy/=n;
  double length=std::abs(g.gradient_rect.width*dx)+std::abs(g.gradient_rect.height*dy);
  double t=.5+((u-g.gradient_rect.x-g.gradient_rect.width/2)*dx+(v-g.gradient_rect.y-g.gradient_rect.height/2)*dy)/length;
  size_t a=0,b=0;
  if(t<g.stops.front().offset)a=b=0;
  else if(t>=g.stops.back().offset)a=b=g.stops.size()-1;
  else {for(size_t i=0;i+1<g.stops.size();++i)if(t>=g.stops[i].offset&&t<g.stops[i+1].offset){a=i;b=i+1;break;}}
  const auto ca=g.stops[a].color,cb=g.stops[b].color;
  double q=a==b?0:(t-g.stops[a].offset)/(g.stops[b].offset-g.stops[a].offset);
  const double alpha=(ca.a*(1-q)+cb.a*q)*g.opacity*layer;
  sum[0]+=(ca.b*ca.a*(1-q)+cb.b*cb.a*q)*g.opacity*layer;
  sum[1]+=(ca.g*ca.a*(1-q)+cb.g*cb.a*q)*g.opacity*layer;
  sum[2]+=(ca.r*ca.a*(1-q)+cb.r*cb.a*q)*g.opacity*layer;
  sum[3]+=alpha;
 }
 std::array<int,4> o{};for(int i=0;i<4;++i)o[i]=int(std::floor(sum[i]*255/4096+.5));return o;
}
static void compare(const Frame& f,const p::LinearGradientFill& g,double sx=0,double sy=0,p::Rect clip={-100,-100,1000,1000},double layer=1,double clip_radius=0){for(int y=0;y<f.viewport.height;++y)for(int x=0;x<f.viewport.width;++x){auto got=pixel(f,x,y),want=oracle(g,x,y,sx,sy,clip,layer,clip_radius);for(int i=0;i<4;++i){if(std::abs(got[i]-want[i])>2)std::cerr<<"pixel "<<x<<','<<y<<" channel "<<i<<" got "<<got[i]<<" want "<<want[i]<<'\n';ck(std::abs(got[i]-want[i])<=2);}}}
struct Cancel:p::Cancellation{int calls=0,at;explicit Cancel(int a):at(a){}bool cancelled()noexcept override{return ++calls==at;}};
int main(){
 FrameStore store;ck(store.resize({24,20,1})==FrameStatus::ok);
 for(auto direction:std::array<std::array<float,2>,5>{{{1,0},{0,1},{-1,0},{0,-1},{.6f,.8f}}}){
  auto g=gradient({3,2,17,11},direction[0],direction[1]);auto pf=prepared({g});ck(s::render(store,pf).status==s::Status::ok);compare(frame(store),g);
 }
 // Ramp origin/size is not coverage geometry; clipping cannot restart colors.
 auto g=gradient({5,3,9,8});g.gradient_rect={2,1,19,13};
 ck(s::render(store,prepared({g})).status==s::Status::ok);compare(frame(store),g);
 // Transparent red MUST contribute no red or black band in premultiplied space.
 g=gradient();g.stops={{0,{1,0,0,0}},{1,{0,0,1,1}}};
 ck(s::render(store,prepared({g})).status==s::Status::ok);compare(frame(store),g);
 ck(pixel(frame(store),8,5)[2]==0);
 // Duplicate stops are a discontinuity, including an exact pixel-center stop.
 g=gradient();g.stops={{0,{1,0,0,1}},{.5f,{1,0,0,1}},{.5f,{0,1,0,1}},{1,{0,1,0,1}}};
 ck(s::render(store,prepared({g})).status==s::Status::ok);compare(frame(store),g);ck(pixel(frame(store),7,5)==std::array<int,4>{0,0,255,255});ck(pixel(frame(store),8,5)==std::array<int,4>{0,255,0,255});
 // Scroll is accumulated once for both ramp and shape; a rect clip stays fixed.
 g=gradient({5,5,16,12});p::Rect clip{3,3,10,9};
 ck(s::render(store,prepared({p::PushClip{clip,{}},p::PushScroll{1,2,3},g,p::PopScroll{},p::PopClip{}})).status==s::Status::ok);compare(frame(store),g,-2,-3,clip);
 // Rounded shape, fractional clipping, command opacity then isolated layer.
 g=gradient({1,1,16,12});g.radii={3,3,3,3,3,3,3,3};g.opacity=.5f;clip={2.25f,2.5f,12,9};
 ck(s::render(store,prepared({p::BeginOpacity{1,0,.5f},p::PushClip{clip,{}},g,p::PopClip{},p::EndOpacity{1}})).status==s::Status::ok);compare(frame(store),g,0,0,clip,.5);
 g=gradient({1,1,16,12});g.radii={3,3,3,3,3,3,3,3};clip={2,2,12,9};
 ck(s::render(store,prepared({p::PushClip{clip,{2,2,2,2,2,2,2,2}},g,p::PopClip{}})).status==s::Status::ok);compare(frame(store),g,0,0,clip,1,2);
 // Full accepted stop cardinality and bounded search, all stops owning.
 g=gradient();g.stops.clear();for(int i=0;i<64;++i)g.stops.push_back({i/63.f,{i/63.f,1-i/63.f,.25f,1}});
 ck(s::render(store,prepared({g})).status==s::Status::ok);compare(frame(store),g);
 // Snapshot/prepared stops own their bytes, independent of the producer lifetime.
 auto owned=prepared({gradient()});ck(s::render(store,owned).status==s::Status::ok);compare(frame(store),gradient());
 auto producer=gradient();auto independent=prepared({producer});producer.stops.assign(2,{0,{0,1,0,1}});
 ck(s::render(store,independent).status==s::Status::ok);compare(frame(store),gradient());
 const auto before=frame(store);
 auto invalid=gradient();invalid.stops.resize(1);ck(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 invalid=gradient();invalid.stops.resize(65);ck(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 invalid=gradient();invalid.stops[0].offset=-.1f;ck(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 invalid=gradient();invalid.stops[1].offset=1.1f;ck(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 invalid=gradient();invalid.stops[0].offset=.9f;invalid.stops[1].offset=.1f;ck(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 invalid=gradient();invalid.direction_x=invalid.direction_y=0;ck(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 invalid=gradient();invalid.opacity=std::numeric_limits<float>::quiet_NaN();ck(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 invalid=gradient();invalid.radii.top_left_x=2;ck(s::render(store,prepared({invalid})).status==s::Status::unsupported);ck(frame(store).sequence==before.sequence);
 // Unsupported geometry rejects even outside the viewport (no visibility gate).
 invalid=gradient({1000,1000,10,10});invalid.radii.top_left_x=2;
 ck(s::render(store,prepared({invalid})).status==s::Status::unsupported);ck(frame(store).sequence==before.sequence);
 auto matrix=p::identity;matrix.dx=1;
 ck(s::render(store,prepared({p::PushTransform{matrix},gradient(),p::PopTransform{}})).status==s::Status::unsupported);ck(frame(store).sequence==before.sequence);
 // Total stop budget checked before deep copies into prepared events.
 p::Snapshot excessive;auto many=gradient();many.stops.resize(64,{1,{0,0,1,1}});
 for(int i=0;i<1025;++i)excessive.commands.push_back(many);
 auto rejected=p::prepare(excessive);ck(std::holds_alternative<p::Error>(rejected));ck(std::get<p::Error>(rejected).code==p::ErrorCode::LIMIT_EXCEEDED);
 Cancel count(100000);ck(s::render(store,owned,&count).status==s::Status::ok);const auto saved=frame(store);
 for(int i=1;i<=count.calls;++i){Cancel cancel(i);ck(s::render(store,owned,&cancel).status==s::Status::cancelled);auto f=frame(store);ck(f.sequence==saved.sequence&&f.pixels==saved.pixels);}
 bool fail=false;FrameStore oom([&]{if(fail)throw std::bad_alloc();});ck(oom.resize({24,20,1})==FrameStatus::ok);ck(s::render(oom,owned).status==s::Status::ok);fail=true;ck(s::render(oom,owned).status==s::Status::allocation_failed);fail=false;ck(frame(oom).sequence==1);
 FrameStore big;ck(big.resize({1024,1024,1})==FrameStatus::ok);auto huge=gradient({0,0,1024,1024});ck(s::render(big,prepared({huge})).status==s::Status::limit_exceeded);
 std::cout<<"G1 independent gradient checks="<<checks<<" oracle=analytic_srgb_premul_grid64 chromium=NOT_RUN\n";
}
