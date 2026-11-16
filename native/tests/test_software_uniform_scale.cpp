#ifdef NDEBUG
#error assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
namespace p=aether::rendering::preparation;
namespace r=aether::rendering;
namespace s=r::software;
using Bytes=std::vector<std::uint8_t>;
constexpr int W=16,H=16;
unsigned q(double x){return unsigned(std::floor(x*255+.5));}
unsigned mul(unsigned x,unsigned y){return (x*y+127)/255;}
struct Shape {double x,y,w,h;std::array<double,8> radii{};};
// Explicit device geometry, never derived using the renderer's transform helper.
// The independent oracle uses ellipse equations in corner-local normalized space.
bool inside(const Shape& a,double x,double y){
 if(x<a.x||x>a.x+a.w||y<a.y||y>a.y+a.h)return false;
 for(int i=0;i<4;++i){double rx=a.radii[i*2],ry=a.radii[i*2+1];if(rx==0||ry==0)continue;
 bool left=i==0||i==3,top=i<2;long double cx=left?a.x+rx:a.x+a.w-rx,cy=top?a.y+ry:a.y+a.h-ry;
 if((left?x<=cx:x>=cx)&&(top?y<=cy:y>=cy)){long double dx=(x-cx)/rx,dy=(y-cy)/ry;if(dx*dx+dy*dy>1)return false;}}
 return true;
}
void blend(Bytes& out,int offset,const std::array<unsigned,4>& src){unsigned inv=255-src[3];for(int c=0;c<4;++c)out[offset+c]=src[c]+mul(out[offset+c],inv);}
void solid(Bytes& out,Shape a,p::Color color,std::vector<Shape> clips={}){
 for(int y=0;y<H;++y)for(int x=0;x<W;++x){
 double l=std::max(double(x),a.x),t=std::max(double(y),a.y),rr=std::min(x+1.,a.x+a.w),bb=std::min(y+1.,a.y+a.h);
 for(auto c:clips){l=std::max(l,c.x);t=std::max(t,c.y);rr=std::min(rr,c.x+c.w);bb=std::min(bb,c.y+c.h);}if(l>=rr||t>=bb)continue;
 // Grid is over the fractional clipped pixel, not the whole device pixel.
 // Circular center-band shortcuts give the same full membership here.
 unsigned n=0;for(int sy=0;sy<16;++sy)for(int sx=0;sx<16;++sx){double px=l+(sx+.5)*(rr-l)/16,py=t+(sy+.5)*(bb-t)/16;bool yes=inside(a,px,py);for(auto c:clips)yes=yes&&inside(c,px,py);n+=yes;}
 unsigned coverage=q((rr-l)*(bb-t)*n/256),alpha=mul(q(color.a),coverage);
 blend(out,(y*W+x)*4,{mul(q(color.b),alpha),mul(q(color.g),alpha),mul(q(color.r),alpha),alpha});
 }}
void gradient(Bytes& out,Shape a,Shape ramp,const p::LinearGradientFill& g,std::vector<Shape> clips={}){
 for(int y=0;y<H;++y)for(int x=0;x<W;++x){double l=std::max(double(x),a.x),t=std::max(double(y),a.y),rr=std::min(x+1.,a.x+a.w),bb=std::min(y+1.,a.y+a.h);for(auto c:clips){l=std::max(l,c.x);t=std::max(t,c.y);rr=std::min(rr,c.x+c.w);bb=std::min(bb,c.y+c.h);}if(l>=rr||t>=bb)continue;
 double sums[4]{};for(int sy=0;sy<16;++sy)for(int sx=0;sx<16;++sx){double px=l+(sx+.5)*(rr-l)/16,py=t+(sy+.5)*(bb-t)/16;bool yes=inside(a,px,py);for(auto c:clips)yes=yes&&inside(c,px,py);if(!yes)continue;
 double v=.5+((px-ramp.x-ramp.w/2)*g.direction_x+(py-ramp.y-ramp.h/2)*g.direction_y)/(ramp.w*std::abs(g.direction_x)+ramp.h*std::abs(g.direction_y));std::size_t j=0;while(j<g.stops.size()&&g.stops[j].offset<=v)++j;
 const auto& aa=g.stops[j?j-1:0];const auto& b=g.stops[j<g.stops.size()?j:g.stops.size()-1];double u=j==0||j==g.stops.size()?0:(v-aa.offset)/(b.offset-aa.offset);
 double av[4]{aa.color.b*double(aa.color.a),aa.color.g*double(aa.color.a),aa.color.r*double(aa.color.a),aa.color.a},bv[4]{b.color.b*double(b.color.a),b.color.g*double(b.color.a),b.color.r*double(b.color.a),b.color.a};for(int c=0;c<4;++c)sums[c]+=(av[c]+(bv[c]-av[c])*u)*g.opacity;}
 std::array<unsigned,4> src{};for(int c=0;c<4;++c)src[c]=q(std::clamp(sums[c]*(rr-l)*(bb-t)/256,0.,1.));for(int c=0;c<3;++c)src[c]=std::min(src[c],src[3]);blend(out,(y*W+x)*4,src);
 }}
p::PushTransform tr(float scale,float x=0,float y=0){return {{scale,0,0,scale,x,y}};}
p::Fill f(p::Rect b={1,1,4,3},p::Radii radii={},p::Color c={1,0,0,1}){return {b,radii,c};}
p::PreparedFrame prepare(std::vector<p::Command> c){auto a=p::prepare({std::move(c)});assert(std::holds_alternative<p::PreparedFrame>(a));return std::move(std::get<p::PreparedFrame>(a));}
r::Frame frame(r::FrameStore& store){std::optional<r::Frame> f;assert(store.copy_confirmed(f)==r::FrameStatus::ok&&f);return *f;}
void unchanged(const r::Frame& a,const r::Frame& b){assert(a.pixels==b.pixels&&a.sequence==b.sequence&&a.stride==b.stride&&a.viewport.width==b.viewport.width&&a.viewport.height==b.viewport.height&&a.viewport.dpi==b.viewport.dpi);}
std::filesystem::path output;std::ofstream manifest;unsigned cases=0;
void dump(std::string name,const Bytes& bytes){std::ofstream o(output/(name+".bgra"),std::ios::binary);o.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(o.good());}
int main(int argc,char** argv){assert(argc==2);output=argv[1];assert(std::filesystem::create_directory(output));manifest.open(output/"manifest.tsv");manifest<<"fixture\twidth\theight\tstride\tformat\tcomparison\n";
 int allocations=0;r::FrameStore store([&]{++allocations;});assert(store.resize({W,H,1})==r::FrameStatus::ok);assert(s::render(store,prepare({f()})).status==s::Status::ok);
 auto check=[&](std::string name,std::vector<p::Command> commands,Bytes expected){auto before=frame(store);auto result=s::render(store,prepare(commands));assert(result.status==s::Status::ok);auto actual=frame(store);dump(name+"-before",before.pixels);dump(name+"-actual",actual.pixels);dump(name+"-expected",expected);manifest<<name<<"\t16\t16\t64\tBGRA8-premultiplied-top-down\t"<<(actual.pixels==expected?"equal":"different")<<'\n';manifest.flush();assert(actual.pixels==expected);++cases;};
 auto blank=[](){return Bytes(W*H*4);};
 auto preservation=[&](std::string name,const r::Frame& before,const r::Frame& after){unchanged(before,after);dump(name+"-before",before.pixels);dump(name+"-actual",after.pixels);dump(name+"-expected",before.pixels);manifest<<name<<'\t'<<before.viewport.width<<'\t'<<before.viewport.height<<'\t'<<before.stride<<"\tBGRA8-premultiplied-top-down\tpreserved\n";};
 for(float scale:{.5f,1.25f,2.f}){auto e=blank();solid(e,{scale-1.25,scale+.5,4*scale,3*scale},{1,0,0,1});check("fractional-"+std::to_string(scale),{tr(scale,-1.25,.5),f(),p::PopTransform{}},e);}
 {auto e=blank();solid(e,{4.5,3,8,6},{1,0,0,1});solid(e,{1,1,1,1},{0,1,0,1});check("parent-scale-child-translation-pops",{tr(2,.5,-1),tr(1,1,1),f(),p::PopTransform{},p::PopTransform{},f({1,1,1,1},{},{0,1,0,1})},e);}
 {auto e=blank();solid(e,{3.5,2,8,6},{1,0,0,1});check("parent-translation-child-scale",{tr(1,1,1),tr(2,.5,-1),f(),p::PopTransform{},p::PopTransform{}},e);}
 {auto e=blank();solid(e,{0,0,8,6},{1,0,0,1});check("resolved-origin",{tr(2,-2,-2),f(),p::PopTransform{}},e);}
 p::Radii ellipse{1,2,2,1,0,2,1.5f,1}; // ABI order TL TR BR BL, confirmed in aether.h
 Shape scaled{-.5,.5,10,7.5,{1.25,2.5,2.5,1.25,0,2.5,1.875,1.25}};
 {auto e=blank();solid(e,scaled,{1,0,0,1});check("elliptic-offscreen-zero-component",{tr(1.25,-.5,.5),f({0,0,8,6},ellipse),p::PopTransform{}},e);}
 {auto e=blank();solid(e,{1.5,.5,12,9,{1.5,3,3,1.5,3.75,2.25,2.25,1.5}},{.25,.5,1,.5});check("four-independent-elliptic-alpha",{tr(1.5,1.5,.5),f({0,0,8,6},{1,2,2,1,2.5,1.5,1.5,1},{.25,.5,1,.5}),p::PopTransform{}},e);}
 for(float scale:{.5f,2.f}){auto e=blank();Shape a{.25,.5,6*scale,4*scale,{scale,scale,scale,scale,scale,scale,scale,scale}};solid(e,a,{1,0,0,1});check("circular-"+std::to_string(scale),{tr(scale,.25,.5),f({0,0,6,4},{1,1,1,1,1,1,1,1}),p::PopTransform{}},e);}
 {auto e=blank();solid(e,{0,0,12,10},{1,0,0,1},{{1.5,.5,6,6}});check("parent-clip-scaled-child",{p::PushClip{{1.5,.5,6,6},{}},tr(2),f({0,0,6,5}),p::PopTransform{},p::PopClip{}},e);}
 {Shape clip{1,1,10,10,{2,2,2,2,2,2,2,2}},clip2{2,2,6,6,{1,2,2,1,0,1,1,1}};auto e=blank();solid(e,{0,0,16,16},{1,0,0,1},{clip,clip2});solid(e,{12,0,2,2},{0,1,0,1});check("captured-curves-descendant-scale-restore",{tr(2,1,1),p::PushClip{{0,0,5,5},{1,1,1,1,1,1,1,1}},tr(.5f,0,0),p::PushClip{{1,1,6,6},{1,2,2,1,0,1,1,1}},f({-1,-1,16,16}),p::PopClip{},p::PopTransform{},p::PopClip{},p::PopTransform{},f({12,0,2,2},{},{0,1,0,1})},e);}
 for(bool diagonal:{false,true}){p::LinearGradientFill g{{0,0,6,5},{-2,-1,10,8},{},diagonal?.70710677f:1.f,diagonal?.70710677f:0.f,.75f,{{0,{1,0,0,1}},{.5,{0,1,0,.5}},{.5,{0,0,1,1}},{1,{1,1,1,.25}}}};auto e=blank();gradient(e,{.5,1,7.5,6.25},{-2,-.25,12.5,10},g,{{2,2,5,4}});check(diagonal?"gradient-diagonal":"gradient-horizontal-hard-alpha",{p::PushClip{{2,2,5,4},{}},tr(1.25,.5,1),g,p::PopTransform{},p::PopClip{}},e);}
 {auto layer=blank();solid(layer,{0,0,8,8},{1,0,0,1});solid(layer,{4,4,8,8},{0,1,0,1});auto e=blank();for(std::size_t i=0;i<e.size();++i)e[i]=mul(layer[i],128);solid(e,{0,12,2,2},{0,0,1,1});check("group-opacity-and-sibling",{tr(2),p::BeginOpacity{1,0,.5},f({0,0,4,4}),f({2,2,4,4},{},{0,1,0,1}),p::EndOpacity{1},p::PopTransform{},f({0,12,2,2},{},{0,0,1,1})},e);}
 auto reject=[&](std::string name,std::vector<p::Command> commands,std::size_t index,bool a8=false){auto prepared=prepare(commands);auto before=frame(store);int n=allocations;auto result=a8?s::render_a8(store,prepared,{}):s::render(store,prepared);assert(result.status==s::Status::unsupported&&result.command_index==index);assert(allocations==n);auto after=frame(store);unchanged(before,after);dump(name+"-before",before.pixels);dump(name+"-actual",after.pixels);dump(name+"-expected",before.pixels);manifest<<name<<"\t16\t16\t64\tBGRA8-premultiplied-top-down\tpreserved-no-candidate\n";++cases;};
 for(float scale:{1.f/16,16.f}){auto e=blank();solid(e,{0,0,scale,scale},{1,0,0,1});check("scale-bound-"+std::to_string(scale),{tr(scale),f({0,0,1,1}),p::PopTransform{}},e);}
 for(float scale:{std::nextafter(1.f/16,0.f),std::nextafter(16.f,17.f),0.f,-1.f})reject("local-bound-"+std::to_string(scale),{tr(scale),f(),p::PopTransform{}},0);
 reject("product-below",{tr(1.f/16),tr(.5),f(),p::PopTransform{},p::PopTransform{}},1);
 reject("zero-component-radius-device-overflow",{tr(2),f({0,0,2,2},{65536,0,0,0,0,0,0,0}),p::PopTransform{}},1);
 reject("product-outside",{tr(16),tr(2),f(),p::PopTransform{},p::PopTransform{}},1);
 for(int kind=0;kind<3;++kind){auto m=p::identity;if(kind==0)m.m22=2;if(kind==1)m.m12=.25;if(kind==2){m.m11=0;m.m22=0;m.m12=1;m.m21=-1;}reject("nonuniform-skew-rotate-"+std::to_string(kind),{p::PushTransform{m},f(),p::PopTransform{}},0);}
 {auto e=blank();check("coordinate-positive-bound",{tr(2),f({32767,0,1,1}),p::PopTransform{}},e);check("coordinate-negative-bound",{tr(2),f({-32768,0,1,1}),p::PopTransform{}},e);}
 reject("coordinate-overflow",{tr(2),f({32768,0,1,1}),p::PopTransform{}},1);
 reject("translation-state-overflow",{tr(2,65536),tr(1,1),p::PopTransform{},p::PopTransform{}},1);
 reject("A8-fill-scale",{tr(2),f(),p::PopTransform{}},0,true);
 reject("A8-local-cancelled-scale",{tr(2),tr(.5),f(),p::PopTransform{},p::PopTransform{}},0,true);
 for(float n:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){auto m=tr(n);assert(std::holds_alternative<p::Error>(p::prepare({{m,f(),p::PopTransform{}}})));}
 p::Stroke stroke{{1,1,3,3},{},1,{1,0,0,1}};p::BorderGroup border;border.outer={1,1,4,4};border.inner={2,2,2,2};border.sides[0]={true,AETHER_BORDER_STYLE_SOLID,{1,0,0,1}};p::BoxShadow shadow{};shadow.rect={1,1,3,3};shadow.shadow.color={1,0,0,1};p::Text text{"x",{1,1,3,3},"test",12,400,AETHER_TEXT_ALIGN_LEFT,{1,0,0,1},0};
 std::array<std::uint8_t,4> data{0,0,255,255};auto imageRef=std::get<p::ImageRef>(p::ImageResource::copy(1,1,data));p::Image image{imageRef,{0,0,1,1},{1,1,3,3},1};
 for(int variant=0;variant<3;++variant){if(variant==1){stroke.rect.width=0;stroke.color.a=0;border.sides[0].color.a=0;shadow.shadow.color.a=0;text.color.a=0;image.opacity=0;}if(variant==2){stroke.rect={100,100,3,3};border.outer={100,100,4,4};border.inner={101,101,2,2};shadow.rect.x=100;text.rect.x=100;image.destination.x=100;}
 std::vector<p::Command> unsupported{stroke,border,shadow,text};for(std::size_t i=0;i<unsupported.size();++i)reject("excluded-"+std::to_string(variant)+"-"+std::to_string(i),{tr(2),unsupported[i],p::PopTransform{}},1);
 // S1-I explicitly evolves excluded-{0,1,2}-4 from rejection to success.
 // Variant 2 historically inherits opacity zero; the new F07-offscreen
 // fixture independently uses positive opacity.
 auto expected=blank();if(variant==0)solid(expected,{2,2,6,6},{1,0,0,1});
 check("admitted-image-legacy-"+std::to_string(variant),{tr(2),image,p::PopTransform{}},expected);}
 // Local scale cancellation restores primitive support, but never legalizes scroll.
 {p::Stroke line{{2,2,4,4},{},2,{1,0,0,1}};auto e=blank();solid(e,{1,1,6,6},{1,0,0,1});for(int y=3;y<5;++y)for(int x=3;x<5;++x)for(int c=0;c<4;++c)e[(y*W+x)*4+c]=0;check("effective-one-stroke",{tr(2),tr(.5),line,p::PopTransform{},p::PopTransform{}},e);}
 reject("scale-before-zero-scroll",{tr(2),p::PopTransform{},p::PushScroll{1,0,0},p::PopScroll{}},2);
 reject("scroll-before-scale",{p::PushScroll{1,1,1},p::PopScroll{},tr(2),p::PopTransform{}},2);
 reject("cancelled-scale-scroll",{tr(2),tr(.5),p::PushScroll{1,0,0},p::PopScroll{},p::PopTransform{},p::PopTransform{}},2);
 {auto e=blank();solid(e,{1.5,2,4,3},{1,0,0,1});check("identity-translation-scroll",{tr(1,1.5,2),p::PushScroll{1,1,1},f(),p::PopScroll{},p::PopTransform{}},e);}
 {auto e=blank();solid(e,{1,1,4,3},{1,0,0,1});check("identity",{tr(1),f(),p::PopTransform{}},e);auto prepared=prepare({f()});auto result=s::render_a8(store,prepared,{});assert(result.status==s::Status::ok&&frame(store).pixels==e);}
 // Allocation and cancellations preserve all confirmed metadata, not just pixels.
 {bool fail=false;int attempts=0;r::FrameStore other([&]{++attempts;if(fail)throw std::bad_alloc();});assert(other.resize({W,H,1})==r::FrameStatus::ok);auto prepared=prepare({tr(2),f(),p::PopTransform{}});assert(s::render(other,prepared).status==s::Status::ok);auto before=frame(other);fail=true;assert(s::render(other,prepared).status==s::Status::allocation_failed);fail=false;preservation("candidate-allocation-failure",before,frame(other));
 struct Cancel:p::Cancellation{int count=0,at;explicit Cancel(int a):at(a){}bool cancelled()noexcept override{return count++==at;}};
 // copy_confirmed also invokes the allocation hook. Freeze render-only attempts
 // before any frame()/preservation() reads can increment that shared counter.
 bool pre=false,during=false;
 for(int n=0;n<60;++n){
  before=frame(other);int a=attempts;Cancel cancel(n);
  auto result=s::render(other,prepared,&cancel);
  const int render_attempts=attempts;
  if(result.status==s::Status::cancelled){
   unchanged(before,frame(other));
   if(render_attempts==a&&!pre)preservation("cancellation-preflight",before,frame(other));
   if(render_attempts>a&&!during)preservation("cancellation-after-candidate",before,frame(other));
   pre|=render_attempts==a;during|=render_attempts>a;
  }
 }
 assert(pre&&during);}
 {int attempts=0;r::FrameStore large([&]{++attempts;});assert(large.resize({256,256,1})==r::FrameStatus::ok);auto seed=prepare({f()});assert(s::render(large,seed).status==s::Status::ok);auto before=frame(large);p::LinearGradientFill g{{0,0,156,156},{0,0,156,156},{},1,0,1,{{0,{1,0,0,1}},{1,{0,0,1,1}}}};
 // 156^2*2048 + 256^2 = 49,905,664 < 50M; scaled/clipped 256^2*2048 > 50M.
 assert(s::render(large,prepare({g})).status==s::Status::ok);before=frame(large);int a=attempts;auto result=s::render(large,prepare({tr(2),g,p::PopTransform{}}));assert(result.status==s::Status::limit_exceeded&&attempts==a);preservation("amplification-budget",before,frame(large));}
 {r::FrameStore dpi;assert(dpi.resize({16,16,2})==r::FrameStatus::ok);assert(s::render(dpi,prepare({f()})).status==s::Status::unsupported);}
 manifest.close();std::cout<<"S1 assertions completed; compared/rejected fixtures="<<cases<<"; independent review pending; coverage_complete=false\n";
}
