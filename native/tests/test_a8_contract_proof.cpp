#include "elliptic_t5_transition.h"
#ifdef NDEBUG
#error Assertions required
#endif
#include "a8_contract_oracle.h"
#include "../src/rendering/software/software_renderer.h"
#include "../../tests/software_render_smoke/png_writer.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <new>
#include <limits>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace sw=aether::rendering::software;
using Mask=std::shared_ptr<const text::GlyphMaskA8>;using Font=std::shared_ptr<const text::FontInstance>;
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto q=std::malloc(n?n:1))return q;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static bool testing=false,throw_hook=false,candidate_seen=false;
static void hook(){if(testing){candidate_seen=true;if(throw_hook)throw std::bad_alloc();}}
static Frame read(FrameStore& s){assert(!testing&&!throw_hook&&fail_after==-1);std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
static void seed(FrameStore& s,int w=16,int h=12){assert(!testing&&!throw_hook&&fail_after==-1);assert(s.resize({w,h,1})==FrameStatus::ok);FrameStore::Token t;assert(s.prepare(t)==FrameStatus::ok);std::span<std::uint8_t> bytes;assert(s.candidate(t,bytes)==FrameStatus::ok);for(std::size_t i=0;i<bytes.size();i+=4){bytes[i]=std::uint8_t((i/4)%127);bytes[i+1]=17;bytes[i+2]=53;bytes[i+3]=255;}assert(s.commit(t)==FrameStatus::ok);}
static void same(const Frame& a,const Frame& b){assert(a.viewport.width==b.viewport.width&&a.viewport.height==b.viewport.height&&a.viewport.dpi==b.viewport.dpi&&a.stride==b.stride&&a.sequence==b.sequence&&a.pixels==b.pixels);}
static void published(const Frame& before,const Frame& after,const oracle::Bytes& expected){assert(after.sequence==before.sequence+1);assert(after.viewport.width==before.viewport.width&&after.viewport.height==before.viewport.height&&after.viewport.dpi==before.viewport.dpi&&after.stride==before.stride);assert(after.pixels==expected);}
static p::PreparedFrame prep(std::vector<p::Command> c){auto r=p::prepare({std::move(c),true});assert(std::holds_alternative<p::PreparedFrame>(r));return std::get<p::PreparedFrame>(std::move(r));}
static Font make_font(){return std::make_shared<const text::FontInstance>(std::vector<std::uint8_t>{1},"SYNTHETIC-NOT-FONT",0,640,0);}
static p::Text textcmd(Font f,float x=1,float y=1,p::Color color={.8f,.4f,.2f,.5f}){p::Text t{};t.text="x";t.rect={x,y,8,10};t.font_family="synthetic";t.font_size=10;t.font_weight=400;t.color=color;t.native=p::NativeText{std::make_shared<const text::ShapedRun>(f,"x",std::vector<text::Glyph>{{1,0,0,0,0,0}},aether_text_metrics_t{8,10,0}),y};return t;}
static p::PushClip clip(float x,float y,float w,float h,float radius){return {{x,y,w,h},{radius,radius,radius,radius,radius,radius,radius,radius}};}
static oracle::Bytes pattern(){oracle::Bytes b(80,99);const unsigned v[]{0,1,127,128,254,255};for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)b[y*10+x]=std::uint8_t(v[(x+2*y)%6]);return b;}
static Mask make_mask(Font f){return std::make_shared<const text::GlyphMaskA8>(f,1,0,0,0,8,8,10,-1,1,pattern());}
static oracle::Op expected(int x,int y,std::vector<oracle::Clip> clips,p::Color c={.8f,.4f,.2f,.5f}){return {0,{x,y,8,8,10,pattern(),std::move(clips),{c.b,c.g,c.r,c.a}},1};}
static void binary(const std::string& path,const oracle::Bytes& b){std::ofstream f(path,std::ios::binary);assert(f);f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();assert(f);}
static void evidence(const std::string& dir,const std::string& name,const Frame& actual,const oracle::Bytes& want){
 binary(dir+"/"+name+".actual.bgra",actual.pixels);binary(dir+"/"+name+".expected.bgra",want);
 std::ofstream diff(dir+"/"+name+".diff.csv");diff<<"offset,expected,actual\n";for(std::size_t i=0;i<want.size();++i)if(want[i]!=actual.pixels[i])diff<<i<<','<<unsigned(want[i])<<','<<unsigned(actual.pixels[i])<<'\n';diff.close();assert(diff);
 auto png=smoke_png::png(smoke_png::rgba(actual),actual.viewport.width,actual.viewport.height);binary(dir+"/"+name+"-SYNTHETIC-NOT-HTML.png",png);
}
static void rectangle_anchors(const oracle::Bytes& bytes){
 assert(bytes.size()==16*12*4);
 const auto pixel=[&](int x,int y,std::array<unsigned,4> wanted){const std::size_t i=(std::size_t(y)*16+x)*4;for(int c=0;c<4;++c)assert(bytes[i+c]==wanted[c]);};
 pixel(1,0,{0,0,1,1});pixel(2,0,{13,26,51,64});pixel(7,7,{13,26,51,64});
 for(std::size_t i=8*16*4;i<9*16*4;++i)assert(bytes[i]==0);
}
static void shifted_anchors(const oracle::Bytes& bytes,const std::string& name){
 if(name!="scroll"&&name!="translate"&&name!="manual-shift"&&name!="frozen-clip")return;
 assert(bytes.size()==16*12*4);
 const std::array<unsigned,4> pixel=name=="frozen-clip"?std::array<unsigned,4>{1,1,2,3}:std::array<unsigned,4>{25,51,102,127};
 for(int c=0;c<4;++c)assert(bytes[c]==pixel[c]);
 for(int y=0;y<12;++y)for(int x=0;x<16;++x)if(x>=6||y>=7)for(int c=0;c<4;++c)assert(bytes[(std::size_t(y)*16+x)*4+c]==0);
}
struct Fixture{std::string name;std::vector<p::Command> cmds;std::vector<oracle::Op> ops;};
static void rejection(const char* label,const p::PreparedFrame& pf,const std::vector<Mask>& masks,sw::Status wanted,int w=16,int h=12){
 FrameStore store(hook);seed(store,w,h);auto before=read(store);candidate_seen=false;testing=true;auto r=sw::render_a8(store,pf,masks);testing=false;auto after=read(store);std::printf("NEG %s expected=%d actual=%d candidate=%d\n",label,int(wanted),int(r.status),candidate_seen);std::fflush(stdout);same(before,after);assert(r.status==wanted);
}
struct Cancel:p::Cancellation{int stop,count=0,pre=0,post=0;explicit Cancel(int n):stop(n){}bool cancelled()noexcept override{++count;if(candidate_seen)++post;else ++pre;return count>=stop;}};
int main(int argc,char** argv){
 assert(argc==2);const std::string out=argv[1];oracle::self_checks();auto f=make_font();auto mask=make_mask(f);std::vector<Mask> masks{mask};auto t=textcmd(f);
 assert(mask->bearing_left==-1&&mask->bearing_top==1&&mask->width==8&&mask->height==8&&mask->stride==10);
 assert(t.rect.x==1&&t.native&&t.native->baseline==1);
 assert(t.rect.x+mask->bearing_left==0&&t.native->baseline-mask->bearing_top==0);
 const auto C=clip(0,0,8,8,3),D=clip(2,0,8,8,3),R=clip(1,0,6,8,0);const oracle::Clip c{0,0,8,8,3},d{2,0,10,8,3},r{1,0,7,8,0};
 std::vector<Fixture> fixtures;
 fixtures.push_back({"rectangle",{clip(0,0,8,8,0),t,p::PopClip{}},{expected(0,0,{{0,0,8,8,0}})}});
 fixtures.push_back({"circle",{C,t,p::PopClip{}},{expected(0,0,{c})}});
 fixtures.push_back({"duplicate",{C,C,t,p::PopClip{},p::PopClip{}},{expected(0,0,{c,c})}});
 fixtures.push_back({"distinct",{C,D,t,p::PopClip{},p::PopClip{}},{expected(0,0,{c,d})}});
 fixtures.push_back({"nested-pop",{C,R,D,t,p::PopClip{},t,p::PopClip{},t,p::PopClip{},t},{expected(0,0,{c,r,d}),expected(0,0,{c,r}),expected(0,0,{c}),expected(0,0,{})}});
 fixtures.push_back({"overlap",{C,t,t,p::PopClip{}},{expected(0,0,{c}),expected(0,0,{c})}});
 fixtures.push_back({"group",{C,p::BeginOpacity{1,0,.5f},t,t,p::EndOpacity{1},p::PopClip{}},{{1,{},.5L},expected(0,0,{c}),expected(0,0,{c}),{2,{},1}}});
 fixtures.push_back({"scroll",{p::PushScroll{1,2,1},C,t,p::PopClip{},p::PopScroll{}},{expected(-2,-1,{{-2,-1,6,7,3}})}});
 fixtures.push_back({"translate",{p::PushTransform{{1,0,0,1,-2,-1}},C,t,p::PopClip{},p::PopTransform{}},{expected(-2,-1,{{-2,-1,6,7,3}})}});
 fixtures.push_back({"manual-shift",{clip(-2,-1,8,8,3),textcmd(f,-1,0),p::PopClip{}},{expected(-2,-1,{{-2,-1,6,7,3}})}});
 fixtures.push_back({"frozen-clip",{C,p::PushScroll{1,2,1},t,p::PopScroll{},p::PopClip{}},{expected(-2,-1,{c})}});
 fixtures.push_back({"offscreen",{clip(100,100,4,4,1),t,p::PopClip{}},{expected(0,0,{{100,100,104,104,1}})}});
 std::vector<oracle::Bytes> actuals;
 for(const auto& fx:fixtures){FrameStore store(hook);seed(store);auto before=read(store);auto pf=prep(fx.cmds);auto want=oracle::render(16,12,fx.ops);if(fx.name=="rectangle")rectangle_anchors(want);shifted_anchors(want,fx.name);testing=true;auto result=sw::render_a8(store,pf,masks);testing=false;assert(result.status==sw::Status::ok);auto actual=read(store);evidence(out,fx.name,actual,want);if(fx.name=="rectangle")rectangle_anchors(actual.pixels);shifted_anchors(actual.pixels,fx.name);published(before,actual,want);actuals.push_back(actual.pixels);std::printf("BYTES %s count=%zu PASS\n",fx.name.c_str(),want.size());std::fflush(stdout);}
 assert(actuals[1]==actuals[2]);assert(actuals[7]==actuals[8]&&actuals[8]==actuals[9]);
 p::Color half{.8f,.4f,.2f,.25f};auto wrong_group=oracle::render(16,12,{expected(0,0,{c},half),expected(0,0,{c},half)});assert(wrong_group!=actuals[6]);
 // Replay EXACT original offscreen geometry/text/mask (not the 8x8 visible mask).
 auto original_t=textcmd(f);original_t.rect={1,1,6,10};original_t.native=p::NativeText{std::make_shared<const text::ShapedRun>(f,"x",std::vector<text::Glyph>{{1,0,0,0,0,0}},aether_text_metrics_t{6,10,0}),1};
 auto original_mask=std::make_shared<const text::GlyphMaskA8>(f,1,0,0,0,6,1,6,0,0,std::vector<std::uint8_t>{0,1,127,128,254,255});
 auto invisible=prep({clip(100,100,4,4,1),original_t,p::PopClip{}});{FrameStore store(hook);seed(store);auto before=read(store);assert(sw::render_a8(store,invisible,std::vector<Mask>{original_mask}).status==sw::Status::ok);published(before,read(store),oracle::Bytes(16*12*4,0));}
 auto plain=prep({t});rejection("missing",plain,{},sw::Status::missing_mask);rejection("other-font",plain,{make_mask(make_font())},sw::Status::missing_mask);rejection("duplicate",plain,{mask,mask},sw::Status::invalid_mask);rejection("null",plain,{nullptr},sw::Status::invalid_mask);
 auto phase_t=textcmd(f,1.5f,1);rejection("missing-phase",prep({phase_t}),masks,sw::Status::missing_mask);
 rejection("missing-offscreen",invisible,{},sw::Status::missing_mask);
 rejection("fractional-visible",prep({clip(.5f,0,8,8,3),t,p::PopClip{}}),masks,sw::Status::unsupported);
 rejection("fractional-offscreen",prep({clip(100.5f,100,4,4,1),t,p::PopClip{}}),masks,sw::Status::unsupported);
 auto uneven=C;uneven.radii.top_left_y=2;
 {namespace o=elliptic_paint_oracle;const o::Shape e{0,0,8,8,{{{3,2},{3,3},{3,3},{3,3}}}};elliptic_transition::check("a8-former-reject",16,12,{uneven,t,p::PopClip{}},o::mask(16,12,0,0,pattern(),{e},{t.color.b,t.color.g,t.color.r,t.color.a}),&masks);}
 uneven.radii.top_left_y=9;rejection("side-sum-exceeded",prep({uneven,t,p::PopClip{}}),masks,sw::Status::unsupported);
 rejection("scale",prep({p::PushTransform{{2,0,0,1,1,0}},t,p::PopTransform{}}),masks,sw::Status::unsupported);
 rejection("offgrid",prep({textcmd(f,.1f,1)}),masks,sw::Status::unsupported);
 // Exact global cap 64 MiB mask + nonzero font/surface/metadata exceeds combined bound.
 {auto huge=std::make_shared<const text::GlyphMaskA8>(f,9,0,0,0,8192,8192,8192,0,0,oracle::Bytes(64u*1024u*1024u));rejection("global-mask-cap",plain,{huge},sw::Status::limit_exceeded);}
 // 64 curves, 64x64 mask: corner bound saturates 4096; 4096*256*(64+2)=69,206,016 > 50M.
 {auto big=std::make_shared<const text::GlyphMaskA8>(f,1,0,0,0,64,64,64,0,0,oracle::Bytes(4096,255));std::vector<p::Command> cmds;for(int i=0;i<64;++i)cmds.push_back(clip(0,0,64,64,32));cmds.push_back(textcmd(f,0,0));for(int i=0;i<64;++i)cmds.push_back(p::PopClip{});rejection("clip-work",prep(std::move(cmds)),{big},sw::Status::limit_exceeded,64,64);}
 auto visible=prep(fixtures[6].cmds);
 {FrameStore store(hook);seed(store);auto before=read(store);candidate_seen=false;testing=true;throw_hook=true;auto result=sw::render_a8(store,visible,masks);testing=false;throw_hook=false;assert(candidate_seen);same(before,read(store));assert(result.status==sw::Status::allocation_failed&&result.frame_status==FrameStatus::allocation_failed);std::printf("HOOK frame allocation preserved\n");}
 bool success=false;unsigned failures=0;
 for(int n=0;n<200;++n){FrameStore store(hook);seed(store);auto before=read(store);candidate_seen=false;testing=true;fail_after=n;auto result=sw::render_a8(store,visible,masks);fail_after=-1;testing=false;auto after=read(store);std::printf("OOM n=%d status=%d candidate=%d\n",n,int(result.status),candidate_seen);std::fflush(stdout);if(result.status==sw::Status::ok){published(before,after,oracle::render(16,12,fixtures[6].ops));success=true;break;}assert(result.status==sw::Status::allocation_failed);same(before,after);++failures;}
 assert(success&&failures>0);
 int count=0;{FrameStore store(hook);seed(store);candidate_seen=false;Cancel probe(1000000);testing=true;auto result=sw::render_a8(store,visible,masks,&probe);testing=false;assert(result.status==sw::Status::ok);count=probe.count;assert(count>0&&count<=1000);}
 int pre=0,post=0;
 for(int n=1;n<=count;++n){FrameStore store(hook);seed(store);auto before=read(store);candidate_seen=false;Cancel cancel(n);testing=true;auto result=sw::render_a8(store,visible,masks,&cancel);testing=false;assert(result.status==sw::Status::cancelled);same(before,read(store));if(candidate_seen)++post;else ++pre;std::printf("CANCEL n=%d calls=%d candidate=%d pre=%d post=%d\n",n,cancel.count,candidate_seen,cancel.pre,cancel.post);std::fflush(stdout);}
 assert(pre>0&&post>0);std::printf("A8_CONTRACT_PROOF_PASS fixtures=%zu oom=%u cancel=%d before_candidate=%d after_candidate=%d coverage_complete=false\n",fixtures.size(),failures,count,pre,post);
}
