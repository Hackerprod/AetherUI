#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include "../../tests/software_render_smoke/png_writer.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <chrono>
#include <limits>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto* q=std::malloc(n?n:1))return q;throw std::bad_alloc();}
void operator delete(void* q)noexcept{std::free(q);}void operator delete(void* q,std::size_t)noexcept{std::free(q);}
static int checks=0;
#define CK(x) do{++checks;assert(x);}while(0)
using Mask=std::shared_ptr<const text::GlyphMaskA8>;
using Font=std::shared_ptr<const text::FontInstance>;
Font font(){return std::make_shared<const text::FontInstance>(std::vector<std::uint8_t>{1},"SYNTHETIC_NOT_A_FONT",0,640,0);}
Mask mask(Font f,unsigned id=1,unsigned px=0,unsigned py=0,std::vector<std::uint8_t> b={0,1,127,128,254,255},int left=0,int top=0){auto w=b.size();return std::make_shared<const text::GlyphMaskA8>(f,id,0,px,py,w,w?1:0,w,left,top,std::move(b));}
p::Text textcmd(Font f,float x=1,float y=1,std::vector<text::Glyph> g={{1,0,0,0,0,0}}){
 p::Text t{};t.text="x";t.rect={x,y,6,10};t.font_family="synthetic";t.font_size=10;t.font_weight=400;t.color={.8f,.4f,.2f,.5f};
 t.native=p::NativeText{std::make_shared<const text::ShapedRun>(f,"x",std::move(g),aether_text_metrics_t{6,10,0}),y};return t;
}
p::PreparedFrame prep(std::vector<p::Command> cmds){p::Snapshot snap{std::move(cmds),true};auto a=p::prepare(snap);CK(std::holds_alternative<p::PreparedFrame>(a));return std::get<p::PreparedFrame>(std::move(a));}
Frame read(FrameStore& store){std::optional<Frame> f;CK(store.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
unsigned mul(unsigned a,unsigned b){return (a*b+127)/255;}
struct Cancel:p::Cancellation{int n,count=0;explicit Cancel(int v):n(v){}bool cancelled()noexcept override{return ++count==n;}};
int main(int argc,char** argv){
 FrameStore store;CK(store.resize({16,12,1})==FrameStatus::ok);auto f=font();std::vector<Mask> ms{mask(f)};
 auto t=textcmd(f);auto frame=prep({t});CK(s::render(store,frame).status==s::Status::unsupported);
 auto start=std::chrono::steady_clock::now();auto result=s::render_a8(store,frame,ms);CK(result.status==s::Status::ok);auto image=read(store);
 CK(result.mask_bytes==6&&result.glyph_uses==1&&result.mask_pixel_visits==6);
 // Independent integer oracle, all requested boundary coverages on clear and opaque backdrops.
 for(bool opaque:{false,true})for(unsigned alpha:{0u,1u,127u,128u,254u,255u}) {
  t.color.a=alpha/255.f;std::vector<p::Command> cmds;if(opaque)cmds.push_back(p::Fill{{0,0,16,12},{},{.1f,.2f,.3f,1}});cmds.push_back(t);
  auto pf=prep(cmds);CK(s::render_a8(store,pf,ms).status==s::Status::ok);auto got=read(store);
  unsigned cov[]{0,1,127,128,254,255};unsigned col[]{51,102,204},dst[]{77,51,26};
  for(int x=0;x<6;++x){auto at=(16+1+x)*4;unsigned a=mul(alpha,cov[x]);for(int c=0;c<3;++c)CK(got.pixels[at+c]==mul(col[c],a)+mul(opaque?dst[c]:0,255-a));CK(got.pixels[at+3]==a+mul(opaque?255:0,255-a));}
 }
 t=textcmd(f);auto nested=prep({p::BeginOpacity{1,0,.5f},p::BeginOpacity{2,1,.5f},t,p::EndOpacity{2},p::EndOpacity{1}});
 CK(s::render_a8(store,nested,ms).status==s::Status::ok);auto group=read(store);CK(group.pixels[(16+6)*4+3]==mul(mul(128,128),128));
 auto overlap=prep({t,t});CK(s::render_a8(store,overlap,ms).status==s::Status::ok);CK(read(store).pixels[(16+6)*4+3]==128+mul(128,127));
 // Negative origin uses floor, final phase includes scroll. A bearing moves into viewport.
 auto neg=textcmd(f,-.25f,2,{{1,0,0,0,0,16}});std::vector<Mask> phases{mask(f,1,32,32,{255},2,1)};
 auto np=prep({p::PushScroll{1,.25,.25},neg,p::PopScroll{}});CK(s::render_a8(store,np,phases).status==s::Status::ok);CK(read(store).pixels[1*4+3]==128);
 // Advances and Y-up offsets: second glyph moves right/down via negative advance_y.
 auto advance=textcmd(f,0,0,{{1,0,64,-64,0,0},{1,0,0,0,0,0}});std::vector<Mask> one{mask(f,1,0,0,{255})};
 auto ap=prep({advance});CK(s::render_a8(store,ap,one).status==s::Status::ok);CK(read(store).pixels[(16+1)*4+3]==128);
 auto spaces=textcmd(f,0,0,{{2,0,64,0,0,0},{1,0,0,0,0,0}});auto sm=one;sm.push_back(mask(f,2,0,0,{}));auto sp=prep({spaces});CK(s::render_a8(store,sp,sm).status==s::Status::ok);
 // Integer clip crops masks without resampling, stride padding is not coverage.
 auto padded=std::make_shared<const text::GlyphMaskA8>(f,1,0,0,0,2,2,3,-1,0,std::vector<std::uint8_t>{255,128,99,1,254,99});
 auto clipframe=prep({p::PushClip{{1,1,1,2},{}},t,p::PopClip{}});
 CK(s::render_a8(store,clipframe,std::vector<Mask>{padded}).status==s::Status::ok);
 auto clipped=read(store);CK(clipped.pixels[(16+1)*4+3]==mul(128,128));CK(clipped.pixels[(32+1)*4+3]==mul(128,254));CK(clipped.pixels[16*4+3]==0);
 FrameStore hidpi;CK(hidpi.resize({16,12,2})==FrameStatus::ok);CK(s::render_a8(hidpi,frame,ms).status==s::Status::unsupported);
 // Phase carry at one pixel and missing Y-phase must not silently reuse a bitmap.
 auto carry=textcmd(f,63.f/64,1,{{1,0,0,0,1,0}});auto cp=prep({carry});CK(s::render_a8(store,cp,one).status==s::Status::ok);CK(read(store).pixels[(16+1)*4+3]==128);
 auto before=read(store);auto rejected=[&](const p::PreparedFrame& pf,const std::vector<Mask>& masks,s::Status status){auto r=s::render_a8(store,pf,masks);CK(r.status==status);auto after=read(store);CK(after.sequence==before.sequence&&after.pixels==before.pixels);};
 rejected(np,one,s::Status::missing_mask);
 rejected(frame,{},s::Status::missing_mask);rejected(frame,{mask(font())},s::Status::missing_mask);rejected(frame,{ms[0],ms[0]},s::Status::invalid_mask);rejected(frame,{nullptr},s::Status::invalid_mask);
 auto fractional=prep({p::PushClip{{100.5,100,2,2},{}},t,p::PopClip{}});rejected(fractional,ms,s::Status::unsupported);
 auto curved=prep({p::PushClip{{100,100,4,4},{1,1,1,1,1,1,1,1}},t,p::PopClip{}});rejected(curved,ms,s::Status::unsupported);
 auto transformed=prep({p::PushTransform{{1,0,0,1,1,0}},t,p::PopTransform{}});rejected(transformed,ms,s::Status::unsupported);
 auto offgrid=textcmd(f,.1f,1);auto og=prep({offgrid});rejected(og,ms,s::Status::unsupported);
 // Full allocation and cancellation sweep: no partial frame commits.
 int oom=0;for(int n=0;n<200;++n){fail_after=n;auto r=s::render_a8(store,nested,ms);fail_after=-1;if(r.status==s::Status::ok)break;CK(r.status==s::Status::allocation_failed);++oom;CK(read(store).sequence==before.sequence);}CK(oom>0);
 before=read(store);Cancel probe(1000000);CK(s::render_a8(store,nested,ms,&probe).status==s::Status::ok);before=read(store);
 for(int n=1;n<=probe.count;++n){Cancel c(n);CK(s::render_a8(store,nested,ms,&c).status==s::Status::cancelled);CK(read(store).sequence==before.sequence);}
 // Constructor validation includes canonical spaces, overflow and exact buffer ownership.
 auto invalid=[&](unsigned w,unsigned h,unsigned stride,int l,std::vector<std::uint8_t>b,unsigned px=0){bool bad=false;try{auto m=std::make_shared<const text::GlyphMaskA8>(f,1,0,px,0,w,h,stride,l,0,std::move(b));}catch(const std::invalid_argument&){bad=true;}CK(bad);};
 invalid(0,1,0,0,{});invalid(1,1,0,0,{255});invalid(1,2,UINT32_MAX,0,{});invalid(1,1,1,0,{});invalid(0,0,0,1,{});invalid(1,1,1,0,{255},64);
 bool flags_bad=false;try{auto m=std::make_shared<const text::GlyphMaskA8>(f,1,7,0,0,1,1,1,0,0,std::vector<std::uint8_t>{255});}catch(const std::invalid_argument&){flags_bad=true;}CK(flags_bad);
 // Global bytes include unused supplied masks, and each repeated use charges work.
 std::vector<Mask> huge{std::make_shared<const text::GlyphMaskA8>(f,9,0,0,0,8192,8192,8192,0,0,std::vector<std::uint8_t>(64*1024*1024))};
 rejected(frame,huge,s::Status::limit_exceeded);huge.clear();
 auto repeated=textcmd(f,0,0,std::vector<text::Glyph>(65536,{1,0,0,0,0,0}));auto rp=prep({repeated});
 // Different-sized viewport maximizes visible per-use charge with modest mask storage.
 FrameStore workstore;CK(workstore.resize({1024,1024,1})==FrameStatus::ok);auto big=std::make_shared<const text::GlyphMaskA8>(f,1,0,0,0,1024,1024,1024,0,0,std::vector<std::uint8_t>(1024*1024,255));
 CK(s::render_a8(workstore,rp,std::vector<Mask>{big}).status==s::Status::limit_exceeded);
 // Prepare and masks retain all resources after the source is destroyed.
 auto weak=std::weak_ptr<const text::FontInstance>(f);f.reset();CK(!weak.expired());CK(s::render_a8(store,frame,ms).status==s::Status::ok);
 auto final=read(store);if(argc>1){
  auto png=smoke_png::png(smoke_png::rgba(final),16,12);
  // Embed provenance rather than allowing a tiny coverage fixture to look like HTML evidence.
  constexpr char label_bytes[]="Description\0SYNTHETIC A8 COVERAGE ONLY; NOT HTML; NOT A FONT";
  const std::string label(label_bytes,sizeof(label_bytes)-1);
  smoke_png::Bytes annotation;smoke_png::chunk(annotation,"tEXt",smoke_png::Bytes(label.begin(),label.end()));
  png.insert(png.end()-12,annotation.begin(),annotation.end());
  std::ofstream raw(std::string(argv[1])+"/synthetic-a8-coverage-NOT-HTML.png",std::ios::binary);
  raw.write(reinterpret_cast<const char*>(png.data()),png.size());raw.close();CK(bool(raw));
 }
 auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
 std::cout<<"SYNTHETIC_A8_ONLY checks="<<checks<<" allocation_failure_points="<<oom<<" cancellation_points="<<probe.count<<" suite_us="<<us<<"\nprovider=NOT_RUN font_verified=false shaping=NOT_RUN font_load=NOT_RUN glyph_rasterization=NOT_RUN chromium=NOT_RUN\nmask_count="<<result.mask_count<<" mask_bytes="<<result.mask_bytes<<" glyph_uses="<<result.glyph_uses<<" visible_mask_pixel_visits="<<result.mask_pixel_visits<<" preparation_ns="<<result.preparation_ns<<" composition_ns="<<result.composition_ns<<"\n";
}
