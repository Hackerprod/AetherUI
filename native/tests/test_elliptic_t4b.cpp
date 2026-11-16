#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <string>
using namespace aether::rendering;namespace p=aether::rendering::preparation;namespace sw=aether::rendering::software;
// Test-executable-only interposition, as in test_software_nested_clip. No product hook added.
static bool armed=false;static unsigned allocations=0,fail_at=0;static std::array<std::size_t,256> sizes{};
void* operator new(std::size_t n){if(armed){assert(allocations<sizes.size());sizes[allocations++]=n;if(allocations==fail_at)throw std::bad_alloc();}if(auto q=std::malloc(n?n:1))return q;throw std::bad_alloc();}
void operator delete(void* q)noexcept{std::free(q);}void operator delete(void* q,std::size_t)noexcept{std::free(q);}
static std::string output;
static Frame read(FrameStore& s){std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
static void seed(FrameStore& s){assert(s.resize({16,12,1})==FrameStatus::ok);FrameStore::Token t;assert(s.prepare(t)==FrameStatus::ok);std::span<std::uint8_t>b;assert(s.candidate(t,b)==FrameStatus::ok);for(std::size_t i=0;i<b.size();i+=4){b[i]=(i/4)%127;b[i+1]=17;b[i+2]=53;b[i+3]=255;}assert(s.commit(t)==FrameStatus::ok);}
static void save(const std::string& n,const Frame& f){std::ofstream s(output+"/"+n+".bgra",std::ios::binary);assert(s);s.write(reinterpret_cast<const char*>(f.pixels.data()),f.pixels.size());s.close();assert(s);}
static void same(const Frame&a,const Frame&b){assert(a.sequence==b.sequence&&a.viewport.width==b.viewport.width&&a.viewport.height==b.viewport.height&&a.viewport.dpi==b.viewport.dpi&&a.stride==b.stride&&a.pixels==b.pixels);}
static p::PreparedFrame prep(std::vector<p::Command> c){auto r=p::prepare({std::move(c),true});assert(std::holds_alternative<p::PreparedFrame>(r));return std::get<p::PreparedFrame>(std::move(r));}
struct Cancel:p::Cancellation{unsigned calls=0,at=0,before_candidate=0,after_candidate=0;bool* candidate;explicit Cancel(bool* c,unsigned n=0):at(n),candidate(c){}bool cancelled()noexcept override{++calls;if(*candidate)++after_candidate;else ++before_candidate;assert(calls<=4096);return calls==at;}};
using Mask=std::shared_ptr<const text::GlyphMaskA8>;
static unsigned failures=0,recoveries=0;
static void exercise(const std::string& name,const p::PreparedFrame& f,const std::vector<Mask>* masks=nullptr){
 bool candidate=false,throw_candidate=false;unsigned candidate_calls=0,candidate_ordinal=0;
 FrameStore store([&]{if(armed){candidate=true;++candidate_calls;candidate_ordinal=allocations+1;if(throw_candidate)throw std::bad_alloc();}});
 auto render=[&](p::Cancellation* c=nullptr){return masks?sw::render_a8(store,f,*masks,c):sw::render(store,f,c);};
 seed(store);Cancel count(&candidate);allocations=0;armed=true;auto baseline=render(&count);armed=false;
 assert(baseline.status==sw::Status::ok&&candidate_calls==1&&count.before_candidate>0&&count.after_candidate>12);
 const auto total=allocations,ordinal=candidate_ordinal;const auto trace=sizes;assert(total>ordinal&&ordinal>1&&total<256);auto golden=read(store);assert(std::any_of(golden.pixels.begin(),golden.pixels.end(),[](auto b){return b!=0;}));save(name+"-golden",golden);
 std::printf("BASE %s allocations=%u candidate_ordinal=%u polls=%u pre=%u post=%u\n",name.c_str(),total,ordinal,count.calls,count.before_candidate,count.after_candidate);for(unsigned i=0;i<total;++i)std::printf("ALLOC %s ordinal=%u bytes=%zu phase=%s\n",name.c_str(),i+1,trace[i],i+1<ordinal?"preflight":i+1==ordinal?"candidate":"post-candidate");std::fflush(stdout);
 auto verify=[&](const std::string& label,const Frame& before,sw::Result r,sw::Status expected){assert(!armed&&fail_at==0&&!throw_candidate);auto after=read(store);save(label+"-before",before);save(label+"-after",after);std::printf("FAIL %s status=%d frame_status=%d candidate_calls=%u allocations=%u seq=%llu/%llu hooks_disabled=true\n",label.c_str(),int(r.status),int(r.frame_status),candidate_calls,allocations,(unsigned long long)before.sequence,(unsigned long long)after.sequence);std::fflush(stdout);assert(r.status==expected);same(before,after);++failures;auto recovery=render();assert(recovery.status==sw::Status::ok);auto recovered=read(store);assert(recovered.sequence==before.sequence+1&&recovered.viewport.width==16&&recovered.viewport.height==12&&recovered.viewport.dpi==1&&recovered.stride==64&&recovered.pixels==golden.pixels);++recoveries;};
 // Every ordinal observed in this small fixture, bounded; not process-wide/exhaustive OOM coverage.
 for(unsigned n=1;n<=total;++n){seed(store);auto before=read(store);candidate=false;candidate_calls=0;allocations=0;fail_at=n;armed=true;auto r=render();armed=false;fail_at=0;assert(allocations==n);assert(candidate_calls==(n>=ordinal?1u:0u));assert(r.frame_status==(n==ordinal?FrameStatus::allocation_failed:FrameStatus::ok));verify(name+"-oom-"+std::to_string(n),before,r,sw::Status::allocation_failed);}
 seed(store);auto before=read(store);candidate=false;candidate_calls=0;allocations=0;throw_candidate=true;armed=true;auto r=render();armed=false;throw_candidate=false;assert(candidate_calls==1&&allocations==ordinal-1&&r.frame_status==FrameStatus::allocation_failed);verify(name+"-hook-oom",before,r,sw::Status::allocation_failed);
 for(unsigned n=1;n<=count.calls;++n){seed(store);before=read(store);candidate=false;candidate_calls=0;allocations=0;Cancel cancel(&candidate,n);armed=true;r=render(&cancel);armed=false;assert(cancel.calls==n&&r.frame_status==FrameStatus::ok);assert(candidate_calls==(n>count.before_candidate?1u:0u));verify(name+"-cancel-"+std::to_string(n),before,r,sw::Status::cancelled);}
 std::printf("CASE %s completed cancellation_polls=%u OOM_ordinals=%u candidate_hook=1 hooks_disabled=true\n",name.c_str(),count.calls,total);std::fflush(stdout);
}
int main(int argc,char**argv){assert(argc==2);output=argv[1];const p::Radii radii{6,3,2,5,4,6,3,4};const p::PushClip clip{{0,0,16,12},radii};
 auto wrap=[&](p::Command draw){return prep({clip,clip,p::BeginOpacity{1,0,.5f},p::BeginOpacity{2,1,.5f},std::move(draw),p::EndOpacity{2},p::EndOpacity{1},p::PopClip{},p::PopClip{}});};
 exercise("solid",wrap(p::Fill{{0,0,16,12},radii,{1,.5f,.25f,.75f}}));
 p::LinearGradientFill gradient;gradient.rect=gradient.gradient_rect={0,0,16,12};gradient.radii=radii;gradient.direction_x=1;gradient.stops={{0,{1,0,0,.5f}},{1,{0,0,1,1}}};exercise("gradient",wrap(gradient));
 auto image=std::get<p::ImageRef>(p::ImageResource::copy(2,2,std::vector<std::uint8_t>{0,0,255,255,0,255,0,255,255,0,0,255,255,255,255,255}));exercise("image",wrap(p::Image{image,{0,0,2,2},{0,0,16,12},1,p::ImageSamplingBounds::StrictCrop}));
 auto font=std::make_shared<const text::FontInstance>(std::vector<std::uint8_t>{1},"SYNTHETIC-T4B",0,640,0);std::vector<Mask> masks{std::make_shared<const text::GlyphMaskA8>(font,1,0,0,0,8,8,10,-1,1,std::vector<std::uint8_t>(80,180))};p::Text t{};t.text="x";t.rect={1,1,8,10};t.font_family="synthetic";t.font_size=10;t.font_weight=400;t.color={.75f,.5f,.25f,.5f};t.native=p::NativeText{std::make_shared<const text::ShapedRun>(font,"x",std::vector<text::Glyph>{{1,0,0,0,0,0}},aether_text_metrics_t{8,10,0}),1};exercise("a8",wrap(t),&masks);
 assert(!armed&&fail_at==0&&failures==recoveries&&failures>100);std::printf("ELLIPTIC_T4B_PASS fixtures=4 failures=%u recoveries=%u hooks_disabled=true coverage_complete=false\n",failures,recoveries);
}
