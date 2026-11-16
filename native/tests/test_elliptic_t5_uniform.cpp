#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
using namespace aether::rendering;namespace p=aether::rendering::preparation;namespace sw=aether::rendering::software;
using Mask=std::shared_ptr<const text::GlyphMaskA8>;
static std::string output;static unsigned cases=0;
static p::Radii radii(float r){return {r,r,r,r,r,r,r,r};}
static Frame read(FrameStore& s){std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
static void save(const std::string& n,const Frame& f){std::ofstream o(output+"/"+n+".bgra",std::ios::binary);assert(o);o.write(reinterpret_cast<const char*>(f.pixels.data()),f.pixels.size());o.close();assert(o);}
static void check(const char* name,std::vector<p::Command> commands,sw::Status expected=sw::Status::ok,const std::vector<Mask>* masks=nullptr,int w=16,int h=12){
 FrameStore store;assert(store.resize({w,h,1})==FrameStatus::ok);FrameStore::Token token;assert(store.prepare(token)==FrameStatus::ok);std::span<std::uint8_t> bytes;assert(store.candidate(token,bytes)==FrameStatus::ok);for(std::size_t i=0;i<bytes.size();i+=4){bytes[i]=(i/4)%127;bytes[i+1]=17;bytes[i+2]=53;bytes[i+3]=255;}assert(store.commit(token)==FrameStatus::ok);auto before=read(store);
 auto prepared=p::prepare({std::move(commands),true});assert(std::holds_alternative<p::PreparedFrame>(prepared));const auto& f=std::get<p::PreparedFrame>(prepared);auto r=masks?sw::render_a8(store,f,*masks):sw::render(store,f);auto after=read(store);save(std::string(name)+"-before",before);save(name,after);
 // Every stable public result field, intentionally excluding only two measured durations.
 std::printf("CASE %s status=%d frame_status=%d index=%zu work=%llu corner=%llu sampled=%llu inner_bound=%llu inner_sampled=%llu clip_bound=%llu joint=%llu masks=%llu mask_bytes=%llu glyphs=%llu mask_visits=%llu central=%llu contains=%llu visits=%llu nodes=%llu depth=%llu sequence=%llu width=%d height=%d dpi=%g stride=%zu bytes=%zu\n",name,int(r.status),int(r.frame_status),r.command_index,(unsigned long long)r.pixel_budget,(unsigned long long)r.corner_pixel_bound,(unsigned long long)r.sampled_pixels,(unsigned long long)r.inner_corner_pixel_bound,(unsigned long long)r.inner_sampled_pixels,(unsigned long long)r.clip_corner_pixel_bound,(unsigned long long)r.joint_sampled_pixels,(unsigned long long)r.mask_count,(unsigned long long)r.mask_bytes,(unsigned long long)r.glyph_uses,(unsigned long long)r.mask_pixel_visits,(unsigned long long)r.clip_central_tests,(unsigned long long)r.clip_contains_tests,(unsigned long long)r.clip_node_visits,(unsigned long long)r.clip_node_count,(unsigned long long)r.max_clip_depth,(unsigned long long)after.sequence,after.viewport.width,after.viewport.height,double(after.viewport.dpi),after.stride,after.pixels.size());std::fflush(stdout);
 assert(r.status==expected&&r.frame_status==FrameStatus::ok);assert(after.viewport.width==w&&after.viewport.height==h&&after.viewport.dpi==1&&after.stride==std::size_t(w)*4&&after.pixels.size()==std::size_t(w)*h*4);
 if(expected==sw::Status::ok)assert(after.sequence==before.sequence+1);else assert(after.sequence==before.sequence&&after.pixels==before.pixels);++cases;
}
int main(int argc,char**argv){assert(argc==2);output=argv[1];p::Fill fill{{1,1,12,10},radii(3),{.75f,.5f,.25f,.5f}};p::PushClip clip{{0,0,16,12},radii(4)},inner{{1,1,12,10},radii(3)};
 check("rect-fractional",{p::Fill{{.25f,.5f,10.5f,8.25f},{},{1,0,0,.5f}}});check("round-alpha",{fill});
 check("stroke-rect",{p::Stroke{{2,2,10,8},{},1.5f,{1,0,0,.5f}}});check("stroke-round",{p::Stroke{{2,2,10,8},radii(3),1.5f,{0,1,0,.5f}}});
 check("nested-clip",{clip,inner,fill,p::PopClip{},p::PopClip{}});
 p::LinearGradientFill g;g.rect=g.gradient_rect={1,1,12,10};g.radii=radii(3);g.direction_x=1;g.opacity=.75f;g.stops={{0,{1,0,0,.25f}},{.5f,{0,1,0,.5f}},{1,{0,0,1,1}}};check("gradient-round",{g});check("gradient-nested",{clip,inner,g,p::PopClip{},p::PopClip{}});
 auto image=std::get<p::ImageRef>(p::ImageResource::copy(2,2,std::array<std::uint8_t,16>{0,0,255,255,0,255,0,255,255,0,0,255,255,255,255,255}));p::Image im{image,{0,0,2,2},{.25f,.5f,14,10},.5f,p::ImageSamplingBounds::StrictCrop};check("image-fractional",{im});check("image-nested",{clip,inner,im,p::PopClip{},p::PopClip{}});
 check("translate-frozen-clip",{clip,p::PushTransform{{1,0,0,1,-2,-1}},fill,p::PopTransform{},p::PopClip{}});check("scroll-frozen-clip",{clip,p::PushScroll{1,2,1},fill,p::PopScroll{},p::PopClip{}});
 check("opacity-nested",{clip,p::BeginOpacity{1,0,.5f},p::BeginOpacity{2,1,.75f},fill,g,p::EndOpacity{2},p::EndOpacity{1},p::PopClip{}});
 auto font=std::make_shared<const text::FontInstance>(std::vector<std::uint8_t>{1},"SYNTHETIC-UNIFORM",0,640,0);std::vector<std::uint8_t> maskbytes(80,99);for(int y=0;y<8;++y)for(int x=0;x<8;++x)maskbytes[y*10+x]=std::uint8_t((x+y)*17);std::vector<Mask> masks{std::make_shared<const text::GlyphMaskA8>(font,1,0,0,0,8,8,10,-1,1,maskbytes)};p::Text t{};t.text="x";t.rect={1,1,8,10};t.font_family="synthetic";t.font_size=10;t.font_weight=400;t.color={.75f,.5f,.25f,.5f};t.native=p::NativeText{std::make_shared<const text::ShapedRun>(font,"x",std::vector<text::Glyph>{{1,0,0,0,0,0}},aether_text_metrics_t{8,10,0}),1};check("a8-circular",{clip,t,p::PopClip{}},sw::Status::ok,&masks);check("a8-group-scroll",{clip,p::BeginOpacity{1,0,.5f},p::PushScroll{1,2,1},t,p::PopScroll{},p::EndOpacity{1},p::PopClip{}},sw::Status::ok,&masks);
 check("uniform-work-limit",{p::PushClip{{0,0,500,500},radii(250)},p::Fill{{0,0,500,500},{},{1,1,1,1}},p::PopClip{}},sw::Status::limit_exceeded,nullptr,500,500);
 p::BoxShadow shadow;shadow.rect={2,2,8,8};shadow.shadow.color={0,0,0,1};check("shadow-curved-ancestor",{clip,shadow,p::PopClip{}},sw::Status::unsupported);
 assert(cases==16);std::printf("T5_UNIFORM_PASS cases=16 stable_fields=all_except_durations coverage_complete=false\n");
}
