#ifdef NDEBUG
#error Assertions required
#endif
#include "elliptic_t2a_oracle.h"
#include "../src/rendering/software/software_renderer.h"
#include "../../tests/software_render_smoke/png_writer.h"
#include <cstdio>
#include <fstream>
#include <string>
using namespace aether::rendering;namespace p=aether::rendering::preparation;namespace sw=aether::rendering::software;namespace o=elliptic_composition_oracle;
constexpr int W=20,H=16;static unsigned checks=0;
static p::PreparedFrame prep(std::vector<p::Command> commands){auto f=p::prepare({std::move(commands),true});assert(std::holds_alternative<p::PreparedFrame>(f));return std::get<p::PreparedFrame>(std::move(f));}
static Frame read(FrameStore& s){std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
static void binary(const std::string& path,const o::Bytes& bytes){std::ofstream f(path,std::ios::binary);assert(f);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();assert(f);}
static Frame check(const std::string& out,const std::string& name,std::vector<p::Command> c,const o::Bytes& expected){
 FrameStore store;assert(store.resize({W,H,1})==FrameStatus::ok);auto r=sw::render(store,prep(std::move(c)));std::printf("CASE %s status=%d budget=%llu\n",name.c_str(),int(r.status),(unsigned long long)r.pixel_budget);std::fflush(stdout);assert(r.status==sw::Status::ok);auto actual=read(store);
 binary(out+"/"+name+".actual.bgra",actual.pixels);binary(out+"/"+name+".expected.bgra",expected);binary(out+"/"+name+".png",smoke_png::png(smoke_png::rgba(actual),W,H));
 std::ofstream csv(out+"/"+name+".csv");csv<<"offset,expected,actual\n";assert(actual.pixels.size()==expected.size());for(std::size_t i=0;i<expected.size();++i)if(actual.pixels[i]!=expected[i])csv<<i<<','<<unsigned(expected[i])<<','<<unsigned(actual.pixels[i])<<'\n';csv.close();assert(csv);assert(actual.pixels==expected);++checks;return actual;
}
static o::Shape moved(o::Shape s,long double x,long double y){s.l+=x;s.r+=x;s.t+=y;s.b+=y;return s;}
static p::Fill move(p::Fill f,float x,float y){f.rect.x+=x;f.rect.y+=y;return f;}
int main(int argc,char** argv){assert(argc==2);std::string out=argv[1];elliptic_oracle::controls();
 // Explicit oracle literals are separate from named product payload fields.
 const o::Shape A{1.25L,1.5L,17.25L,13.5L,{{{6,3},{2,5},{7,4},{3,6}}}},B{.5L,.75L,18.5L,14.75L,{{{8,5},{4,6},{6,7},{5,3}}}},C{2,1,18,15,{{{4,7},{9,3},{5,6},{2,4}}}},D{0,2,19,16,{{{5,4},{3,8},{8,3},{4,5}}}},R{2.25L,2.5L,16.25L,13.5L,{}};
 const p::Fill fill{{1.25f,1.5f,16,12},{6,3,2,5,7,4,3,6},{1,0,0,1}};
 const p::PushClip b{{.5f,.75f,18,14},{8,5,4,6,6,7,5,3}},c{{2,1,16,14},{4,7,9,3,5,6,2,4}},d{{0,2,19,14},{5,4,3,8,8,3,4,5}},rect{{2.25f,2.5f,14,11},{}};
 const o::Color red{0,0,1,1},green{0,1,0,1},blue{1,0,0,1};
 auto expected=[&](std::vector<o::Paint> draws){return o::render(W,H,draws);};
 auto one=check(out,"different",{b,fill,p::PopClip{}},expected({{A,{B},red}}));
 auto duplicate=check(out,"duplicate",{b,b,fill,p::PopClip{},p::PopClip{}},expected({{A,{B,B},red}}));assert(one.pixels==duplicate.pixels);
 check(out,"two",{b,c,fill,p::PopClip{},p::PopClip{}},expected({{A,{B,C},red}}));
 check(out,"three",{b,c,d,fill,p::PopClip{},p::PopClip{},p::PopClip{}},expected({{A,{B,C,D},red}}));
 check(out,"rect-between",{b,rect,c,fill,p::PopClip{},p::PopClip{},p::PopClip{}},expected({{A,{B,R,C},red}}));
 // A draw after EACH pop, with distinct colors/alpha to expose stale nodes.
 auto f2=fill;f2.color={0,1,0,.5f};auto f3=fill;f3.color={0,0,1,.5f};auto f4=fill;f4.color={1,1,0,.5f};
 check(out,"pop-restoration",{b,rect,c,fill,p::PopClip{},f2,p::PopClip{},f3,p::PopClip{},f4},expected({{A,{B,R,C},red},{A,{B,R},{0,1,0,.5L}},{A,{B},{1,0,0,.5L}},{A,{},{0,1,1,.5L}}}));
 // Positive scroll offsets SUBTRACT; transform below adds the same displacement.
 p::Matrix matrix{1,0,0,1,-1.25f,-.5f};auto shifted=move(fill,-1.25f,-.5f);auto shifted_shape=moved(A,-1.25L,-.5L);auto want=expected({{shifted_shape,{B},red}});
 auto manual=check(out,"manual-frozen",{b,shifted,p::PopClip{}},want);
 auto translated=check(out,"translate-frozen",{b,p::PushTransform{matrix},fill,p::PopTransform{},p::PopClip{}},want);
 auto scrolled=check(out,"scroll-frozen",{b,p::PushScroll{1,1.25,.5},fill,p::PopScroll{},p::PopClip{}},want);assert(manual.pixels==translated.pixels&&manual.pixels==scrolled.pixels);
 // Clip is captured at first offset; nested scroll moves only later draws.
 auto nestedwant=expected({{moved(A,-1.75L,-1.25L),{moved(B,-1.25L,-.5L)},red},{shifted_shape,{moved(B,-1.25L,-.5L)},{0,1,0,.5L}},{A,{},blue}});
 auto tail=fill;tail.color={0,0,1,1};
 auto nestedactual=check(out,"nested-scroll-restore",{p::PushScroll{1,1.25,.5},b,p::PushScroll{2,.5,.75},fill,p::PopScroll{},f2,p::PopClip{},p::PopScroll{},tail},nestedwant);
 auto mb=b;mb.rect.x-=1.25f;mb.rect.y-=.5f;
 auto nestedmanual=check(out,"nested-manual",{mb,move(fill,-1.75f,-1.25f),move(f2,-1.25f,-.5f),p::PopClip{},tail},nestedwant);
 assert(nestedactual.pixels==nestedmanual.pixels);
 // Group alpha differs from per-primitive alpha where two opaque paints overlap.
 auto second=fill;second.color={0,1,0,1};auto opaque=expected({{A,{B},red},{A,{B},green}});auto groupwant=o::blank(W,H);o::composite(groupwant,opaque,.5L);
 auto grouped=check(out,"group-opacity",{b,p::BeginOpacity{1,0,.5f},fill,second,p::EndOpacity{1},p::PopClip{}},groupwant);
 auto translucent=fill;translucent.color.a=.5f;second.color.a=.5f;
 auto per=check(out,"primitive-opacity",{b,translucent,second,p::PopClip{}},expected({{A,{B},{0,0,1,.5L}},{A,{B},{0,1,0,.5L}}}));
 auto anchor=(8*W+8)*4;assert(grouped.pixels[anchor+3]==128&&per.pixels[anchor+3]==192&&grouped.pixels!=per.pixels);
 check(out,"nested-group",{b,p::BeginOpacity{1,0,.5f},p::BeginOpacity{2,1,.5f},fill,p::EndOpacity{2},p::EndOpacity{1},p::PopClip{}},[&]{auto inner=o::blank(W,H);o::composite(inner,expected({{A,{B},red}}),.5L);auto outer=o::blank(W,H);o::composite(outer,inner,.5L);return outer;}());
 // Reject an ancestral ellipse despite the most recent rectangular clip.
 p::BorderGroup border;border.outer={3,3,10,9};border.inner={5,5,6,5};for(auto& side:border.sides)side={true,AETHER_BORDER_STYLE_SOLID,{1,0,0,1}};
 p::BoxShadow shadow{{4,4,8,6},{},{0,0,0,2,{1,1,1,1},AETHER_FALSE}};
 for(int kind=0;kind<2;++kind){FrameStore store;assert(store.resize({W,H,1})==FrameStatus::ok);assert(sw::render(store,prep({p::Fill{{0,0,W,H},{},{0,0,1,1}}})).status==sw::Status::ok);auto before=read(store);p::Command primitive=kind?p::Command(shadow):p::Command(border);auto result=sw::render(store,prep({b,rect,primitive,p::PopClip{},p::PopClip{}}));auto after=read(store);std::printf("ANCESTRAL kind=%d status=%d index=%zu\n",kind,int(result.status),result.command_index);std::fflush(stdout);binary(out+"/ancestor-"+std::to_string(kind)+"-before.bgra",before.pixels);binary(out+"/ancestor-"+std::to_string(kind)+"-after.bgra",after.pixels);assert(result.status==sw::Status::unsupported&&result.command_index==2);assert(before.sequence==after.sequence&&before.stride==after.stride&&before.viewport.width==after.viewport.width&&before.viewport.height==after.viewport.height&&before.viewport.dpi==after.viewport.dpi&&before.pixels==after.pixels);}
 assert(checks==14);std::puts("ELLIPTIC_T2A_PASS fixtures=14 ancestor_rejections=2 T2b_NOT_RUN coverage_complete=false");
}
