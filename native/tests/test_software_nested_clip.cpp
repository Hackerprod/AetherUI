#include "elliptic_t5_transition.h"
#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
namespace p=aether::rendering::preparation;namespace s=aether::rendering::software;using namespace aether::rendering;
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto* ptr=std::malloc(n?n:1))return ptr;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static p::Radii radii(float r){return {r,r,r,r,r,r,r,r};}
static p::PreparedFrame prepare(std::vector<p::Command> commands){auto result=p::prepare(p::Snapshot{std::move(commands)});assert(std::holds_alternative<p::PreparedFrame>(result));return std::move(std::get<p::PreparedFrame>(result));}
static Frame pixels(FrameStore& store){std::optional<Frame> f;assert(store.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
struct Clip{double l,t,r,b,radius;};
static bool inside(const Clip& c,long double x,long double y){
 if(x<c.l||x>c.r||y<c.t||y>c.b)return false;
 const long double cx=std::max((long double)c.l+c.radius,std::min((long double)c.r-c.radius,x));
 const long double cy=std::max((long double)c.t+c.radius,std::min((long double)c.b-c.radius,y));
 return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=c.radius*c.radius;
}
static std::uint64_t pixels_in(double l,double t,double r,double b){return r>l&&b>t?std::uint64_t(std::ceil(r)-std::floor(l))*std::uint64_t(std::ceil(b)-std::floor(t)):0;}
static std::array<std::uint64_t,3> bound(const std::vector<Clip>& clips){
 double l=0,t=0,r=16,b=16;for(const auto& c:clips){l=std::max(l,c.l);t=std::max(t,c.t);r=std::min(r,c.r);b=std::min(b,c.b);}auto n=pixels_in(l,t,r,b);std::uint64_t sum=0,depth=0;
 for(const auto& c:clips)if(c.radius>0){++depth;for(double x:{c.l,c.r-c.radius})for(double y:{c.t,c.b-c.radius})sum+=pixels_in(std::max(l,x),std::max(t,y),std::min(r,x+c.radius),std::min(b,y+c.radius));}
 return {n,std::min(n,sum),depth};
}
static int expected(const std::vector<Clip>& clips,int px,int py){
 long double left=px,top=py,right=px+1,bottom=py+1;
 for(const auto& c:clips){left=std::max(left,(long double)c.l);top=std::max(top,(long double)c.t);right=std::min(right,(long double)c.r);bottom=std::min(bottom,(long double)c.b);}
 if(right<=left||bottom<=top)return 0;
 unsigned count=0;
 for(int y=0;y<16;++y)for(int x=0;x<16;++x){long double sx=left+(x+.5L)*(right-left)/16,sy=top+(y+.5L)*(bottom-top)/16;bool hit=true;for(const auto& c:clips)hit=inside(c,sx,sy)&&hit;count+=hit;}
 return int(std::floor((right-left)*(bottom-top)*count/256*255+.5L));
}
static std::vector<p::Command> sequence(const std::vector<Clip>& clips,int kind,p::ImageRef image){
 std::vector<p::Command> out;for(const auto& c:clips)out.push_back(p::PushClip{{float(c.l),float(c.t),float(c.r-c.l),float(c.b-c.t)},radii(c.radius)});
 if(kind==0)out.push_back(p::Fill{{0,0,16,16},{},{1,1,1,1}});
 if(kind==1)out.push_back(p::Image{image,{0,0,1,1},{0,0,16,16},1});
 if(kind==2){p::LinearGradientFill g;g.rect=g.gradient_rect={0,0,16,16};g.direction_x=1;g.stops={{0,{1,1,1,1}},{1,{1,1,1,1}}};out.push_back(g);}
 for(std::size_t i=0;i<clips.size();++i)out.push_back(p::PopClip{});return out;
}
struct Span{long double top,bottom;};
static Span cross_section(const Clip& c,long double x){if(c.r<=c.l||c.b<=c.t||x<c.l||x>c.r)return {0,0};long double delta=std::max({(long double)c.l+c.radius-x,0.L,x-((long double)c.r-c.radius)});long double inset=c.radius?c.radius-std::sqrt(std::max(0.L,c.radius*c.radius-delta*delta)):0;return {c.t+inset,c.b-inset};}
static Span intersect_span(Span a,Span b){return {std::max(a.top,b.top),std::min(a.bottom,b.bottom)};}
static long double span_length(Span s){return std::max(0.L,s.bottom-s.top);}
static long double coverage(const std::vector<Clip>& clips,Clip outer,Clip inner,int px,int py,int steps){
 long double l=std::max((long double)px,(long double)outer.l),r=std::min((long double)px+1,(long double)outer.r);for(const auto& c:clips){l=std::max(l,(long double)c.l);r=std::min(r,(long double)c.r);}if(r<=l)return 0;
 long double sum=0;for(int n=0;n<steps;++n){long double x=l+(n+.5L)*(r-l)/steps;Span a=intersect_span({(long double)py,(long double)py+1},cross_section(outer,x));for(const auto& c:clips)a=intersect_span(a,cross_section(c,x));sum+=span_length(a)-span_length(intersect_span(a,cross_section(inner,x)));}return sum*(r-l)/steps;
}
static std::array<long double,4> gradient_coverage(const std::vector<Clip>& clips,int px,int py,int steps){
 long double l=px,r=px+1;for(const auto& c:clips){l=std::max(l,(long double)c.l);r=std::min(r,(long double)c.r);}std::array<long double,4> sums{};if(r<=l)return sums;
 for(int n=0;n<steps;++n){long double x=l+(n+.5L)*(r-l)/steps;Span a{(long double)py,(long double)py+1};for(const auto& c:clips)a=intersect_span(a,cross_section(c,x));const long double h=span_length(a),u=std::clamp((x-.5L)/13,0.L,1.L);sums[0]+=.75L*u*h;sums[1]+=(.0625L*(1-u)+.375L*u)*h;sums[2]+=.25L*(1-u)*h;sums[3]+=(.25L+.5L*u)*h;}
 for(auto& channel:sums)channel*=.75L*(r-l)/steps;return sums;
}
struct Cancel:p::Cancellation{unsigned calls=0,at;explicit Cancel(unsigned n):at(n){}bool cancelled()noexcept override{return ++calls==at;}};
int main(){
 FrameStore store;assert(store.resize({16,16,1})==FrameStatus::ok);
 const auto image=std::get<p::ImageRef>(p::ImageResource::copy(1,1,std::array<std::uint8_t,4>{255,255,255,255}));
 std::vector<std::vector<Clip>> corpus{
  {{1,1,13,13,4},{1,1,13,13,4}},
  {{.25,.75,13.25,14.75,5},{2.25,1.25,14.25,12.25,4}},
  {{0,0,15,15,7},{1.25,2.25,11.25,12.25,5},{3,.5,14,13.5,3}},
  {{-2,-1,10,12,5},{2,2,14,14,5},{1.5,1.5,12.5,11.5,0}},
  {{0,0,4,4,2},{8,8,12,12,2}},
  {{1.5,1.25,14.5,14.25,.25},{1.75,1.5,13.75,13.5,.5}},
  {{0,0,16,16,8},{2,2,14,14,6},{4,4,12,12,4}}
 };
 std::vector<Clip> eight;for(int i=0;i<8;++i)eight.push_back({.25*i,.125*i,15-.125*i,15-.25*i,4-.25*i});corpus.push_back(eight);
 unsigned frames=0,checks=0;
 for(const auto& clips:corpus)for(int kind=0;kind<3;++kind){auto prepared=prepare(sequence(clips,kind,image));auto result=s::render(store,prepared);assert(result.status==s::Status::ok);const auto limit=bound(clips);assert(result.clip_central_tests<=limit[2]*limit[0]);assert(result.clip_contains_tests<=256*limit[2]*limit[1]);if(limit[1]==0)assert(result.clip_contains_tests==0);assert(result.clip_node_count<=65536);assert(result.max_clip_depth<=256);auto f=pixels(store);for(int y=0;y<16;++y)for(int x=0;x<16;++x){int want=expected(clips,x,y);for(int c=0;c<4;++c){assert(f.pixels[y*f.stride+x*4+c]==want);++checks;}}++frames;}
 // Variable premultiplied gradient: independent long-double color/alpha oracle.
 unsigned gradient_checks=0;long double gradient_worst=0,gradient_convergence=0;
 for(const auto& clips:{corpus[1],corpus[2],corpus.back()}){
  auto commands=sequence(clips,2,image);auto& g=std::get<p::LinearGradientFill>(commands[clips.size()]);g.gradient_rect={.5f,-1,13,18};g.opacity=.75f;g.stops={{0,{1,.25f,0,.25f}},{1,{0,.5f,1,.75f}}};
  auto prepared=prepare(std::move(commands));auto result=s::render(store,prepared);assert(result.status==s::Status::ok);const auto budget=bound(clips);assert(result.clip_central_tests<=budget[2]*budget[0]);assert(result.clip_contains_tests<=256*budget[2]*budget[1]);if(budget[1]==0)assert(result.clip_contains_tests==0);auto actual=pixels(store);
  for(int py=0;py<16;++py)for(int px=0;px<16;++px){long double l=px,t=py,r=px+1,b=py+1;for(const auto& c:clips){l=std::max(l,(long double)c.l);t=std::max(t,(long double)c.t);r=std::min(r,(long double)c.r);b=std::min(b,(long double)c.b);}long double sum[4]{};
   if(r>l&&b>t)for(int y=0;y<16;++y)for(int x=0;x<16;++x){const long double sx=l+(x+.5L)*(r-l)/16,sy=t+(y+.5L)*(b-t)/16;bool hit=true;for(const auto& c:clips)hit=inside(c,sx,sy)&&hit;if(!hit)continue;long double u=std::clamp((sx-.5L)/13,0.L,1.L);sum[0]+=.75L*u;sum[1]+=.0625L*(1-u)+.375L*u;sum[2]+=.25L*(1-u);sum[3]+=.25L+.5L*u;}
   const long double scale=(r>l&&b>t)?(r-l)*(b-t)/256*.75L:0;for(int c=0;c<4;++c){const int want=std::floor(sum[c]*scale*255+.5L);assert(std::abs(int(actual.pixels[py*actual.stride+px*4+c])-want)<=1);++gradient_checks;}const auto coarse=gradient_coverage(clips,px,py,4096),fine=gradient_coverage(clips,px,py,8192);for(int c=0;c<4;++c){gradient_convergence=std::max(gradient_convergence,std::abs(coarse[c]-fine[c]));gradient_worst=std::max(gradient_worst,std::abs(actual.pixels[py*actual.stride+px*4+c]/255.L-fine[c]));}for(int c=0;c<3;++c)assert(actual.pixels[py*actual.stride+px*4+c]<=actual.pixels[py*actual.stride+px*4+3]);
  }
 }
 assert(gradient_convergence<.1L/255);assert(gradient_worst<=16.L/255);
 // Independent vertical integration for rounded fills and stroke inner cutouts.
 long double worst=0,convergence=0;
 for(const auto& clips:{corpus[1],corpus[2],corpus.back()})for(bool stroke:{false,true}){
  const p::Rect box{1.25f,1.5f,11,10};const float rad=3,width=stroke?1.5f:0;const double half=width/2;
  std::vector<p::Command> c;for(const auto& q:clips)c.push_back(p::PushClip{{float(q.l),float(q.t),float(q.r-q.l),float(q.b-q.t)},radii(q.radius)});
  if(stroke)c.push_back(p::Stroke{box,radii(rad),width,{1,1,1,1}});else c.push_back(p::Fill{box,radii(rad),{1,1,1,1}});for(std::size_t i=0;i<clips.size();++i)c.push_back(p::PopClip{});
  auto f=prepare(std::move(c));assert(s::render(store,f).status==s::Status::ok);auto actual=pixels(store);
  Clip outer{box.x-half,box.y-half,box.x+box.width+half,box.y+box.height+half,rad+half};Clip inner{};if(stroke)inner={box.x+half,box.y+half,box.x+box.width-half,box.y+box.height-half,rad-half};
  for(int y=0;y<16;++y)for(int x=0;x<16;++x){auto a=coverage(clips,outer,inner,x,y,4096),b=coverage(clips,outer,inner,x,y,8192);convergence=std::max(convergence,std::abs(a-b));worst=std::max(worst,std::abs(actual.pixels[y*actual.stride+x*4+3]/255.L-b));}
 }
 assert(convergence<.1L/255);assert(worst<=16.L/255);
 // Depth and bounded work. Repeated paths test the conjunction, not 256 masks.
 for(int depth:{2,8,256})for(double radius:{.25,8.}){
  std::vector<Clip> deep(depth,Clip{0,0,16,16,radius});auto f=prepare(sequence(deep,0,image));auto r=s::render(store,f);assert(r.status==s::Status::ok);assert(r.max_clip_depth==unsigned(depth));assert(r.clip_node_count==unsigned(depth));assert(r.clip_central_tests<=256u*depth);assert(r.clip_contains_tests<=256u*256u*depth);assert(r.pixel_budget<=50000000);const std::uint64_t bound=radius==8?256:std::min(256,4*depth);assert(r.pixel_budget==512+2u*depth*256+256u*(depth+2)*bound);assert(r.clip_corner_pixel_bound==bound);assert(r.clip_node_visits==r.clip_central_tests+r.clip_contains_tests+depth);
 }
 for(int depth:{2,8,256})for(int kind:{0,2}){std::vector<Clip> central(depth,Clip{-4,-4,20,20,1});auto f=prepare(sequence(central,kind,image));auto r=s::render(store,f);assert(r.status==s::Status::ok);assert(r.clip_corner_pixel_bound==0&&r.clip_contains_tests==0);assert(r.clip_central_tests==std::uint64_t(depth)*256);assert(r.clip_node_visits==r.clip_central_tests+depth);assert(r.pixel_budget==256+(kind==2?2048u:1u)*256+2u*depth*256);}
 {auto commands=sequence(std::vector<Clip>(257,Clip{0,0,16,16,4}),0,image);assert(std::get<p::Error>(p::prepare(p::Snapshot{std::move(commands)})).code==p::ErrorCode::LIMIT_EXCEEDED);}
 {auto commands=sequence(std::vector<Clip>(256,Clip{0,0,16,16,4}),0,image);commands.insert(commands.begin(),p::PushTransform{p::identity});commands.push_back(p::PopTransform{});assert(std::get<p::Error>(p::prepare(p::Snapshot{std::move(commands)})).code==p::ErrorCode::LIMIT_EXCEEDED);}
 // Reordering clip conjunction and duplicate alpha clipping must be byte exact.
 auto alpha_sequence=[&](std::vector<Clip> clips){auto out=sequence(clips,0,image);out[clips.size()]=p::Fill{{0,0,16,16},{},{1,.5f,.25f,.5f}};out.insert(out.begin()+clips.size(),p::BeginOpacity{1,0,.5f});out.insert(out.begin()+clips.size()+2,p::EndOpacity{1});return out;};
 auto original=prepare(alpha_sequence(corpus[1]));assert(s::render(store,original).status==s::Status::ok);auto first=pixels(store);auto reversed=corpus[1];std::reverse(reversed.begin(),reversed.end());reversed.push_back(reversed[0]);auto order=prepare(alpha_sequence(reversed));assert(s::render(store,order).status==s::Status::ok);assert(pixels(store).pixels==first.pixels);for(std::size_t i=0;i<first.pixels.size();i+=4)for(int c=0;c<3;++c)assert(first.pixels[i+c]<=first.pixels[i+3]);
 // Restore the parent curved state and then the unclipped state exactly.
 auto both=sequence(corpus[1],0,image);both.pop_back(); // inner pop was already emitted; retain outer scope
 both.push_back(p::Fill{{1,1,2,2},{},{0,1,0,1}});both.push_back(p::PopClip{});both.push_back(p::Fill{{14,14,2,2},{},{1,0,0,1}});
 auto prepared=prepare(std::move(both));assert(s::render(store,prepared).status==s::Status::ok);auto restored=pixels(store);assert(restored.pixels[15*restored.stride+15*4+2]==255&&restored.pixels[15*restored.stride+15*4+3]==255);
 // The same conjunction must be idempotent, not multiplication of masks.
 auto single=prepare(sequence({corpus[0][0]},0,image));assert(s::render(store,single).status==s::Status::ok);auto once=pixels(store);auto repeated=prepare(sequence(corpus[0],0,image));assert(s::render(store,repeated).status==s::Status::ok);assert(pixels(store).pixels==once.pixels);
 // Parent clip remains frozen while nested clips are translated/scrolled.
 for(float dx:{1.25f,-.75f}){
  auto matrix=p::identity;matrix.dx=dx;matrix.dy=.5f;const float tx=dx-.5f,ty=.25f;
  const p::PushClip parent{{1,1,13,13},radii(4)},child{{1.25f,1.75f,10,10},radii(3)};
  const p::Fill content{{0,0,16,16},{},{1,.5f,.25f,.75f}},restored_fill{{1,1,2,2},{},{1,0,0,.5f}},outside{{14,14,2,2},{},{0,1,0,1}};
  auto shifted=prepare({parent,p::PushTransform{matrix},p::PushScroll{1,.5,.25},child,content,p::PopClip{},p::PopScroll{},p::PopTransform{},restored_fill,p::PopClip{},outside});
  auto moved_child=child;moved_child.rect.x+=tx;moved_child.rect.y+=ty;auto moved_content=content;moved_content.rect.x+=tx;moved_content.rect.y+=ty;
  auto absolute=prepare({parent,moved_child,moved_content,p::PopClip{},restored_fill,p::PopClip{},outside});
  assert(s::render(store,shifted).status==s::Status::ok);auto a=pixels(store);assert(s::render(store,absolute).status==s::Status::ok);assert(pixels(store).pixels==a.pixels);
 }
 // Stored clip IDs survive vector growth after their original scope is popped.
 // A left-hand draw is retained while 512 later, disjoint nodes force growth.
 {
  const std::vector<p::Command> first_commands{
   p::PushClip{{.25f,.5f,7,7},radii(3)},
   p::Fill{{0,0,8,8},{},{1,.25f,.5f,.75f}},p::PopClip{}};
  assert(s::render(store,prepare(first_commands)).status==s::Status::ok);
  const auto first_region=pixels(store);
  auto growth=first_commands;
  for(unsigned i=0;i<512;++i){
   const float shift=(i%4)*.125f;
   growth.push_back(p::PushClip{{10+shift,10+shift,4,4},radii(1)});
   growth.push_back(p::Fill{{10,10,5,5},{},{0,1,.25f,1}});
   growth.push_back(p::PopClip{});
  }
  auto result=s::render(store,prepare(std::move(growth)));
  assert(result.status==s::Status::ok);
  assert(result.clip_node_count==513&&result.max_clip_depth==1);
  // Frame clear 256; first draw N=64,C=64. Of 512 later draws,
  // 128 have N=16,C=4 and 384 fractional ones have N=25,C=16.
  // Each fill costs N + 2*d*N + 256*(d+2)*C, with d=1.
  constexpr std::uint64_t expected_growth_budget=
   256+3*64+768*64+128*(3*16+768*4)+384*(3*25+768*16);
  assert(result.pixel_budget==expected_growth_budget);
  const auto grown=pixels(store);
  for(unsigned y=0;y<10;++y)for(unsigned x=0;x<10;++x)
   for(unsigned channel=0;channel<4;++channel)
    assert(grown.pixels[y*grown.stride+x*4+channel]==first_region.pixels[y*first_region.stride+x*4+channel]);
  assert(grown.pixels[12*grown.stride+12*4+1]==255);
 }
 // Existing exclusions remain all-or-nothing under nested clips.
 auto valid_nested=sequence(corpus[1],0,image);auto good=prepare(valid_nested);assert(s::render(store,good).status==s::Status::ok);auto confirmed=pixels(store);
 auto unchanged=[&](){auto f=pixels(store);assert(f.pixels==confirmed.pixels&&f.sequence==confirmed.sequence);};
 p::BoxShadow shadow;shadow.rect={1,1,8,8};shadow.shadow.color={0,0,0,1};
 p::BorderGroup border;border.outer={1,1,8,8};border.inner={2,2,6,6};for(auto& side:border.sides)side={true,AETHER_BORDER_STYLE_SOLID,{1,0,0,1}};
 for(p::Command excluded:{p::Command(shadow),p::Command(border)}){auto c=valid_nested;c[corpus[1].size()]=excluded;assert(s::render(store,prepare(std::move(c))).status==s::Status::unsupported);unchanged();}
 const std::span<const std::shared_ptr<const aether::rendering::text::GlyphMaskA8>> no_masks;
 assert(s::render_a8(store,good,no_masks).status==s::Status::unsupported);unchanged();
 auto uneven=valid_nested;std::get<p::PushClip>(uneven[0]).radii.top_left_y=2;
 {namespace o=elliptic_composition_oracle;const o::Shape a{.25L,.75L,13.25L,14.75L,{{{5,2},{5,5},{5,5},{5,5}}}},b{2.25L,1.25L,14.25L,12.25L,{{{4,4},{4,4},{4,4},{4,4}}}};elliptic_transition::check("nested-former-reject",16,16,uneven,o::render(16,16,{{{0,0,16,16,{}},{a,b},{1,1,1,1}}}));}
 std::get<p::PushClip>(uneven[0]).radii.top_left_y=15;assert(s::render(store,prepare(std::move(uneven))).status==s::Status::unsupported);unchanged();
 // Large corrected gradient budget rejects before publishing any candidate.
 FrameStore large;assert(large.resize({200,200,1})==FrameStatus::ok);auto seed=prepare({p::Fill{{0,0,200,200},{},{1,0,0,1}}});assert(s::render(large,seed).status==s::Status::ok);auto before=pixels(large);
 p::LinearGradientFill gradient;gradient.rect=gradient.gradient_rect={0,0,200,200};gradient.direction_x=1;gradient.stops={{0,{1,0,0,1}},{1,{0,0,1,1}}};auto excessive=prepare({p::PushClip{{0,0,200,200},radii(2)},p::PushClip{{0,0,200,200},radii(2)},gradient,p::PopClip{},p::PopClip{}});assert(s::render(large,excessive).status==s::Status::limit_exceeded);assert(pixels(large).pixels==before.pixels&&pixels(large).sequence==before.sequence);
 auto saved=pixels(store);Cancel count(std::numeric_limits<unsigned>::max());assert(s::render(store,prepared,&count).status==s::Status::ok);saved=pixels(store);
 for(unsigned n=1;n<=count.calls;++n){Cancel cancel(n);assert(s::render(store,prepared,&cancel).status==s::Status::cancelled);auto f=pixels(store);assert(f.pixels==saved.pixels&&f.sequence==saved.sequence);}
 unsigned oom=0;bool succeeded=false;for(int n=0;n<200;++n){fail_after=n;auto result=s::render(store,prepared);fail_after=-1;if(result.status==s::Status::ok){succeeded=true;break;}assert(result.status==s::Status::allocation_failed);auto f=pixels(store);assert(f.pixels==saved.pixels&&f.sequence==saved.sequence);++oom;}assert(succeeded&&oom>=3);
 std::cout<<"N1 nested conjunction frames="<<frames<<" exact_channel_checks="<<checks<<" gradient_channel_checks="<<gradient_checks<<" cancellation_points="<<count.calls<<" stable_id_nodes=513 OOM="<<oom<<" gradient_max_error="<<double(gradient_worst)<<" gradient_convergence="<<double(gradient_convergence)<<" coverage_max_error="<<double(worst)<<" reference_convergence="<<double(convergence)<<" PASS\n";
}
