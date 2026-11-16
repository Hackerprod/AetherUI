#ifdef NDEBUG
#error Assertions required
#endif
#include "elliptic_t2b_oracle.h"
#include "../src/rendering/software/software_renderer.h"
#include "../../tests/software_render_smoke/png_writer.h"
#include <cstdio>
#include <fstream>
#include <string>
using namespace aether::rendering;namespace p=aether::rendering::preparation;namespace sw=aether::rendering::software;namespace o=elliptic_paint_oracle;
using Mask=std::shared_ptr<const text::GlyphMaskA8>;constexpr int W=16,H=12;static unsigned checks=0;
static p::PreparedFrame prep(std::vector<p::Command> c){auto f=p::prepare({std::move(c),true});assert(std::holds_alternative<p::PreparedFrame>(f));return std::get<p::PreparedFrame>(std::move(f));}
static Frame read(FrameStore& s){std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
static void binary(const std::string& path,const o::Bytes& b){std::ofstream f(path,std::ios::binary);assert(f);f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();assert(f);}
static Frame check(const std::string& out,const std::string& name,std::vector<p::Command> commands,const o::Bytes& expected,const std::vector<Mask>* masks=nullptr){
 FrameStore store;assert(store.resize({W,H,1})==FrameStatus::ok);auto prepared=prep(std::move(commands));auto result=masks?sw::render_a8(store,prepared,*masks):sw::render(store,prepared);std::printf("CASE %s status=%d budget=%llu\n",name.c_str(),int(result.status),(unsigned long long)result.pixel_budget);std::fflush(stdout);assert(result.status==sw::Status::ok);auto actual=read(store);
 binary(out+"/"+name+".actual.bgra",actual.pixels);binary(out+"/"+name+".expected.bgra",expected);binary(out+"/"+name+"-SYNTHETIC-NOT-HTML.png",smoke_png::png(smoke_png::rgba(actual),W,H));std::ofstream csv(out+"/"+name+".csv");csv<<"offset,expected,actual\n";assert(actual.pixels.size()==expected.size());for(std::size_t i=0;i<expected.size();++i)if(actual.pixels[i]!=expected[i])csv<<i<<','<<unsigned(expected[i])<<','<<unsigned(actual.pixels[i])<<'\n';csv.close();assert(csv);assert(actual.pixels==expected);++checks;return actual;
}
int main(int argc,char** argv){assert(argc==2);std::string out=argv[1];elliptic_oracle::controls();
 const o::Shape A{0,0,16,12,{{{6,3},{2,5},{4,6},{3,4}}}},C{.25L,.5L,14.25L,10.5L,{{{5,2},{3,4},{4,5},{2,3}}}};
 const p::Radii ar{6,3,2,5,4,6,3,4};const p::PushClip clip{{.25f,.5f,14,10},{5,2,3,4,4,5,2,3}};
 p::LinearGradientFill g;g.rect={0,0,16,12};g.gradient_rect={0,0,16,16};g.radii=ar;g.direction_x=1;g.stops={{0,{1,0,0,0}},{1,{0,0,1,1}}};
 o::Gradient gh{A,{}, {0,0,16,16},false,1,{{0,{0,0,1,0}},{1,{1,0,0,1}}}};
 auto full=check(out,"gradient-h",{g},o::gradient(W,H,gh));gh.clips={C};auto clipped=check(out,"gradient-h-clip",{clip,g,p::PopClip{}},o::gradient(W,H,gh));
 auto anchor=(6*W+8)*4;assert(full.pixels[anchor]==135&&full.pixels[anchor+3]==135&&full.pixels[anchor+2]==0);for(int k=0;k<4;++k)assert(clipped.pixels[anchor+k]==full.pixels[anchor+k]);
 auto v=g;v.direction_x=0;v.direction_y=1;v.opacity=.5f;v.stops={{0,{1,0,0,1}},{1,{0,1,0,.5f}}};auto gv=gh;gv.vertical=true;gv.opacity=.5L;gv.stops={{0,{0,0,1,1}},{1,{0,1,0,.5L}}};check(out,"gradient-v-alpha",{clip,v,p::PopClip{}},o::gradient(W,H,gv));
 auto hard=g;hard.stops={{0,{1,0,0,1}},{.5f,{1,0,0,1}},{.5f,{0,1,0,.5f}},{1,{0,1,0,.5f}}};auto gr=gh;gr.stops={{0,{0,0,1,1}},{.5L,{0,0,1,1}},{.5L,{0,1,0,.5L}},{1,{0,1,0,.5L}}};auto hardframe=check(out,"gradient-hard",{clip,hard,p::PopClip{}},o::gradient(W,H,gr));assert(hardframe.pixels[(6*W+7)*4+2]==255&&hardframe.pixels[anchor+1]==128&&hardframe.pixels[anchor+3]==128);
 auto shifted=g;shifted.rect.x-=1.25f;shifted.rect.y-=.5f;shifted.gradient_rect.x-=1.25f;shifted.gradient_rect.y-=.5f;auto gs=gh;gs.shape.l-=1.25L;gs.shape.r-=1.25L;gs.shape.t-=.5L;gs.shape.b-=.5L;gs.ramp={-1.25L,-.5L,14.75L,15.5L};auto swant=o::gradient(W,H,gs);
 auto manual=check(out,"gradient-manual",{clip,shifted,p::PopClip{}},swant);auto scroll=check(out,"gradient-scroll",{clip,p::PushScroll{1,1.25,.5},g,p::PopScroll{},p::PopClip{}},swant);assert(manual.pixels==scroll.pixels);
 auto groupwant=o::Bytes(W*H*4);elliptic_composition_oracle::composite(groupwant,o::gradient(W,H,gh),.5L);check(out,"gradient-group",{clip,p::BeginOpacity{1,0,.5f},g,p::EndOpacity{1},p::PopClip{}},groupwant);
 // Immutable premultiplied source bytes, independent scalar bilinear reference.
 const o::Bytes pixels2{0,0,255,255, 0,255,0,255, 255,0,0,255, 255,255,255,255};o::Bytes pixels4(64);for(int i=0;i<16;++i){pixels4[4*i]=(i*37)%180;pixels4[4*i+1]=(i*61)%180;pixels4[4*i+2]=(i*83)%180;pixels4[4*i+3]=200;}
 auto res2=std::get<p::ImageRef>(p::ImageResource::copy(2,2,pixels2));auto res4=std::get<p::ImageRef>(p::ImageResource::copy(4,4,pixels4));
 p::Image im2{res2,{0,0,2,2},{0,0,16,16},1,p::ImageSamplingBounds::StrictCrop};o::Image oi2{2,2,pixels2,{0,0,2,2},{0,0,16,16,{}},{C},false,1};auto i2=check(out,"image2",{clip,im2,p::PopClip{}},o::image(W,H,oi2));auto dup=check(out,"image2-duplicate",{clip,clip,im2,p::PopClip{},p::PopClip{}},o::image(W,H,oi2));assert(i2.pixels==dup.pixels);
 p::Image crop{res4,{1,1,2,2},{0,0,16,16},.5f,p::ImageSamplingBounds::StrictCrop};o::Image ocrop{4,4,pixels4,{1,1,3,3},{0,0,16,16,{}},{C},false,.5L};check(out,"image4-strict-crop",{clip,crop,p::PopClip{}},o::image(W,H,ocrop));
 p::Image fractional{res4,{.25f,.5f,3.5f,3},{-4,-4,16,16},1,p::ImageSamplingBounds::FullResource};o::Image ofull{4,4,pixels4,{.25L,.5L,3.75L,3.5L},{-4,-4,12,12,{}},{C},true,1};auto fi=check(out,"image4-full-fractional",{clip,fractional,p::PopClip{}},o::image(W,H,ofull));auto unclipped=ofull;unclipped.clips.clear();auto ui=check(out,"image4-unclipped",{fractional},o::image(W,H,unclipped));for(int k=0;k<4;++k)assert(fi.pixels[anchor+k]==ui.pixels[anchor+k]);
 // At (0,4), b=[.25,1]x[4,5], all admitted, coverage3/4. Sample MUST
 // be device center(.5,4.5), not clipped center(.625,4.5).
 auto fixed=o::image_sample(ofull,.5L,4.5L),wrong=o::image_sample(ofull,.625L,4.5L);bool distinguish=false;for(int k=0;k<4;++k){assert(fi.pixels[(4*W)*4+k]==o::Q(fixed[k]*.75L));distinguish=distinguish||o::Q(fixed[k]*.75L)!=o::Q(wrong[k]*.75L);}assert(distinguish);
 // Explicit synthetic A8 only: no provider, TrueType rasterizer, or HTML claim.
 auto font=std::make_shared<const text::FontInstance>(std::vector<std::uint8_t>{1},"SYNTHETIC-NOT-FONT",0,640,0);o::Bytes maskbytes(80,99);const unsigned values[]{0,1,127,128,254,255};for(int y=0;y<8;++y)for(int x=0;x<8;++x)maskbytes[y*10+x]=values[(x+2*y)%6];
 auto mask=std::make_shared<const text::GlyphMaskA8>(font,1,0,0,0,8,8,10,-1,1,maskbytes);std::vector<Mask> masks{mask};p::Text text{};text.text="x";text.rect={1,1,8,10};text.font_family="synthetic";text.font_size=10;text.font_weight=400;text.color={.75f,.5f,.25f,.5f};text.native=p::NativeText{std::make_shared<const text::ShapedRun>(font,"x",std::vector<text::Glyph>{{1,0,0,0,0,0}},aether_text_metrics_t{8,10,0}),1};assert(text.rect.x+mask->bearing_left==0&&text.native->baseline-mask->bearing_top==0);
 const p::PushClip ac{{0,0,8,8},{6,2,1,4,4,3,2,5}},bc{{1,0,7,8},{2,3,3,2,4,3,2,4}};const o::Shape AC{0,0,8,8,{{{6,2},{1,4},{4,3},{2,5}}}},BC{1,0,8,8,{{{2,3},{3,2},{4,3},{2,4}}}};const o::Color tint{.25L,.5L,.75L,.5L};auto awant=o::mask(W,H,0,0,maskbytes,{AC},tint);
 auto a=check(out,"a8-ellipse",{ac,text,p::PopClip{}},awant,&masks);auto ad=check(out,"a8-duplicate",{ac,ac,text,p::PopClip{},p::PopClip{}},awant,&masks);assert(a.pixels==ad.pixels);
 check(out,"a8-two",{ac,bc,text,p::PopClip{},p::PopClip{}},o::mask(W,H,0,0,maskbytes,{AC,BC},tint),&masks);
 auto ag=o::Bytes(W*H*4);elliptic_composition_oracle::composite(ag,awant,.5L);check(out,"a8-group",{ac,p::BeginOpacity{1,0,.5f},text,p::EndOpacity{1},p::PopClip{}},ag,&masks);
 auto ts=text;ts.rect.x-=2;ts.rect.y-=1;ts.native->baseline-=1;auto aswant=o::mask(W,H,-2,-1,maskbytes,{AC},tint);auto am=check(out,"a8-manual",{ac,ts,p::PopClip{}},aswant,&masks);auto ascroll=check(out,"a8-scroll",{ac,p::PushScroll{1,2,1},text,p::PopScroll{},p::PopClip{}},aswant,&masks);assert(am.pixels==ascroll.pixels);
 FrameStore store;assert(store.resize({W,H,1})==FrameStatus::ok);assert(sw::render(store,prep({p::Fill{{0,0,W,H},{},{0,1,0,1}}})).status==sw::Status::ok);auto before=read(store);auto fractionalclip=ac;fractionalclip.rect.x=.25f;auto result=sw::render_a8(store,prep({fractionalclip,text,p::PopClip{}}),masks);auto after=read(store);binary(out+"/a8-fractional-before.bgra",before.pixels);binary(out+"/a8-fractional-after.bgra",after.pixels);std::printf("NEG a8-fractional status=%d index=%zu\n",int(result.status),result.command_index);std::fflush(stdout);assert(result.status==sw::Status::unsupported&&result.command_index==0);assert(before.sequence==after.sequence&&before.stride==after.stride&&before.viewport.width==after.viewport.width&&before.viewport.height==after.viewport.height&&before.viewport.dpi==after.viewport.dpi&&before.pixels==after.pixels);
 assert(checks==18);std::puts("ELLIPTIC_T2B_PASS gradient=7 image=5 a8=6 fractional_rejections=1 coverage_complete=false");
}
