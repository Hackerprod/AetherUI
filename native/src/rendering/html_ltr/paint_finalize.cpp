#include "paint_finalize.h"
#include "raster_coordinate.h"
#include "session.h"
#include "../display_list_types.h"
#include <cstring>
#include <set>
#include <tuple>

namespace aether::rendering::html_ltr {
namespace {
[[noreturn]] void internal() { throw Error{AETHER_ERROR_INTERNAL}; }
[[noreturn]] void unsupported() { throw Error{AETHER_ERROR_UNSUPPORTED}; }
void reserve_items(FontProvider& provider,std::size_t n,std::size_t size) {
 if (size && n > FontProvider::payload_limit / size) throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
 provider.reserve_payload(n*size);
}
bool same(aether_rectf a,aether_rectf b) {
 return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;
}
bool same(aether_text_metrics_t a,aether_text_metrics_t b) {
 return a.width==b.width&&a.height==b.height&&a.baseline==b.baseline;
}
bool same(const text::Glyph& a,const text::Glyph& b) {
 return a.id==b.id&&a.cluster==b.cluster&&a.advance_x==b.advance_x&&a.advance_y==b.advance_y&&a.offset_x==b.offset_x&&a.offset_y==b.offset_y;
}
void rect(aether_rectf r) {
 // Generated NaN evidence is INTERNAL; legitimate accumulated overflow is a limit.
 absolute_coordinate(r.x);absolute_coordinate(r.y);
 logical_extent(r.width);logical_extent(r.height);
 absolute_coordinate(double(r.x)+r.width);absolute_coordinate(double(r.y)+r.height);
}
void square(aether_corner_radii r) {
 if(r.top_left_x!=0||r.top_left_y!=0||r.top_right_x!=0||r.top_right_y!=0||
    r.bottom_right_x!=0||r.bottom_right_y!=0||r.bottom_left_x!=0||r.bottom_left_y!=0) unsupported();
}
void opaque(aether_color_rgba c) {
 if(!std::isfinite(c.r)||!std::isfinite(c.g)||!std::isfinite(c.b)||!std::isfinite(c.a))internal();
 if(c.r<0||c.r>1||c.g<0||c.g>1||c.b<0||c.b>1||c.a!=1)unsupported();
}
void product_matches(const FinalText& final,const text::ShapedRun* key) {
 if(!final.original||final.original.get()!=key||!final.product||!final.product->binding)internal();
 const auto& run=*final.original;const auto& product=*final.product;
 if(run.paragraph||run.font!=product.binding->font()||run.source!=product.source||
    !same(run.metrics,product.metrics)||run.glyphs.size()!=product.glyphs.size()||
    product.prefixes.size()!=product.glyphs.size()+1||product.prefixes.front()!=0)internal();
 validate_run_transport(run.source,run.glyphs,run.metrics.width,run.metrics.height,run.metrics.baseline);
 rect(final.rect);
 if(final.rect.width!=run.metrics.width||final.rect.height!=run.metrics.height)internal();
 const double scale=double(run.font->size_26_6)/64/product.binding->face()->units_per_em();
 if(float(scaled_design_prefix(product.prefixes.back(),scale))!=product.metrics.width)internal();
 for(std::size_t i=0;i<run.glyphs.size();++i) {
  if(!same(run.glyphs[i],product.glyphs[i])||run.glyphs[i].advance_y||run.glyphs[i].offset_x||run.glyphs[i].offset_y||
     product.prefixes[i+1]<product.prefixes[i])internal();
  const double advance=scaled_design_prefix(product.prefixes[i+1]-product.prefixes[i],scale);
  if(run.glyphs[i].advance_x!=quantize_absolute(advance))internal();
 }
}
void validate_controls(const NativePaintList& native,FontProvider& provider) {
 const auto* list=NativeListAccess::list(native);if(!list)internal();
 if(!native.scroll.empty())unsupported();
 if(!list->border_groups.empty())unsupported(); // nonuniform emitter, outside profile
 if(list->native_gradient_unproven||!list->native_srgb_gradients.empty()||list->native_gradient_stop_count)unsupported();
 reserve_items(provider,list->commands.size(),sizeof(std::pair<int,std::int64_t>));
 std::vector<std::pair<int,std::int64_t>> controls;
 for(const auto& command:list->commands) {
  switch(command.tag) {
  case AETHER_PAINT_FILL_ROUNDED_RECT:
   rect(command.data.fill_rounded_rect.rect);square(command.data.fill_rounded_rect.radii);opaque(command.data.fill_rounded_rect.color);break;
  case AETHER_PAINT_STROKE_ROUNDED_RECT: {
   const auto& s=command.data.stroke_rounded_rect;rect(s.rect);square(s.radii);opaque(s.color);
   if(!std::isfinite(s.stroke_width))internal();
   if(s.stroke_width<0||s.stroke_width>128)unsupported();
   break;
  }
  case AETHER_PAINT_DRAW_TEXT:break; // complete identity check below
  case AETHER_PAINT_PUSH_CLIP:
   rect(command.data.push_clip.rect);square(command.data.push_clip.radii);controls.emplace_back(AETHER_PAINT_PUSH_CLIP,0);break;
  case AETHER_PAINT_POP_CLIP:
   if(controls.empty()||controls.back().first!=AETHER_PAINT_PUSH_CLIP)internal();
   controls.pop_back();break;
  case AETHER_PAINT_PUSH_TRANSFORM: {
   const auto& m=command.data.push_transform.matrix;
   if(m.m11!=1||m.m12!=0||m.m21!=0||m.m22!=1||m.dx!=0||m.dy!=0)unsupported();
   controls.emplace_back(AETHER_PAINT_PUSH_TRANSFORM,0);break;
  }
  case AETHER_PAINT_POP_TRANSFORM:
   if(controls.empty()||controls.back().first!=AETHER_PAINT_PUSH_TRANSFORM)internal();
   controls.pop_back();break;
  case AETHER_PAINT_BEGIN_PAINT_CHUNK:
   if(command.data.paint_chunk.style_node_id<=0)internal();
   controls.emplace_back(AETHER_PAINT_BEGIN_PAINT_CHUNK,command.data.paint_chunk.style_node_id);break;
  case AETHER_PAINT_END_PAINT_CHUNK:
   if(controls.empty()||controls.back()!=std::pair<int,std::int64_t>{AETHER_PAINT_BEGIN_PAINT_CHUNK,command.data.paint_chunk.style_node_id})internal();
   controls.pop_back();break;
  default:unsupported();
  }
 }
 if(!controls.empty())internal();
 reserve_items(provider,native.events.size(),sizeof(std::uint64_t)*3);
 std::vector<std::uint64_t> stack;std::set<std::uint64_t> ids;
 std::size_t previous=0;
 for(const auto& e:native.events) {
  if(e.boundary<previous||e.boundary>list->commands.size()||!e.id)internal();
  previous=e.boundary;
  if(e.opacity!=1)unsupported();
  if(e.open) {
   if(e.parent!=(stack.empty()?0:stack.back())||!ids.insert(e.id).second)internal();
   stack.push_back(e.id);
  } else {
   if(stack.empty()||stack.back()!=e.id)internal();
   stack.pop_back();
   if(e.parent!=(stack.empty()?0:stack.back()))internal();
  }
 }
 if(!stack.empty())internal();
}
struct MaskKey {
 const text::FontInstance* font;std::uint32_t glyph,flags;std::uint8_t x,y;
 bool operator<(const MaskKey& rhs)const noexcept {
  if(font!=rhs.font)return std::less<const text::FontInstance*>{}(font,rhs.font);
  return std::tie(glyph,flags,x,y)<std::tie(rhs.glyph,rhs.flags,rhs.x,rhs.y);
 }
};
// Private validation seam: also exercised with a deliberately altered phase in
// the focal TU. A raster result never supplies authority to change the key.
void validate_generated_mask(const std::shared_ptr<const text::GlyphMaskA8>& mask,
 const std::shared_ptr<const text::FontInstance>& font,const MaskKey& key) {
 if(!mask||mask->font!=font||mask->glyph_id!=key.glyph||mask->flags!=key.flags||mask->phase_x!=key.x||mask->phase_y!=key.y)internal();
}
}
std::vector<std::shared_ptr<const text::GlyphMaskA8>> finalize_paint(
 NativePaintList& native,const FinalTexts& finals,FontProvider& provider) {
 validate_controls(native,provider);
 auto* list=NativeListAccess::list(native);
 reserve_items(provider,finals.size(),sizeof(std::pair<const text::ShapedRun*,bool>));
 std::set<const text::ShapedRun*> expected;
 std::size_t placed=0;
 for(const auto& [key,final]:finals) {
  product_matches(final,key);
  reserve_count(placed,final.original->glyphs.size(),16384);
  // Builder explicitly omits empty boxes, but never viewport-clipped nonempty
  // text. Account for those omitted zero-area fragments rather than weakening
  // coverage to only the entries that happened to survive.
  if(final.rect.width>0&&final.rect.height>0)expected.insert(key);
 }
 if(native.text_runs.size()!=expected.size())internal();
 reserve_items(provider,list->commands.size(),sizeof(aether_paint_command_t));
 reserve_items(provider,native.text_runs.size(),sizeof(NativeTextEntry));
 HTML_LTR_STAGE(staging);
 auto commands=list->commands; // shallow staging only; original owns text strings
 auto entries=native.text_runs;
 std::vector<std::shared_ptr<const text::GlyphMaskA8>> masks;
 std::set<MaskKey> mask_keys;
 std::size_t entry_index=0;
 for(std::size_t i=0;i<commands.size();++i) {
  if(commands[i].tag!=AETHER_PAINT_DRAW_TEXT)continue;
  if(entry_index>=entries.size())internal();
  auto& entry=entries[entry_index++];
  auto original=entry.placement.run;
  if(entry.command_index!=i||!original||!expected.erase(original.get()))internal();
  const auto& final=finals.at(original.get());const auto& product=*final.product;
  auto& t=commands[i].data.draw_text;
  if(!t.text||std::string_view(t.text)!=original->source||!same(t.rect,final.rect)||
     !std::isfinite(entry.placement.baseline)||
     entry.placement.baseline!=final.rect.y+original->metrics.baseline||
     t.font_size!=float(original->font->size_26_6)/64.f||
     std::string_view(t.font_family,strnlen(t.font_family,sizeof(t.font_family)))!="AetherLTR"||
     (t.font_weight!=400&&t.font_weight!=700)||t.text_align!=AETHER_TEXT_ALIGN_START||t.wrap)internal();
  if(original->font->source!=(t.font_weight==700?provider.bold_source():provider.regular_source()))internal();
  opaque(t.color);
  const double scale=double(original->font->size_26_6)/64/product.binding->face()->units_per_em();
  const auto qx=quantize_absolute(t.rect.x),qy=quantize_absolute(entry.placement.baseline);
  reserve_items(provider,original->glyphs.size(),sizeof(text::Glyph));
  reserve_items(provider,original->source.size(),sizeof(char));
  provider.reserve_payload(sizeof(text::ShapedRun));
  auto glyphs=original->glyphs;
  for(std::size_t j=0;j<glyphs.size();++j) {
   const auto current=quantize_endpoint(final.rect.x,scaled_design_prefix(product.prefixes[j],scale));
   const auto next=quantize_endpoint(final.rect.x,scaled_design_prefix(product.prefixes[j+1],scale));
   glyphs[j].advance_x=endpoint_delta(current,next);
   const MaskKey key{original->font.get(),glyphs[j].id,original->font->raster_flags,raster_phase(current),raster_phase(qy)};
   if(!mask_keys.contains(key)) {
    if(mask_keys.empty()) HTML_LTR_STAGE(masks);
    reserve_items(provider,1,sizeof(MaskKey)+sizeof(std::shared_ptr<const text::GlyphMaskA8>)+sizeof(text::GlyphMaskA8));
    auto outline=provider.outline(product.binding,glyphs[j].id);
    // The raster routine debits mask bytes before allocation. Lend it the
    // smaller remaining allowance of BOTH budgets so A8 allocation cannot
    // temporarily bypass the aggregate session-payload quota. Its noexcept
    // return lets us record any debit even on OOM without a refund.
    const auto mask_remaining=provider.mask_budget.remaining;
    const auto payload_remaining=FontProvider::payload_limit-provider.usage().payload;
    const auto allowance=std::min<std::uint64_t>(mask_remaining,payload_remaining);
    tt::AggregateBudget local_mask_budget{allowance};
    auto result=tt::rasterize_document_budgeted(outline,original->font,key.x,key.y,provider.raster_budget,local_mask_budget);
    if(local_mask_budget.remaining>allowance)internal();
    const auto charged=allowance-local_mask_budget.remaining;
    provider.mask_budget.remaining=mask_remaining-charged;
    provider.reserve_payload(static_cast<std::size_t>(charged));
    if(auto* error=std::get_if<tt::RasterError>(&result)) {
     if(*error==tt::RasterError::budget_exceeded)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
     if(*error==tt::RasterError::out_of_memory)throw Error{AETHER_ERROR_OUT_OF_MEMORY};
     internal();
    }
    auto mask=std::get<tt::RasterOutput>(result).mask;
    validate_generated_mask(mask,original->font,key);
    mask_keys.insert(key);masks.push_back(std::move(mask));
   }
  }
  validate_run_transport(original->source,glyphs,original->metrics.width,original->metrics.height,original->metrics.baseline);
  entry.placement.run=std::make_shared<const text::ShapedRun>(original->font,original->source,std::move(glyphs),original->metrics);
  // Only staging paint geometry changes. Logical boxes/metrics stay untouched.
  t.rect.x=float(qx)/64;
  entry.placement.baseline=float(qy)/64;
  t.rect.y=entry.placement.baseline-original->metrics.baseline;
  if(t.rect.y+original->metrics.baseline!=entry.placement.baseline)internal();
  rect(t.rect);
 }
 if(entry_index!=entries.size()||!expected.empty())internal();
 // All allocations and checks finished; no throwable operation after publishing
 // this staging pair. NativePaintList retains ownership of the same C strings.
 list->commands.swap(commands);native.text_runs.swap(entries);
 return masks;
}
} // namespace aether::rendering::html_ltr
