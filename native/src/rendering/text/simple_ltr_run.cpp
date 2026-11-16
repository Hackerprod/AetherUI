#include "simple_ltr_run.h"
#include <algorithm>
#include <limits>
#include <new>
namespace aether::rendering::text::truetype {
namespace {
RunError convert(Error e) { switch(e){case Error::malformed:return RunError::malformed;case Error::unsupported:return RunError::unsupported;case Error::limit_exceeded:return RunError::limit_exceeded;case Error::cancelled:return RunError::cancelled;default:return RunError::out_of_memory;} }
RunError convert(CmapError e) { switch(e){case CmapError::invalid_input:return RunError::invalid_input;case CmapError::malformed:return RunError::malformed;case CmapError::unsupported:return RunError::unsupported;case CmapError::limit_exceeded:return RunError::limit_exceeded;case CmapError::cancelled:return RunError::cancelled;default:return RunError::out_of_memory;} }
RunError convert(RasterError e) { switch(e){case RasterError::invalid_input:return RunError::invalid_input;case RasterError::unsupported:return RunError::unsupported;case RasterError::numeric_failure:return RunError::numeric_failure;case RasterError::budget_exceeded:return RunError::limit_exceeded;case RasterError::cancelled:return RunError::cancelled;default:return RunError::out_of_memory;} }
bool add(std::uint64_t& a,std::uint64_t b){if(b>std::numeric_limits<std::uint64_t>::max()-a)return false;a+=b;return true;}
}
struct SimpleRunBuilder {
static RunResult build(std::shared_ptr<const RealFontBinding> binding,std::string_view text,LineBox box,Cancellation* cancel,RunWork* out,RunBudget* shared) noexcept {
 RunWork local{}; RunWork& work=out?*out:local; work={};
 auto reserve=[](AggregateBudget& b,std::uint64_t n){if(n>b.remaining)return false;b.remaining-=n;return true;};
 auto cancelled=[&](){++work.cancel_checks;return cancel&&cancel->cancelled();};
 try {
  if(cancelled())return RunError::cancelled;
  if(!binding||box.height26_6<=0||box.height26_6>8192*64||box.baseline26_6<0||box.baseline26_6>box.height26_6)return RunError::invalid_input;
  if(text.size()>4096)return RunError::limit_exceeded;
  // Decode the complete input before interpreting its supported repertoire.
  std::vector<std::pair<std::uint32_t,std::size_t>> scalars;
  for(std::size_t i=0;i<text.size();){
   if(cancelled())return RunError::cancelled;
   auto start=i;auto c=static_cast<unsigned char>(text[i++]);std::uint32_t cp=c;unsigned n=0;std::uint32_t min=0;
   if(c<128){}else if(c>=0xc2&&c<=0xdf){n=1;cp=c&31;min=128;}else if(c>=0xe0&&c<=0xef){n=2;cp=c&15;min=2048;}else if(c>=0xf0&&c<=0xf4){n=3;cp=c&7;min=65536;}else return RunError::invalid_input;
   if(n>text.size()-i)return RunError::invalid_input;
   for(unsigned j=0;j<n;++j){auto b=static_cast<unsigned char>(text[i++]);if((b&192)!=128)return RunError::invalid_input;cp=(cp<<6)|(b&63);}
   if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return RunError::invalid_input;
   scalars.emplace_back(cp,start);++work.decoded;
  }
  if(scalars.size()>256)return RunError::limit_exceeded;
  for(auto [cp,cluster]:scalars){(void)cluster;if(!((cp>=32&&cp<=126)||(cp>=160&&cp<=255&&cp!=173)))return RunError::unsupported;}
  std::vector<RunGlyph> glyphs;glyphs.reserve(scalars.size());std::int64_t pen=0;
  AggregateBudget budget{512000000};bool has_bounds=false;std::int64_t left=0,top=0,right=0,bottom=0;
  for(auto [cp,cluster]:scalars){
   if(cancelled())return RunError::cancelled;
   if(shared){if(!reserve(shared->cmap,17))return RunError::limit_exceeded;work.cmap_reserved+=17;}
   CmapWork cw{};auto lookup=binding->map()->lookup(cp,&cw);++work.lookups;if(!add(work.lookup_comparisons,cw.comparisons))return RunError::limit_exceeded;
   if(auto e=std::get_if<CmapError>(&lookup))return convert(*e);
   if(std::holds_alternative<Missing>(lookup))return RunError::missing_glyph;
   auto id=std::get<Mapped>(lookup).glyph_id;
   if(shared&&!reserve(shared->outline,1000000))return RunError::limit_exceeded;
   work.outline_reserved+=1000000;Work ow{};auto outline=binding->face()->outline(id,cancel,&ow);work.outline_units+=ow.units;
   if(auto e=std::get_if<Error>(&outline))return convert(*e);
   if(cancelled())return RunError::cancelled;
   RasterWork rw{};auto raster=shared?rasterize_budgeted(std::get<std::shared_ptr<const Outline>>(outline),binding->font(),unsigned(pen%64),unsigned(box.baseline26_6%64),shared->raster,shared->masks,cancel,&rw):rasterize(std::get<std::shared_ptr<const Outline>>(outline),binding->font(),unsigned(pen%64),unsigned(box.baseline26_6%64),cancel,&rw,&budget);
   work.mask_reserved+=rw.mask_charged;
   work.raster_reserved+=rw.aggregate_charged;work.raster_units+=rw.units;
   if(auto e=std::get_if<RasterError>(&raster))return convert(*e);
   auto r=std::get<RasterOutput>(raster);auto mask=r.mask;
   const auto& shape=std::get<std::shared_ptr<const Outline>>(outline);
   const std::int64_t numerator=std::int64_t(shape->metrics.advance)*binding->font()->size_26_6;
   const std::int64_t units=shape->units_per_em();
   const std::int64_t advance=(numerator+units/2)/units;
   if(advance!=r.advance_26_6)return RunError::numeric_failure;
   if(advance<0||pen>65536LL*64-advance)return RunError::limit_exceeded;
   if(mask->bytes.size()>64ULL*1024*1024-work.retained_mask_bytes)return RunError::limit_exceeded;
   work.retained_mask_bytes+=mask->bytes.size();
   const auto x=pen/64+mask->bearing_left,y=box.baseline26_6/64-mask->bearing_top;
   if(mask->width&&mask->height){if(!has_bounds){left=x;top=y;right=x+mask->width;bottom=y+mask->height;has_bounds=true;}else{left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+mask->width);bottom=std::max(bottom,y+mask->height);}}
   glyphs.push_back({id,cluster,advance,pen,x,y,std::move(mask)});pen+=advance;
  }
  RunBounds bounds{};if(has_bounds)bounds={left,top,right-left,bottom-top};
  left=std::min<std::int64_t>(0,left)-4;top=std::min<std::int64_t>(0,top)-4;
  right=std::max<std::int64_t>(std::max<std::int64_t>(1,(pen+63)/64),right)+4;bottom=std::max(bottom,(box.height26_6+63)/64)+4;
  auto width=right-left,height=bottom-top;
  if(width>8192||height>8192||width*height>16LL*1024*1024)return RunError::limit_exceeded;
  std::uint64_t visits=0;for(const auto& g:glyphs){visits+=std::uint64_t(g.mask->width)*g.mask->height;if(visits>16ULL*1024*1024)return RunError::limit_exceeded;}
  if(cancelled())return RunError::cancelled;
  if(shared){
   // Both dimensions are bounded above by8192 before this unsigned product.
   const auto pixels=std::uint64_t(width)*std::uint64_t(height);
   if(!reserve(shared->canvas_pixels,pixels))return RunError::limit_exceeded;
   work.canvas_reserved=pixels;
   if(!reserve(shared->composite_visits,visits))return RunError::limit_exceeded;
   work.composite_reserved=visits;
  }
  RunCanvas canvas{left,top,std::uint32_t(width),std::uint32_t(height),{}};work.composition_started=true;
  // Capacity allocation is synchronous; explicit clear/composite/convert loops poll.
  auto count=std::size_t(width*height);std::unique_ptr<std::uint8_t[]> alpha(new std::uint8_t[count]);
  for(std::size_t i=0;i<count;++i){if(i%256==0&&cancelled())return RunError::cancelled;alpha[i]=0;++work.clear_pixels;}
  for(const auto& g:glyphs)for(std::uint32_t y=0;y<g.mask->height;++y)for(std::uint32_t x=0;x<g.mask->width;++x){
   if(work.source_pixel_visits%256==0&&cancelled())return RunError::cancelled;
   auto index=std::size_t(g.bitmap_y-top+y)*std::size_t(width)+std::size_t(g.bitmap_x-left+x);
   unsigned a=alpha[index],b=g.mask->bytes[std::size_t(y)*g.mask->stride+x];alpha[index]=std::uint8_t(a+b-(a*b+127)/255);++work.source_pixel_visits;
  }
  canvas.rgba.resize(count*4);
  for(std::size_t i=0;i<count;++i){if(i%256==0&&cancelled())return RunError::cancelled;canvas.rgba[i*4+3]=alpha[i];++work.conversion_pixels;}
  if(cancelled())return RunError::cancelled;
  auto result=std::shared_ptr<const PreparedSimpleRun>(new PreparedSimpleRun(std::move(binding),std::string(text),std::move(glyphs),box,pen,bounds,std::move(canvas)));
  if(cancelled())return RunError::cancelled;
  work.canvas_complete=true;return result;
 }catch(const std::bad_alloc&){return RunError::out_of_memory;}catch(...){return RunError::numeric_failure;}
}
};
RunResult build_simple_ltr_run(std::shared_ptr<const RealFontBinding> b,std::string_view t,LineBox l,Cancellation* c,RunWork* w) noexcept {
 return SimpleRunBuilder::build(std::move(b),t,l,c,w,nullptr);
}
RunResult build_simple_ltr_run_budgeted(std::shared_ptr<const RealFontBinding> b,std::string_view t,LineBox l,RunBudget& budget,Cancellation* c,RunWork* w) noexcept {
 return SimpleRunBuilder::build(std::move(b),t,l,c,w,&budget);
}
}
