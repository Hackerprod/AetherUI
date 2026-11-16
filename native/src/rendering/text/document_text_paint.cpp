#include "document_text_paint.h"
#include "selected_text_product.h"
#include "../../layout/layout_types.h"
#include "../../layout/native_text_transaction.h"
#include "../display_list_types.h"
#include <map>
#include <set>
#include <type_traits>
namespace aether::rendering::text {
namespace {
using truetype::RunError;
using aether::layout::DocumentTextAccess;
using aether::layout::DocumentTextBudget;
bool rect_equal(aether_rectf a,aether_rectf b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
bool color_equal(aether_color_rgba a,aether_color_rgba b){return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;}
[[noreturn]] void raster_error(truetype::RasterError e){switch(e){case truetype::RasterError::out_of_memory:throw std::bad_alloc();case truetype::RasterError::cancelled:throw aether::layout::DocumentTextCancelled{};case truetype::RasterError::budget_exceeded:throw RunError::limit_exceeded;case truetype::RasterError::numeric_failure:throw RunError::numeric_failure;default:throw RunError::unsupported;}}
}
DocumentPaintProduct DocumentTextPainter::finalize(aether_layout_engine_s* engine,aether_layout_result_s* layout,NativePaintList& list,
 DocumentTextProvider& provider){
 const auto operation=DocumentTextAccess::paint_token(engine,layout,&provider);
 auto& budget=DocumentTextAccess::paint_budget(engine,layout,&provider);
 budget.bytes(1,sizeof(aether::layout::LayoutOperationPin));aether::layout::LayoutOperationPin pin(layout);
 auto check=[&]{provider.check();if(DocumentTextAccess::paint_token(engine,layout,&provider)!=operation)throw RunError::invalid_input;};
 check();auto* native=NativeListAccess::list(list);if(!native)throw RunError::invalid_input;
 using BoxMap=std::map<const ShapedRun*,const aether_layout_box_s*>;BoxMap boxes;
 std::size_t expected_commands=0;std::set<const aether_layout_box_s*> visited;
 budget.bytes(1,sizeof(aether_layout_box_s*));std::vector<const aether_layout_box_s*> pending;if(layout->root)pending.push_back(layout->root);
 // The registry includes owned text boxes, even if hidden. Only commands actually
 // emitted by the existing painter become placed glyph occurrences.
 while(!pending.empty()){check();const auto* box=pending.back();pending.pop_back();
  budget.bytes(1,sizeof(aether_layout_box_s*)+4*sizeof(void*));if(!box||!visited.insert(box).second)throw RunError::invalid_input;
  for(const auto* child:box->children){check();budget.bytes(1,sizeof(aether_layout_box_s*));pending.push_back(child);}
  if(box->kind!=LAYOUT_BOX_TEXT)continue;
  if(box->style.visible&&!box->text.empty())++expected_commands;
  if(!box->native_text.run||box->text.empty())throw RunError::invalid_input;
  auto occurrence=provider.occurrences_.find(box->native_text.run.get());if(occurrence==provider.occurrences_.end())throw RunError::invalid_input;
  const auto& fragment=*occurrence->second.fragment;
  if(fragment.operation_!=operation||!fragment.layout_owner_||!fragment.layout_owner_->alive||!fragment.layout_owner_->attached||fragment.layout_owner_->owner!=box->element)throw RunError::invalid_input;
  if(fragment.style_.style_owner()!=box->style.style_owner()||fragment.style_.measures_owner()!=box->style.measures_owner()||!aether::layout::native_text_detail::scalars_equal(fragment.style_.scalars(),box->style.scalars())||box->text!=fragment.normalized||box->native_text.baseline!=box->native_text.run->metrics.baseline)throw RunError::invalid_input;
  budget.bytes(1,sizeof(BoxMap::value_type)+4*sizeof(void*));if(!boxes.emplace(box->native_text.run.get(),box).second)throw RunError::invalid_input;
 }
 // Staging commands are NON-OWNING copies of the C records. Their destructor
 // frees no payload; NativePaintList retains the single owner of string/gradient
 // allocations. Swapping the vector preserves those allocation identities.
 static_assert(std::is_trivially_copyable_v<aether_paint_command_t>);
 static_assert(std::is_trivially_destructible_v<aether_paint_command_t>);
 DocumentPaintProduct output;std::set<std::size_t> indices;std::set<const ShapedRun*> consumed_runs;
 budget.bytes(native->commands.size(),sizeof(aether_paint_command_t));std::vector<aether_paint_command_t> commands_copy;commands_copy.reserve(native->commands.size());
 for(const auto& command:native->commands){check();commands_copy.push_back(command);}
 budget.bytes(list.text_runs.size(),sizeof(NativeTextEntry));std::vector<NativeTextEntry> entries_copy;entries_copy.reserve(list.text_runs.size());
 for(const auto& entry:list.text_runs){check();entries_copy.push_back(entry);}
 using MaskKey=std::tuple<const FontInstance*,std::uint32_t,std::uint32_t,unsigned,unsigned>;
 using Masks=std::map<MaskKey,std::shared_ptr<const GlyphMaskA8>>;Masks masks;
 if(list.text_runs.size()>65536)throw RunError::limit_exceeded;
 if(list.text_runs.size()!=expected_commands)throw RunError::invalid_input;
 budget.bytes(std::min(list.text_runs.size(),std::size_t{128}),sizeof(DocumentPaintSample));output.samples.reserve(std::min(list.text_runs.size(),std::size_t{128}));
 for(std::size_t index=0;index<list.text_runs.size();++index){check();const auto& entry=list.text_runs[index];
  if(!entry.placement.run||entry.command_index>=native->commands.size())throw RunError::invalid_input;
  budget.bytes(1,sizeof(std::size_t)+4*sizeof(void*));if(!indices.insert(entry.command_index).second)throw RunError::invalid_input;
  const auto found=provider.occurrences_.find(entry.placement.run.get());const auto box_it=boxes.find(entry.placement.run.get());
  if(found==provider.occurrences_.end()||box_it==boxes.end())throw RunError::invalid_input;
  budget.bytes(1,sizeof(const ShapedRun*)+4*sizeof(void*));if(!consumed_runs.insert(entry.placement.run.get()).second)throw RunError::invalid_input;
  if(!box_it->second->style.visible||box_it->second->text.empty())throw RunError::invalid_input;
  const auto& occurrence=found->second;const auto& logical=*occurrence.logical;const auto& fragment=*occurrence.fragment;const auto* box=box_it->second;
  const auto& command=native->commands[entry.command_index];if(command.tag!=AETHER_PAINT_DRAW_TEXT)throw RunError::invalid_input;const auto& text=command.data.draw_text;
  if(!text.text||entry.placement.run!=occurrence.measured||entry.placement.run->font!=logical.binding->font()||logical.source!=fragment.normalized||entry.placement.run->source!=logical.source||logical.glyphs.empty()||logical.glyphs.size()!=occurrence.measured->glyphs.size()||occurrence.measured->metrics.width!=float(logical.width)||occurrence.measured->metrics.height!=logical.height||occurrence.measured->metrics.baseline!=logical.baseline||!rect_equal(text.rect,box->content_box)||entry.placement.baseline!=box->content_box.y+logical.baseline||text.font_size!=fragment.style_.font_size||text.font_weight!=fragment.style_.font_weight||!color_equal(text.color,fragment.style_.color)||text.wrap||text.text_align!=AETHER_TEXT_ALIGN_START||std::strncmp(text.font_family,fragment.style_.font_family.data(),sizeof(text.font_family))!=0)throw RunError::invalid_input;
  for(std::size_t byte=0;byte<logical.source.size();++byte){if((byte&255)==0)check();if(text.text[byte]=='\0'||text.text[byte]!=logical.source[byte])throw RunError::invalid_input;}if(text.text[logical.source.size()]!='\0')throw RunError::invalid_input;
  budget.placed(logical.glyphs.size());budget.bytes(logical.glyphs.size(),sizeof(Glyph));std::vector<Glyph> glyphs;glyphs.reserve(logical.glyphs.size());
  const auto origin=selected_raster_coordinate(double(box->content_box.x));const auto baseline=selected_raster_coordinate(double(entry.placement.baseline));
  std::int64_t accumulated=origin.value26_6;std::int64_t end=origin.value26_6;
  for(std::size_t i=0;i<logical.glyphs.size();++i){check();const auto& glyph=logical.glyphs[i];
   if(glyph.cluster>=logical.source.size()||occurrence.measured->glyphs[i].id!=glyph.glyph||occurrence.measured->glyphs[i].cluster!=glyph.cluster||!glyph.outline||glyph.outline->source()!=logical.binding->source()||glyph.outline->glyph_id!=glyph.glyph||glyph.outline->units_per_em()!=logical.binding->face()->units_per_em())throw RunError::invalid_input;
   const auto position=selected_raster_coordinate(double(box->content_box.x)+glyph.prefix);
   const double next_prefix=i+1<logical.glyphs.size()?logical.glyphs[i+1].prefix:logical.width;
   const auto next=selected_raster_coordinate(double(box->content_box.x)+next_prefix);const auto delta=std::int64_t(next.value26_6)-position.value26_6;
   if(position.value26_6!=accumulated||delta<0||delta>65536LL*64||std::int64_t(position.value26_6)+delta!=next.value26_6)throw RunError::numeric_failure;
   accumulated=next.value26_6;end=next.value26_6;glyphs.push_back({glyph.glyph,glyph.cluster,std::int32_t(delta),0,0,0});
   MaskKey key{logical.binding->font().get(),glyph.glyph,logical.binding->font()->raster_flags,position.phase,baseline.phase};
   if(masks.find(key)!=masks.end()){++output.work.mask_hits;continue;}
   budget.bytes(1,sizeof(Masks::value_type)+4*sizeof(void*)+sizeof(std::shared_ptr<const GlyphMaskA8>));truetype::RasterWork raster_work;
   auto raster=truetype::rasterize_document_budgeted(glyph.outline,logical.binding->font(),position.phase,baseline.phase,provider.font_budget.raster,provider.font_budget.masks,provider.cancel_,&raster_work);check();
   if(auto* error=std::get_if<truetype::RasterError>(&raster))raster_error(*error);
   auto mask=std::get<truetype::RasterOutput>(std::move(raster)).mask;
   if(!mask||mask->font!=logical.binding->font()||mask->glyph_id!=glyph.glyph||mask->flags!=logical.binding->font()->raster_flags||mask->phase_x!=position.phase||mask->phase_y!=baseline.phase)throw RunError::invalid_input;
   masks.emplace(key,mask);output.masks.push_back(mask);++output.work.mask_misses;output.work.mask_bytes+=mask->bytes.size();output.work.raster_work+=raster_work.units;output.work.raster_reserved+=raster_work.aggregate_charged;output.work.edge_scan_pairs+=raster_work.edge_scan_pairs;output.work.active_pairs+=raster_work.active_pairs;output.work.prepass_work+=raster_work.prepass_work;output.work.prepass_reserved+=raster_work.prepass_reserved;output.work.main_reserved+=raster_work.main_reserved;
  }
  const auto endpoint=selected_raster_coordinate(double(box->content_box.x)+logical.width);if(end!=endpoint.value26_6||accumulated-origin.value26_6!=std::int64_t(endpoint.value26_6)-origin.value26_6)throw RunError::numeric_failure;
  budget.bytes(1,sizeof(ShapedRun)+2*sizeof(void*));budget.bytes(logical.source.size());
  auto painted=std::make_shared<const ShapedRun>(logical.binding->font(),logical.source,std::move(glyphs),occurrence.measured->metrics);
  const float y=float(baseline.value26_6)/64.f-logical.baseline;if(!std::isfinite(y)||std::abs(y)>65536||y+logical.baseline!=float(baseline.value26_6)/64.f)throw RunError::numeric_failure;
  auto& staged_command=commands_copy[entry.command_index].data.draw_text;staged_command.rect.x=float(origin.value26_6)/64.f;staged_command.rect.y=y;
  entries_copy[index].placement.run=std::move(painted);entries_copy[index].placement.baseline=float(baseline.value26_6)/64.f;
  ++output.work.commands;output.work.placed_glyphs+=logical.glyphs.size();
  if(output.samples.size()<128)output.samples.push_back({fragment.occurrence,entry.command_index,logical.glyphs.size(),box->content_box,entry.placement.baseline,float(baseline.value26_6)/64.f,origin.value26_6,end,baseline.value26_6});
 }
 for(const auto& [run,box]:boxes){check();if(box->style.visible&&!box->text.empty()&&!consumed_runs.count(run))throw RunError::invalid_input;}
 for(std::size_t i=0;i<native->commands.size();++i){if((i&255)==0)check();if(native->commands[i].tag==AETHER_PAINT_DRAW_TEXT&&indices.find(i)==indices.end())throw RunError::invalid_input;}
 // Last callback/epoch check after every allocation and before the no-throw commit.
 check();
 native->commands.swap(commands_copy);list.text_runs.swap(entries_copy);
 return output;
}
DocumentPaintProduct DocumentTextPainter::finalize_drawing(aether_layout_engine_s* engine,aether_layout_result_s* layout,
 NativePaintList& list,DocumentTextProvider& provider){
 const auto operation=DocumentTextAccess::drawing_paint_token(engine,layout,&provider);
 auto& budget=DocumentTextAccess::drawing_paint_budget(engine,layout,&provider);
 budget.bytes(1,sizeof(aether::layout::LayoutOperationPin));aether::layout::LayoutOperationPin pin(layout);
 auto check=[&]{provider.check();if(DocumentTextAccess::drawing_paint_token(engine,layout,&provider)!=operation)throw RunError::invalid_input;};
 check();const auto product=DocumentTextAccess::drawing_product(engine,layout,&provider);
 auto* native=NativeListAccess::list(list);if(!native||!product)throw RunError::invalid_input;
 using Binding=aether::layout::DocumentFinalizationProduct::Binding;
 using Boxes=std::map<const aether_layout_box_s*,const Binding*>;Boxes boxes;
 budget.bytes(product->bindings_.size(),sizeof(Boxes::value_type)+4*sizeof(void*));
 auto lookup_work=[&](std::size_t size){std::uint64_t work=1;for(;size;size>>=1)work+=2;budget.normalized(work);};
 std::size_t expected=0;
 for(const auto& binding:product->bindings_){check();const auto* box=binding.placement.box;
  lookup_work(boxes.size());
  if(!box||!binding.placement.interval||!boxes.emplace(box,&binding).second)throw RunError::invalid_input;
  if(box->style.visible&&!box->text.empty())++expected;
 }
 if(list.text_runs.size()!=expected||list.text_runs.size()>65536)throw RunError::invalid_input;
 budget.bytes(native->commands.size(),sizeof(aether_paint_command_t));auto commands=native->commands;
 budget.bytes(list.text_runs.size(),sizeof(NativeTextEntry));auto entries=list.text_runs;
 std::set<std::size_t> indices;std::set<std::uint64_t> consumed;
 using MaskKey=std::tuple<const FontInstance*,std::uint32_t,std::uint32_t,unsigned,unsigned>;
 using Masks=std::map<MaskKey,std::shared_ptr<const GlyphMaskA8>>;Masks masks;
 DocumentPaintProduct output;budget.bytes(std::min(expected,std::size_t{128}),sizeof(DocumentPaintSample));output.samples.reserve(std::min(expected,std::size_t{128}));
 for(std::size_t index=0;index<list.text_runs.size();++index){
  check();const auto& entry=list.text_runs[index];
  if(!entry.document_box||entry.document_placement||entry.command_index>=native->commands.size())throw RunError::invalid_input;
  lookup_work(boxes.size());
  const auto found=boxes.find(entry.document_box);if(found==boxes.end())throw RunError::invalid_input;
  const auto& binding=*found->second;const auto* box=found->first;const auto& interval=*binding.placement.interval;
  budget.bytes(2,sizeof(std::uint64_t)+4*sizeof(void*));
  lookup_work(indices.size());lookup_work(consumed.size());
  if(!indices.insert(entry.command_index).second||!consumed.insert(binding.id).second)throw RunError::invalid_input;
  if(!box->style.visible||box->text.empty()||box->document_interval!=binding.placement.interval||entry.placement.run!=interval.run_)
   throw RunError::invalid_input;
  lookup_work(provider.occurrences_.size());
  const auto occurrence=provider.occurrences_.find(interval.run_.get());
  if(occurrence==provider.occurrences_.end()||occurrence->second.fragment!=interval.fragment_)throw RunError::invalid_input;
  const auto& logical=*occurrence->second.logical;const auto& fragment=*interval.fragment_;
  const auto first=interval.glyph_begin_,last=interval.glyph_end_,byte_first=interval.byte_begin_,byte_last=interval.byte_end_;
  if(first>=last||last>logical.glyphs.size()||byte_first>=byte_last||byte_last>logical.source.size()||
     !interval.run_||interval.run_->source!=logical.source||logical.source!=fragment.normalized||
     interval.run_->font!=logical.binding->font()||interval.run_->glyphs.size()!=logical.glyphs.size())throw RunError::invalid_input;
  const auto source=std::string_view(logical.source).substr(byte_first,byte_last-byte_first);
  const auto base_design=logical.glyphs[first].design_prefix;
  const auto end_design=last<logical.glyphs.size()?logical.glyphs[last].design_prefix:logical.design_end;
  if(end_design<base_design||double(end_design-base_design)*logical.design_scale!=interval.exact_width_)throw RunError::invalid_input;
  const auto& command=native->commands[entry.command_index];if(command.tag!=AETHER_PAINT_DRAW_TEXT)throw RunError::invalid_input;
  const auto& text=command.data.draw_text;
  if(!text.text||!rect_equal(text.rect,box->content_box)||box->text!=source||box->content_box.width!=interval.projected_width_||
     entry.placement.baseline!=float(binding.placement.baseline)||box->native_text.baseline!=logical.baseline||
     text.font_size!=fragment.style_.font_size||text.font_weight!=fragment.style_.font_weight||!color_equal(text.color,fragment.style_.color)||
     text.wrap||text.text_align!=AETHER_TEXT_ALIGN_START||std::strncmp(text.font_family,fragment.style_.font_family.data(),sizeof(text.font_family)))throw RunError::invalid_input;
  for(std::size_t i=0;i<source.size();++i){if((i&255)==0)check();if(text.text[i]!=source[i]||text.text[i]=='\0')throw RunError::invalid_input;}
  if(text.text[source.size()]!='\0')throw RunError::invalid_input;
  // Placed glyph reservation belongs to the sealed ledger; do not charge twice.
  budget.bytes(last-first,sizeof(Glyph));std::vector<Glyph> glyphs;glyphs.reserve(last-first);
  const auto origin=selected_raster_coordinate(binding.placement.x),baseline=selected_raster_coordinate(binding.placement.baseline);
  std::int64_t accumulated=origin.value26_6,end=origin.value26_6;
  for(auto i=first;i<last;++i){check();const auto& glyph=logical.glyphs[i];
   const auto next_design=i+1<logical.glyphs.size()?logical.glyphs[i+1].design_prefix:logical.design_end;
   if(glyph.design_prefix<base_design||next_design<glyph.design_prefix||glyph.cluster<byte_first||glyph.cluster>=byte_last||
      !glyph.outline||glyph.outline->source()!=logical.binding->source()||glyph.outline->glyph_id!=glyph.glyph||
      glyph.outline->units_per_em()!=logical.binding->face()->units_per_em()||interval.run_->glyphs[i].id!=glyph.glyph||
      interval.run_->glyphs[i].cluster!=glyph.cluster)throw RunError::invalid_input;
   const auto position=selected_raster_coordinate(binding.placement.x+double(glyph.design_prefix-base_design)*logical.design_scale);
   const auto next=selected_raster_coordinate(binding.placement.x+double(next_design-base_design)*logical.design_scale);
   const auto delta=std::int64_t(next.value26_6)-position.value26_6;
   if(position.value26_6!=accumulated||delta<0||delta>65536LL*64)throw RunError::numeric_failure;
   accumulated=next.value26_6;end=next.value26_6;
   glyphs.push_back({glyph.glyph,std::uint32_t(glyph.cluster-byte_first),std::int32_t(delta),0,0,0});
   MaskKey key{logical.binding->font().get(),glyph.glyph,logical.binding->font()->raster_flags,position.phase,baseline.phase};
   lookup_work(masks.size());
   if(masks.contains(key)){++output.work.mask_hits;continue;}
   budget.bytes(1,sizeof(Masks::value_type)+4*sizeof(void*)+sizeof(std::shared_ptr<const GlyphMaskA8>));
   truetype::RasterWork raster_work;
   auto raster=truetype::rasterize_document_budgeted(glyph.outline,logical.binding->font(),position.phase,baseline.phase,
    provider.font_budget.raster,provider.font_budget.masks,provider.cancel_,&raster_work);check();
   if(auto* error=std::get_if<truetype::RasterError>(&raster))raster_error(*error);
   auto mask=std::get<truetype::RasterOutput>(std::move(raster)).mask;
   if(!mask||mask->font!=logical.binding->font()||mask->glyph_id!=glyph.glyph||mask->flags!=logical.binding->font()->raster_flags||mask->phase_x!=position.phase||mask->phase_y!=baseline.phase)throw RunError::invalid_input;
   lookup_work(masks.size());
   if(output.masks.size()==output.masks.capacity()){const auto capacity=output.masks.capacity()?output.masks.capacity()*2:1;budget.bytes(capacity,sizeof(std::shared_ptr<const GlyphMaskA8>));output.masks.reserve(capacity);}
   masks.emplace(key,mask);output.masks.push_back(mask);++output.work.mask_misses;output.work.mask_bytes+=mask->bytes.size();
   output.work.raster_work+=raster_work.units;output.work.raster_reserved+=raster_work.aggregate_charged;
   output.work.edge_scan_pairs+=raster_work.edge_scan_pairs;output.work.active_pairs+=raster_work.active_pairs;
   output.work.prepass_work+=raster_work.prepass_work;output.work.prepass_reserved+=raster_work.prepass_reserved;output.work.main_reserved+=raster_work.main_reserved;
  }
  const auto endpoint=selected_raster_coordinate(binding.placement.x+interval.exact_width_);
  if(end!=endpoint.value26_6||accumulated-origin.value26_6!=std::int64_t(endpoint.value26_6)-origin.value26_6)throw RunError::numeric_failure;
  budget.bytes(1,sizeof(ShapedRun)+2*sizeof(void*));budget.bytes(source.size()+1);
  const aether_text_metrics_t metrics{interval.projected_width_,logical.height,logical.baseline};
  auto painted=std::make_shared<const ShapedRun>(logical.binding->font(),std::string(source),std::move(glyphs),metrics);
  const float y=float(baseline.value26_6)/64.f-logical.baseline;
  if(!std::isfinite(y)||std::abs(y)>65536||y+logical.baseline!=float(baseline.value26_6)/64.f)throw RunError::numeric_failure;
  commands[entry.command_index].data.draw_text.rect.x=float(origin.value26_6)/64.f;
  commands[entry.command_index].data.draw_text.rect.y=y;
  entries[index].placement.run=std::move(painted);entries[index].placement.baseline=float(baseline.value26_6)/64.f;
  entries[index].document_placement=binding.id;
  ++output.work.commands;output.work.placed_glyphs+=last-first;
  if(output.samples.size()<128)output.samples.push_back({binding.id,entry.command_index,last-first,box->content_box,
   float(binding.placement.baseline),float(baseline.value26_6)/64.f,origin.value26_6,end,baseline.value26_6});
 }
 for(const auto& [box,binding]:boxes){check();lookup_work(consumed.size());if(box->style.visible&&!box->text.empty()&&!consumed.contains(binding->id))throw RunError::invalid_input;}
 for(std::size_t i=0;i<native->commands.size();++i){if((i&255)==0)check();lookup_work(indices.size());if(native->commands[i].tag==AETHER_PAINT_DRAW_TEXT&&!indices.contains(i))throw RunError::invalid_input;}
 check();native->commands.swap(commands);list.text_runs.swap(entries);return output;
}

}
