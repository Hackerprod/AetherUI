#include "rendering/html_ltr/owned_raster.h"
#include "rendering/html_ltr/raster_coordinate.h"
#include "rendering/html_ltr/owned_software_renderer.h"
#include "rendering/html_ltr/persistent_font_provider.h"
#include "rendering/text/real_font_binding.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <cstring>
using namespace aether::rendering;
namespace h=html_ltr;namespace tt=text::truetype;namespace prep=preparation;
// Test-only replacement: fail each owned allocation, including rebinding.
static long fail_at=-1,attempts=0;
static void failure(){if(fail_at>=0&&attempts++==fail_at)throw std::bad_alloc{};}
void* operator new(std::size_t n){failure();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc{};}
void* operator new[](std::size_t n){failure();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc{};}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){failure();void* p=nullptr;if(posix_memalign(&p,std::size_t(a),n?n:1))throw std::bad_alloc{};return p;}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}
static bool bytes(const auto& a,const auto& b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin());}
int main(int argc,char** argv){
 // Independent rational oracle, including exact rounding ties.
 assert(h::quantize_absolute(1.0/128)==1&&h::quantize_absolute(-1.0/128)==-1);
 for(unsigned m=0;m<256;++m)for(unsigned x=0;x<=4;++x)for(unsigned y=0;y<=4;++y)assert(software::render_kernel::filtered_a8(m,{0,0,x/4.0,y/4.0})==(m*x*y+8)/16);
 {auto w=software::render_kernel::max_work-9;assert(software::render_kernel::charge(w,1,9));assert(!software::render_kernel::charge(w,1,9));w=0;assert(!software::render_kernel::charge(w,UINT64_MAX,9)&&w==0);}
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(f),{}};assert(!data.empty());
 h::OwnedBudget budget;const auto initial=budget.total().current;
 {
 auto provider=h::PersistentFontProvider::create(budget,data.data(),data.size(),nullptr,0);auto operation=provider->operation();auto owned=provider->product("A",16,400,24);auto oo=provider->outline(owned->binding,owned->run->glyphs.front().id);
 h::FontProvider legacy(data,{},2);auto product=legacy.product("A",16,400,24);auto lo=legacy.outline(product.binding,product.glyphs.front().id);
 // Curves, composite glyphs, empty outlines and generated password bullet.
 for(const char* sample:{"O","\xc3\xa9"," ","\xe2\x80\xa2"}){
  auto op=provider->product(sample,16,400,24);auto lp=legacy.product(sample,16,400,24);
  auto oline=provider->outline(op->binding,op->run->glyphs.front().id);auto lline=legacy.outline(lp.binding,lp.glyphs.front().id);
  for(unsigned phase:{0u,16u,63u}){
   tt::AggregateBudget a{512000000},m{16*1024*1024},b=a,n=m;tt::RasterWork aw{},bw{};
   auto l=tt::rasterize_document_budgeted(lline,lp.binding->font(),phase,phase,a,m,nullptr,&aw);auto o=h::rasterize_owned_document(budget,oline,op->binding->font,phase,phase,b,n,nullptr,&bw);
   assert(std::holds_alternative<tt::RasterOutput>(l)&&std::holds_alternative<h::OwnedRasterOutput>(o));const auto& lm=std::get<tt::RasterOutput>(l).mask;const auto& om=std::get<h::OwnedRasterOutput>(o).mask;
   assert(bytes(lm->bytes,om->bytes)&&lm->width==om->width&&lm->height==om->height&&lm->bearing_left==om->bearing_left&&lm->bearing_top==om->bearing_top);
   assert(std::memcmp(&aw,&bw,sizeof aw)==0&&a.remaining==b.remaining&&m.remaining==n.remaining);
  }
 }
 auto lr=std::make_shared<const text::ShapedRun>(product.binding->font(),product.source,product.glyphs,product.metrics);
 auto alias_source=h::owned_text::legacy_source(provider->regular_source());assert(alias_source->bytes.empty()&&alias_source->byte_view().data()==provider->regular_source()->byte_view().data());
 auto alias_binding_result=tt::RealFontBinding::create(alias_source,1024);assert(std::holds_alternative<std::shared_ptr<const tt::RealFontBinding>>(alias_binding_result));auto alias_binding=std::get<std::shared_ptr<const tt::RealFontBinding>>(alias_binding_result);
 auto alias_outline_result=alias_binding->face()->outline(product.glyphs.front().id);assert(std::holds_alternative<std::shared_ptr<const tt::Outline>>(alias_outline_result));auto alias_outline=std::get<std::shared_ptr<const tt::Outline>>(alias_outline_result);
 auto alias_run=std::make_shared<const text::ShapedRun>(alias_binding->font(),product.source,product.glyphs,product.metrics);assert(alias_binding->font()->bytes.empty());assert(h::owned_software_detail::LegacyAccountedStorage{budget.handle()}.font_bytes(*alias_binding->font())==data.size());

 for(unsigned phase:{0u,16u,63u}){
  tt::AggregateBudget la{512000000},lm{16*1024*1024},oa=la,om=lm;tt::RasterWork lw{},ow{};
  auto lres=tt::rasterize_document_budgeted(lo,product.binding->font(),phase,phase,la,lm,nullptr,&lw);
  auto ores=h::rasterize_owned_document(budget,oo,owned->binding->font,phase,phase,oa,om,nullptr,&ow);
  assert(std::holds_alternative<tt::RasterOutput>(lres)&&std::holds_alternative<h::OwnedRasterOutput>(ores));
  const auto& l=std::get<tt::RasterOutput>(lres);const auto& o=std::get<h::OwnedRasterOutput>(ores);
  assert(l.advance_px==o.advance_px&&l.advance_26_6==o.advance_26_6&&la.remaining==oa.remaining&&lm.remaining==om.remaining);
  assert(std::memcmp(&lw,&ow,sizeof lw)==0);assert(l.mask->width==o.mask->width&&l.mask->height==o.mask->height&&l.mask->bearing_left==o.mask->bearing_left&&l.mask->bearing_top==o.mask->bearing_top&&bytes(l.mask->bytes,o.mask->bytes));
  const float baseline=16.f+float(phase)/64;const prep::Rect rect{4.f+float(phase)/64,baseline-product.metrics.baseline,product.metrics.width,24};
  prep::Text lt{"A",rect,"AetherLTR",16,400,AETHER_TEXT_ALIGN_START,{1,1,1,1},0,prep::NativeText{lr,baseline}};
  h::InteractionTextView ot{"A",rect,"AetherLTR",16,400,AETHER_TEXT_ALIGN_START,{1,1,1,1},0,h::InteractionNativeText{owned->run,baseline}};
  prep::BorderGroup border{{1,1,30,28},{2,2,28,26},{},{},{}};for(auto& side:border.sides)side={true,AETHER_BORDER_STYLE_SOLID,{1,0,0,1}};
  prep::Snapshot ls;ls.native_text_required=true;ls.commands={prep::Fill{{0,0,32,32},{},{0,0,0,1}},prep::BeginChunk{1},prep::PushClip{{0,0,32,32},{}},prep::BeginOpacity{1,0,0.75f},prep::PushTransform{prep::identity},border,lt,prep::PopTransform{},prep::EndOpacity{1},prep::PopClip{},prep::EndChunk{1}};
  const h::InteractionCommandView views[]={prep::Fill{{0,0,32,32},{},{0,0,0,1}},prep::BeginChunk{1},prep::PushClip{{0,0,32,32},{}},prep::BeginOpacity{1,0,0.75f},prep::PushTransform{prep::identity},border,ot,prep::PopTransform{},prep::EndOpacity{1},prep::PopClip{},prep::EndChunk{1}};
  auto os=h::interaction_snapshot(budget,views);assert(std::holds_alternative<h::InteractionSnapshot>(os));auto op=h::interaction_prepare(budget,std::get<h::InteractionSnapshot>(os));assert(std::holds_alternative<h::InteractionPreparedFrame>(op));auto lp=prep::prepare(ls);assert(std::holds_alternative<prep::PreparedFrame>(lp));
  FrameStore store;assert(store.resize({32,32,1})==FrameStatus::ok);const std::shared_ptr<const text::GlyphMaskA8> lmask[]{l.mask};const std::shared_ptr<const h::owned_text::Mask> omask[]{o.mask};
  auto lrender=software::render_a8(store,std::get<prep::PreparedFrame>(lp),lmask);assert(lrender.status==software::Status::ok);
  h::InteractionFrame confirmed;auto orender=h::render_owned_a8(budget,confirmed,{32,32,1},7,11,std::get<h::InteractionPreparedFrame>(op),omask);assert(orender.status==software::Status::ok);
  std::optional<Frame> lf;assert(store.copy_confirmed(lf)==FrameStatus::ok);assert(lf->pixels.size()==confirmed.bytes()&&std::equal(lf->pixels.begin(),lf->pixels.end(),confirmed.data()));
  assert(lrender.pixel_budget==orender.pixel_budget&&lrender.mask_bytes==orender.mask_bytes&&lrender.glyph_uses==orender.glyph_uses);
  assert(confirmed.version()==7&&confirmed.sequence()==11);
  {auto as=ls;for(auto& c:as.commands)if(auto* t=std::get_if<prep::Text>(&c))t->native->run=alias_run;auto ap=prep::prepare(as);assert(std::holds_alternative<prep::PreparedFrame>(ap));tt::AggregateBudget ra{512000000},ma{16*1024*1024};auto ar=tt::rasterize_document_budgeted(alias_outline,alias_binding->font(),phase,phase,ra,ma);assert(std::holds_alternative<tt::RasterOutput>(ar));const std::shared_ptr<const text::GlyphMaskA8> masks[]{std::get<tt::RasterOutput>(ar).mask};h::InteractionFrame af;auto r=h::render_legacy_accounted(budget,af,{32,32,1},1,1,std::get<prep::PreparedFrame>(ap),masks);assert(r.status==software::Status::ok&&af.bytes()==lf->pixels.size()&&std::equal(lf->pixels.begin(),lf->pixels.end(),af.data()));}

  {h::InteractionFrame bridge;auto result=h::render_legacy_accounted(budget,bridge,{32,32,1},3,4,std::get<prep::PreparedFrame>(lp),lmask);assert(result.status==software::Status::ok&&result.pixel_budget==lrender.pixel_budget&&result.mask_bytes==lrender.mask_bytes);
   assert(bridge.bytes()==lf->pixels.size()&&std::equal(lf->pixels.begin(),lf->pixels.end(),bridge.data()));const auto stable=budget.total().current;unsigned failures=0;
   for(long n=0;n<100;++n){attempts=0;fail_at=n;auto r=h::render_legacy_accounted(budget,bridge,{32,32,1},4,5,std::get<prep::PreparedFrame>(lp),lmask);fail_at=-1;if(r.status==software::Status::ok)break;assert(r.status==software::Status::allocation_failed);++failures;assert(bridge.version()==3&&bridge.sequence()==4&&budget.total().current==stable&&std::equal(lf->pixels.begin(),lf->pixels.end(),bridge.data()));}
   assert(failures>0&&bridge.version()==4&&bridge.sequence()==5&&budget.total().current==stable);
   {auto full=budget.reserve(h::OwnedCategory::temporary,budget.remaining());auto r=h::render_legacy_accounted(budget,bridge,{32,32,1},5,6,std::get<prep::PreparedFrame>(lp),lmask);assert(r.status==software::Status::limit_exceeded&&bridge.version()==4&&bridge.sequence()==5);}
  }

  // Fractional policy integral equivalence, independent integer-quarter oracle.
  {h::InteractionFrame integral;auto z=h::render_interaction_fractional_rect_accounted(budget,integral,{32,32,1},1,1,std::get<prep::PreparedFrame>(lp),lmask);assert(z.status==software::Status::ok&&z.pixel_budget==lrender.pixel_budget&&std::equal(lf->pixels.begin(),lf->pixels.end(),integral.data()));}
  for(int quarter=1;quarter<=5;++quarter){
   prep::Snapshot raw;raw.native_text_required=true;raw.commands={lt};auto rp=prep::prepare(raw);assert(std::holds_alternative<prep::PreparedFrame>(rp));h::InteractionFrame full;
   assert(h::render_legacy_accounted(budget,full,{32,32,1},1,1,std::get<prep::PreparedFrame>(rp),lmask).status==software::Status::ok);
   const prep::Rect clip=quarter<=3?prep::Rect{4.f+quarter/4.f,0.25f,8.5f,18.5f}:(quarter==4?prep::Rect{5.25f,5.25f,0.5f,0.5f}:prep::Rect{-0.25f,-0.75f,8.5f,18.5f});raw.commands={prep::PushClip{{-0.75f,-0.25f,32.5f,32.5f},{}},prep::PushClip{clip,{}},prep::PushClip{clip,{}},lt,prep::PopClip{},prep::PopClip{},prep::PopClip{}};auto clipped_prepared=prep::prepare(raw);assert(std::holds_alternative<prep::PreparedFrame>(clipped_prepared));auto& pf=std::get<prep::PreparedFrame>(clipped_prepared);h::InteractionFrame clipped;
   assert(h::render_legacy_accounted(budget,clipped,{32,32,1},1,1,pf,lmask).status==software::Status::unsupported);
   auto result=h::render_interaction_fractional_rect_accounted(budget,clipped,{32,32,1},2,3,pf,lmask);assert(result.status==software::Status::ok);
   for(int y=0;y<32;++y)for(int x=0;x<32;++x){const int dx=std::max(0,std::min(4*x+4,int((clip.x+clip.width)*4))-std::max(4*x,int(clip.x*4)));const int dy=std::max(0,std::min(4*y+4,int((clip.y+clip.height)*4))-std::max(4*y,int(clip.y*4)));for(int ch=0;ch<4;++ch){auto i=(y*32+x)*4+ch;const unsigned expected=(unsigned(full.data()[i])*unsigned(dx*dy)+8)/16;assert(clipped.data()[i]==expected);}}
   const auto stable=budget.total().current;const std::vector<uint8_t> saved(clipped.data(),clipped.data()+clipped.bytes());unsigned failures=0;
   for(long n=0;n<100;++n){attempts=0;fail_at=n;auto r=h::render_interaction_fractional_rect_accounted(budget,clipped,{32,32,1},4,5,pf,lmask);fail_at=-1;if(r.status==software::Status::ok)break;assert(r.status==software::Status::allocation_failed&&clipped.version()==2&&clipped.sequence()==3&&budget.total().current==stable&&std::equal(saved.begin(),saved.end(),clipped.data()));++failures;}assert(failures&&clipped.version()==4);
   {auto reserve=budget.reserve(h::OwnedCategory::temporary,budget.remaining());assert(h::render_interaction_fractional_rect_accounted(budget,clipped,{32,32,1},6,7,pf,lmask).status==software::Status::limit_exceeded);assert(clipped.version()==4&&std::equal(saved.begin(),saved.end(),clipped.data()));}
  }
  if(phase==0){ // Large explicit synthetic mask reaches the NEW logical work limit.
   auto big=std::make_shared<const text::GlyphMaskA8>(lr->font,l.mask->glyph_id,l.mask->flags,0,0,1000,1000,1000,0,0,std::vector<uint8_t>(1000000,255));const std::shared_ptr<const text::GlyphMaskA8> bm[]{big};prep::Snapshot snap;snap.native_text_required=true;snap.commands={prep::PushClip{{4.25f,16.25f,999.5f,999.5f},{}}};for(int i=0;i<6;++i)snap.commands.push_back(lt);snap.commands.push_back(prep::PopClip{});auto pp=prep::prepare(snap);assert(std::holds_alternative<prep::PreparedFrame>(pp));const auto old=confirmed.version(),seq=confirmed.sequence();const std::vector<uint8_t> saved(confirmed.data(),confirmed.data()+confirmed.bytes());const auto stable=budget.total().current;auto limited=h::render_interaction_fractional_rect_accounted(budget,confirmed,{1024,1024,1},99,99,std::get<prep::PreparedFrame>(pp),bm);assert(limited.status==software::Status::limit_exceeded&&confirmed.version()==old&&confirmed.sequence()==seq&&budget.total().current==stable&&std::equal(saved.begin(),saved.end(),confirmed.data()));
  }

  auto stable=budget.total().current;auto first=confirmed.data()[0];unsigned failures=0;
  struct Stop:prep::Cancellation{bool cancelled()noexcept override{return true;}} stop;
  auto cancelled=h::render_owned_a8(budget,confirmed,{32,32,1},8,12,std::get<h::InteractionPreparedFrame>(op),omask,&stop);assert(cancelled.status==software::Status::cancelled&&confirmed.version()==7&&budget.total().current==stable);
  for(long n=0;n<100;++n){attempts=0;fail_at=n;auto r=h::render_owned_a8(budget,confirmed,{32,32,1},8,12,std::get<h::InteractionPreparedFrame>(op),omask);fail_at=-1;if(r.status==software::Status::ok)break;assert(r.status==software::Status::allocation_failed);++failures;assert(budget.total().current==stable&&confirmed.version()==7&&confirmed.sequence()==11&&confirmed.data()[0]==first);}
  assert(failures>0&&confirmed.version()==8&&confirmed.sequence()==12);assert(budget.total().current==stable);
  {auto full=budget.reserve(h::OwnedCategory::temporary,budget.remaining());const auto charged=budget.total().current;auto r=h::render_owned_a8(budget,confirmed,{32,32,1},9,13,std::get<h::InteractionPreparedFrame>(op),omask);assert(r.status==software::Status::limit_exceeded&&confirmed.version()==8&&confirmed.sequence()==12&&budget.total().current==charged);}assert(budget.total().current==stable);
 }
 // Raster allocation failures discard all partial buffers and masks.
 const auto stable=budget.total().current;unsigned failures=0;
 for(long n=0;n<100;++n){tt::AggregateBudget a{512000000},m{16*1024*1024};attempts=0;fail_at=n;auto r=h::rasterize_owned_document(budget,oo,owned->binding->font,0,0,a,m);fail_at=-1;if(std::holds_alternative<h::OwnedRasterOutput>(r))break;assert(std::get<tt::RasterError>(r)==tt::RasterError::out_of_memory);++failures;assert(budget.total().current==stable);}
 assert(failures>0&&budget.total().current==stable);
 {auto full=budget.reserve(h::OwnedCategory::temporary,budget.remaining());const auto charged=budget.total().current;tt::AggregateBudget a{512000000},m{16*1024*1024};auto r=h::rasterize_owned_document(budget,oo,owned->binding->font,0,0,a,m);assert(std::get<tt::RasterError>(r)==tt::RasterError::budget_exceeded&&budget.total().current==charged);}assert(budget.total().current==stable);
 {tt::AggregateBudget a{512000000},m{16*1024*1024};auto invalid=h::rasterize_owned_document(budget,oo,owned->binding->font,64,0,a,m);assert(std::get<tt::RasterError>(invalid)==tt::RasterError::invalid_input);struct Stop:tt::Cancellation{bool cancelled()noexcept override{return true;}} stop;auto cancelled=h::rasterize_owned_document(budget,oo,owned->binding->font,0,0,a,m,&stop);assert(std::get<tt::RasterError>(cancelled)==tt::RasterError::cancelled);assert(budget.total().current==stable);}
 tt::AggregateBudget a{0},m{0};auto limited=h::rasterize_owned_document(budget,oo,owned->binding->font,0,0,a,m);assert(std::get<tt::RasterError>(limited)==tt::RasterError::budget_exceeded);assert(budget.total().current==stable);
 }
 assert(budget.total().current==initial);
 std::puts("owned raster/software PASS: shared pixels/work, BorderGroup/scopes, allocation failure and confirmed preservation");
}
