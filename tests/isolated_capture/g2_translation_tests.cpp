#ifdef NDEBUG
#error assertions required
#endif
#include "../../native/src/rendering/software/software_renderer.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
using namespace aether::rendering;
static long budget=-1;
void* operator new(std::size_t n){if(budget==0)throw std::bad_alloc();if(budget>0)--budget;if(auto* q=std::malloc(n?n:1))return q;throw std::bad_alloc();}
void operator delete(void* q)noexcept{std::free(q);}void operator delete(void* q,std::size_t)noexcept{std::free(q);}
static unsigned checks=0;
#define CK(x) do{++checks;assert(x);}while(0)
p::PushTransform tr(float x,float y=0){return {{1,0,0,1,x,y}};}
p::PreparedFrame prep(std::vector<p::Command> c,bool native=false){auto f=p::prepare({std::move(c),native});if(!std::holds_alternative<p::PreparedFrame>(f))std::cerr<<"prepare failure at check "<<checks<<" code="<<int(std::get<p::Error>(f).code)<<"\n";CK(std::holds_alternative<p::PreparedFrame>(f));return std::get<p::PreparedFrame>(std::move(f));}
Frame read(FrameStore& st){std::optional<Frame> f;CK(st.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
struct Cancel:p::Cancellation{int at,count=0;explicit Cancel(int n):at(n){}bool cancelled()noexcept override{return ++count==at;}};
int main(){
 FrameStore st;CK(st.resize({32,32,1})==FrameStatus::ok);
 auto render=[&](std::vector<p::Command> c){auto f=prep(std::move(c));CK(s::render(st,f).status==s::Status::ok);return read(st).pixels;};
 p::Fill fill{{4,5,8,8},{},{1,0,0,1}};
 CK(render({tr(2.25f,-1.5f),fill,p::PopTransform{}})==render({p::Fill{{6.25f,3.5f,8,8},{},{1,0,0,1}}}));
 p::Fill fractional{{4.5f,5,8,8},{},{1,0,0,1}};
 const auto precise=render({tr(32768),tr(1.f/1024),tr(-32768),fractional,p::PopTransform{},p::PopTransform{},p::PopTransform{}});
 CK(precise==render({p::Fill{{4.5f+1.f/1024,5,8,8},{},{1,0,0,1}}}));
 CK(precise!=render({fractional}));
 // Explicit oracle geometry, independent of the raster's displacement helper.
 CK(render({p::PushClip{{4,4,10,10},{}},tr(3,2),p::PushClip{{2,2,8,8},{}},p::PushScroll{1,1,1},p::BeginOpacity{1,0,.5f},fill,p::EndOpacity{1},p::PopScroll{},p::PopClip{},p::PopTransform{},p::PopClip{}})==render({p::PushClip{{4,4,10,10},{}},p::PushClip{{5,4,8,8},{}},p::BeginOpacity{1,0,.5f},p::Fill{{6,6,8,8},{},{1,0,0,1}},p::EndOpacity{1},p::PopClip{},p::PopClip{}}));
 p::LinearGradientFill g;g.rect={4,5,8,8};g.gradient_rect={2,3,12,12};g.direction_x=1;g.stops={{0,{1,0,0,1}},{1,{0,0,1,1}}};auto shifted=g;shifted.rect={6.25f,3.5f,8,8};shifted.gradient_rect={4.25f,1.5f,12,12};
 CK(render({tr(2.25f,-1.5f),g,p::PopTransform{}})==render({shifted}));
 p::BorderGroup b;b.outer={4,5,12,12};b.inner={6,7,8,8};for(auto& side:b.sides)side={true,AETHER_BORDER_STYLE_DASHED,{1,0,0,1}};auto bs=b;bs.outer={6.25f,3.5f,12,12};bs.inner={8.25f,5.5f,8,8};
 CK(render({tr(2.25f,-1.5f),b,p::PopTransform{}})==render({bs}));
 p::Stroke stroke{{4,5,8,8},{},2,{1,0,0,1}},ss=stroke;ss.rect={6.25f,3.5f,8,8};CK(render({tr(2.25f,-1.5f),stroke,p::PopTransform{}})==render({ss}));
 p::BoxShadow shadow{};shadow.rect={4,5,8,8};shadow.shadow.color={1,0,0,1};shadow.shadow.offset_x=2;shadow.shadow.offset_y=1;shadow.shadow.blur_radius=2;auto sh=shadow;sh.rect={6.25f,3.5f,8,8};CK(render({tr(2.25f,-1.5f),shadow,p::PopTransform{}})==render({sh}));
 auto old=read(st);auto reject=[&](std::vector<p::Command> c){auto f=prep(std::move(c));CK(s::render(st,f).status==s::Status::unsupported);auto now=read(st);CK(now.sequence==old.sequence&&now.pixels==old.pixels);};
 reject({tr(65536),tr(1.f/1024),tr(-65536),p::PopTransform{},p::PopTransform{},p::PopTransform{}});
 reject({p::PushTransform{{2,0,0,1,0,0}},p::PopTransform{}});reject({p::PushTransform{{1,.1f,0,1,0,0}},p::Fill{{1000,1000,0,0},{},{}},p::PopTransform{}});
 reject({tr(-65536),p::PushScroll{1,1,0},p::PopScroll{},p::PopTransform{}});
 reject({tr(65536),p::Fill{{0,0,1,1},{},{}},p::PopTransform{}});
 reject({tr(65535),p::Stroke{{0,0,1,1},{},4,{}},p::PopTransform{}});
 reject({tr(65530),shadow,p::PopTransform{}});
 // Synthetic A8: translation changes mask phase BEFORE lookup. Not real fonts.
 auto font=std::make_shared<const text::FontInstance>(std::vector<std::uint8_t>{1},"SYNTHETIC",0,640,0);
 p::Text t{};t.text="x";t.font_family="synthetic";t.rect={1,1,1,1};t.font_size=10;t.font_weight=400;t.color={1,0,0,1};t.native=p::NativeText{std::make_shared<const text::ShapedRun>(font,"x",std::vector<text::Glyph>{{1,0,0,0,0,0}},aether_text_metrics_t{1,1,0}),1};
 using Mask=std::shared_ptr<const text::GlyphMaskA8>;
 std::vector<Mask> masks{std::make_shared<const text::GlyphMaskA8>(font,1,0,32,16,1,1,1,0,0,std::vector<std::uint8_t>{255})};
 auto tf=prep({tr(.5,.25),t,p::PopTransform{}},true);CK(s::render_a8(st,tf,masks).status==s::Status::ok);auto ti=read(st);CK(ti.pixels[(32+1)*4+3]==255);
 old=ti;CK(s::render_a8(st,tf,{}).status==s::Status::missing_mask);CK(read(st).sequence==old.sequence);
 std::vector<Mask> edge{std::make_shared<const text::GlyphMaskA8>(font,1,0,0,0,1,1,1,1,0,std::vector<std::uint8_t>{255})};
 auto ef=prep({tr(65535),t,p::PopTransform{}},true);CK(s::render_a8(st,ef,edge).status==s::Status::unsupported);CK(read(st).sequence==old.sequence);
 auto pf=prep({tr(1),tr(2),p::BeginOpacity{1,0,.5f},g,p::EndOpacity{1},p::PopTransform{},p::PopTransform{}});
 int failures=0;bool done=false;for(int i=0;i<100;++i){budget=i;auto r=s::render(st,pf);budget=-1;if(r.status==s::Status::ok){done=true;break;}CK(r.status==s::Status::allocation_failed);auto now=read(st);CK(now.sequence==old.sequence&&now.pixels==old.pixels);++failures;}CK(done&&failures>0);
 Cancel probe(1000000);CK(s::render(st,pf,&probe).status==s::Status::ok);old=read(st);for(int i=1;i<=probe.count;++i){Cancel c(i);CK(s::render(st,pf,&c).status==s::Status::cancelled);auto now=read(st);CK(now.sequence==old.sequence&&now.pixels==old.pixels);}
 std::cout<<"G2 ABI checks="<<checks<<" render_oom_sites="<<failures<<" cancellation_sites="<<probe.count<<" HTML=NOT_RUN Chromium=NOT_RUN fonts=SYNTHETIC_ONLY\n";
}
