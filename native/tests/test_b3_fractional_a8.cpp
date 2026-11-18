#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/rendering/software/software_renderer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto* q=std::malloc(n?n:1))return q;throw std::bad_alloc();}
void operator delete(void* q)noexcept{std::free(q);}void operator delete(void* q,std::size_t)noexcept{std::free(q);}
static unsigned checks=0;
#define CK(x) do{++checks;assert(x);}while(0)
using Mask=std::shared_ptr<const text::GlyphMaskA8>;
using Font=std::shared_ptr<const text::FontInstance>;
Font font(){return std::make_shared<const text::FontInstance>(std::vector<uint8_t>{1},"SYNTHETIC_NOT_A_FONT",0,640,0);}
Mask mask(Font f,unsigned value,unsigned width=1,unsigned height=1){return std::make_shared<const text::GlyphMaskA8>(f,1,0,0,0,width,height,width,0,0,std::vector<uint8_t>(size_t(width)*height,value));}
p::Text textcmd(Font f,float x=1,float y=1){p::Text t{};t.text="x";t.rect={x,y,1,10};t.font_family="synthetic";t.font_size=10;t.font_weight=400;t.color={.8f,.4f,.2f,.5f};t.native=p::NativeText{std::make_shared<const text::ShapedRun>(f,"x",std::vector<text::Glyph>{{1,0,0,0,0,0}},aether_text_metrics_t{1,10,0}),y};return t;}
p::PreparedFrame prep(std::vector<p::Command> commands){auto result=p::prepare(p::Snapshot{std::move(commands),true});CK(std::holds_alternative<p::PreparedFrame>(result));return std::get<p::PreparedFrame>(std::move(result));}
Frame read(FrameStore& store){std::optional<Frame> f;CK(store.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
unsigned mul(unsigned a,unsigned b){return (a*b+127)/255;}
struct Cancel:p::Cancellation{unsigned at,count=0;explicit Cancel(unsigned n):at(n){}bool cancelled()noexcept override{return ++count==at;}};
static void same_frame(const Frame& a,const Frame& b){CK(a.sequence==b.sequence&&a.viewport.width==b.viewport.width&&a.viewport.height==b.viewport.height&&a.viewport.dpi==b.viewport.dpi&&a.stride==b.stride&&a.pixels==b.pixels);}
static void pixel(const Frame& frame,unsigned x,unsigned y,unsigned alpha,unsigned coverage,bool opaque){unsigned sa=mul(alpha,coverage);unsigned channels[]{51,102,204},dst[]{77,51,26};auto at=size_t(y)*frame.stride+x*4;for(unsigned c=0;c<3;++c)CK(frame.pixels[at+c]==mul(channels[c],sa)+mul(opaque?dst[c]:0,255-sa));CK(frame.pixels[at+3]==sa+mul(opaque?255:0,255-sa));}
int main(){
 auto f=font();auto t=textcmd(f);FrameStore store;CK(store.resize({4,4,1})==FrameStatus::ok);
 // Independent integer-area oracle: a quarter-grid area is dx*dy/16.
 // Includes horizontal, vertical, corner and empty intersections, not filtered_a8.
 unsigned coverage_cases=0;
 for(unsigned value:{0u,1u,127u,128u,254u,255u})for(unsigned alpha:{0u,1u,127u,128u,254u,255u})for(bool opaque:{false,true})for(auto extent:{std::pair{0,4},std::pair{1,4},std::pair{2,4},std::pair{3,4},std::pair{4,4},std::pair{4,1},std::pair{4,2},std::pair{4,3},std::pair{2,2}}){
  auto [dx,dy]=extent;t.color.a=alpha/255.f;std::vector<p::Command> cmds;if(opaque)cmds.push_back(p::Fill{{0,0,4,4},{},{.1f,.2f,.3f,1}});cmds.push_back(p::PushClip{{1,1,dx/4.f,dy/4.f},{}});cmds.push_back(t);cmds.push_back(p::PopClip{});auto pf=prep(std::move(cmds));std::vector<Mask> masks{mask(f,value)};auto r=s::render_a8_b3(store,pf,masks);CK(r.status==s::Status::ok);const unsigned expected=(value*unsigned(dx*dy)+8)/16;pixel(read(store),1,1,alpha,expected,opaque);++coverage_cases;
 }
 // Nested clipping intersects geometry first; duplicate quarter clips do not square alpha.
 t=textcmd(f);t.color.a=1;std::vector<Mask> masks{mask(f,255)};
 auto nested=prep({p::PushClip{{1.25f,1,0.75f,1},{}},p::PushClip{{1,1.25f,1,.75f},{}},p::PushClip{{1,1.25f,1,.75f},{}},t,p::PopClip{},p::PopClip{},p::PopClip{}});
 CK(s::render_a8_b3(store,nested,masks).status==s::Status::ok);pixel(read(store),1,1,255,(255*9+8)/16,false);
 // Pop restores fractional parent, then outer viewport. Use distinct scanlines.
 auto t2=textcmd(f,1,2);t2.color.a=1;auto t3=textcmd(f,1,3);t3.color.a=1;
 auto restore=prep({p::PushClip{{1.5f,0,0.5f,4},{}},p::PushClip{{1,1.5f,1,.5f},{}},t,p::PopClip{},t2,p::PopClip{},t3});
 CK(s::render_a8_b3(store,restore,masks).status==s::Status::ok);auto restored=read(store);pixel(restored,1,1,255,64,false);pixel(restored,1,2,255,128,false);pixel(restored,1,3,255,255,false);
 for(bool outer_fractional:{false,true}){const p::Rect a=outer_fractional?p::Rect{.25f,.25f,3.5f,3.5f}:p::Rect{0,0,4,4};const p::Rect b=outer_fractional?p::Rect{1,1,1,1}:p::Rect{1.5f,1,0.5f,1};auto mixed=prep({p::PushClip{a,{}},p::PushClip{b,{}},t,p::PopClip{},p::PopClip{}});auto r=s::render_a8_b3(store,mixed,masks);CK(r.status==s::Status::ok&&r.pixel_budget==16+(outer_fractional?1:9));pixel(read(store),1,1,255,outer_fractional?255:128,false);}
 // Clip area is not work area: huge clip with one glyph visits one pixel.
 auto huge_clip=prep({p::PushClip{{-100.25f,-100.25f,1000.5f,1000.5f},{}},t,p::PopClip{}});auto hr=s::render_a8_b3(store,huge_clip,masks);CK(hr.status==s::Status::ok&&hr.mask_pixel_visits==1&&hr.pixel_budget==17);
 // Negative/offscreen clip and glyph intersection at viewport boundary.
 auto edge=textcmd(f,0,0);edge.color.a=1;auto negative=prep({p::PushClip{{-.25f,-.75f,1,1},{}},edge,p::PopClip{}});auto nr=s::render_a8_b3(store,negative,masks);CK(nr.status==s::Status::ok&&nr.mask_pixel_visits==1&&nr.pixel_budget==25);pixel(read(store),0,0,255,(255*3+8)/16,false);
 auto offscreen=textcmd(f,-1,0);auto empty=prep({p::PushClip{{-.75f,0,.5f,1},{}},offscreen,p::PopClip{}});auto er=s::render_a8_b3(store,empty,masks);CK(er.status==s::Status::ok&&er.mask_pixel_visits==0&&er.pixel_budget==16);for(auto value:read(store).pixels)CK(value==0);
 // Integral policy equivalence and retained legacy rejection.
 auto integral=prep({p::PushClip{{1,1,1,1},{}},t,p::PopClip{}});FrameStore old;CK(old.resize({4,4,1})==FrameStatus::ok);auto old_result=s::render_a8(old,integral,masks);auto new_result=s::render_a8_b3(store,integral,masks);CK(old_result.status==s::Status::ok&&new_result.status==s::Status::ok&&old_result.pixel_budget==new_result.pixel_budget&&read(old).pixels==read(store).pixels);
 auto before=read(old);CK(s::render_a8(old,nested,masks).status==s::Status::unsupported);same_frame(before,read(old));
 const auto rejected=[&](const p::PreparedFrame& pf,const std::vector<Mask>& ms,s::Status expected){auto before=read(store);CK(s::render_a8_b3(store,pf,ms).status==expected);same_frame(before,read(store));};
 rejected(prep({p::PushClip{{0,0,4,4},{1,1,1,1,1,1,1,1}},t,p::PopClip{}}),masks,s::Status::unsupported);
 rejected(integral,{},s::Status::missing_mask);rejected(integral,{nullptr},s::Status::invalid_mask);rejected(integral,{masks[0],masks[0]},s::Status::invalid_mask);rejected(prep({textcmd(f,.1f,1)}),masks,s::Status::unsupported);rejected(prep({p::PushTransform{{2,0,0,1,0,0}},t,p::PopTransform{}}),masks,s::Status::unsupported);
 for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-1.f}){p::Snapshot snap{{p::PushClip{{0,0,bad,1},{}},t,p::PopClip{}},true};auto r=p::prepare(snap);CK(!std::holds_alternative<p::PreparedFrame>(r));}
 // Every OOM/cancellation position, with and without an existing confirmed frame.
 unsigned oom_total=0,cancel_total=0;
 for(bool confirmed:{false,true}){
  FrameStore target;CK(target.resize({4,4,1})==FrameStatus::ok);if(confirmed)CK(s::render_a8_b3(target,integral,masks).status==s::Status::ok);std::optional<Frame> prior;CK(target.copy_confirmed(prior)==FrameStatus::ok);bool success=false;
  for(int at=0;at<256;++at){fail_after=at;auto r=s::render_a8_b3(target,nested,masks);fail_after=-1;if(r.status==s::Status::ok){success=true;break;}CK(r.status==s::Status::allocation_failed);++oom_total;std::optional<Frame> actual;CK(target.copy_confirmed(actual)==FrameStatus::ok&&bool(actual)==bool(prior));if(prior)same_frame(*prior,*actual);}CK(success);
  FrameStore cancellation;CK(cancellation.resize({4,4,1})==FrameStatus::ok);if(confirmed)CK(s::render_a8_b3(cancellation,integral,masks).status==s::Status::ok);prior.reset();CK(cancellation.copy_confirmed(prior)==FrameStatus::ok);
  Cancel probe(UINT32_MAX);FrameStore counting;CK(counting.resize({4,4,1})==FrameStatus::ok);CK(s::render_a8_b3(counting,nested,masks,&probe).status==s::Status::ok);
  for(unsigned at=1;at<=probe.count;++at){Cancel c(at);CK(s::render_a8_b3(cancellation,nested,masks,&c).status==s::Status::cancelled);++cancel_total;std::optional<Frame> actual;CK(cancellation.copy_confirmed(actual)==FrameStatus::ok&&bool(actual)==bool(prior));if(prior)same_frame(*prior,*actual);}
 }
 // Owner thread and failed copy preserve caller's owning destination.
 before=read(store);std::optional<Frame> destination=before;FrameStatus wrong{};std::thread thread([&]{wrong=store.copy_confirmed(destination);});thread.join();CK(wrong==FrameStatus::wrong_thread);same_frame(before,*destination);fail_after=0;auto copy_status=store.copy_confirmed(destination);fail_after=-1;CK(copy_status==FrameStatus::allocation_failed);same_frame(before,*destination);
 s::Result wrong_render;std::thread renderer([&]{wrong_render=s::render_a8_b3(store,nested,masks);});renderer.join();CK(wrong_render.status==s::Status::frame_failed&&wrong_render.frame_status==FrameStatus::wrong_thread);same_frame(before,read(store));
 // Logical work, one below/on/above 50,000,000; no frame allocation on excess.
 unsigned allocations=0;FrameStore work([&]{++allocations;});CK(work.resize({1000,1000,1})==FrameStatus::ok);auto big=mask(f,255,1000,1000);std::vector<Mask> large{big};auto draw=textcmd(f,0,0);draw.color.a=1;
 const auto budget_frame=[&](int tail){std::vector<p::Command> commands;commands.push_back(p::PushClip{{.25f,.25f,999.5f,999.5f},{}});for(int i=0;i<5;++i)commands.push_back(draw);commands.push_back(p::PopClip{});for(int i=0;i<3;++i)commands.push_back(draw);commands.push_back(p::PushClip{{0,0,1000,999},{}});commands.push_back(draw);commands.push_back(p::PopClip{});commands.push_back(p::PushClip{{0,999,999,1},{}});commands.push_back(draw);commands.push_back(p::PopClip{});for(int i=0;i<tail;++i){commands.push_back(p::PushClip{{999,999,1,1},{}});commands.push_back(draw);commands.push_back(p::PopClip{});}return prep(std::move(commands));};
 for(int tail=0;tail<2;++tail){auto r=s::render_a8_b3(work,budget_frame(tail),large);CK(r.status==s::Status::ok&&r.pixel_budget==49999999u+tail&&r.mask_pixel_visits==8999999u+tail);}
 auto confirmed=read(work);auto allocated=allocations;auto too_much=s::render_a8_b3(work,budget_frame(2),large);CK(too_much.status==s::Status::limit_exceeded&&allocations==allocated);same_frame(confirmed,read(work));
 auto repeat=s::render_a8_b3(store,nested,masks);CK(repeat.status==s::Status::ok);auto first=read(store);CK(s::render_a8_b3(store,nested,masks).status==s::Status::ok);auto second=read(store);CK(second.sequence==first.sequence+1&&first.pixels==second.pixels&&second.stride==16);for(size_t at=0;at<second.pixels.size();at+=4)for(size_t c=0;c<3;++c)CK(second.pixels[at+c]<=second.pixels[at+3]);
 std::printf("B3 existing fractional A8 wrapper PASS checks=%u analytical_cases=%u OOM_points=%u cancel_points=%u exact_work=49999999/50000000/50000001\n",checks,coverage_cases,oom_total,cancel_total);
}
