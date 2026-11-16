#include "document_text_provider.h"
#include "../../css/frozen_measure_read.h"
#include "../../style_runtime/typography_record.h"
#include <limits>
#include <span>
namespace aether::rendering::text {
namespace {
using aether::layout::DocumentTextBudget;
using truetype::RunError;
void reserve(truetype::AggregateBudget& budget,std::uint64_t amount){if(amount>budget.remaining)throw RunError::limit_exceeded;budget.remaining-=amount;}
// Only bounded, validated immutable font bytes are inspected. No font selection,
// naming-table heuristic or synthetic bold; requested usWeightClass is exact.
std::uint16_t physical_weight(const FontSource& source){
 const auto& b=source.bytes;
 auto u16=[&](std::size_t p){if(p>b.size()||2>b.size()-p)throw RunError::malformed;return std::uint16_t((std::uint16_t(b[p])<<8)|b[p+1]);};
 auto u32=[&](std::size_t p){return (std::uint32_t(u16(p))<<16)|u16(p+2);};
 const auto count=u16(4);if(!count||count>truetype::max_tables)throw RunError::malformed;
 bool found=false;std::uint16_t weight=0;
 for(std::size_t i=0;i<count;++i){const auto at=12+i*16;if(u32(at)!=0x4f532f32)continue;if(found)throw RunError::malformed;found=true;
  const std::size_t offset=u32(at+8),length=u32(at+12);if(offset>b.size()||length>b.size()-offset||length<8)throw RunError::malformed;weight=u16(offset+4);
 }
 if(!found||weight<1||weight>1000)throw RunError::unsupported;
 return weight;
}
std::uint32_t scalar(const std::string& text,std::size_t& i){
 if(i>=text.size())throw RunError::malformed;
 const auto c=static_cast<unsigned char>(text[i++]);if(c<128)return c;
 unsigned tail;std::uint32_t cp,min;if(c>=194&&c<=223){tail=1;cp=c&31;min=128;}else if(c>=224&&c<=239){tail=2;cp=c&15;min=2048;}else if(c>=240&&c<=244){tail=3;cp=c&7;min=65536;}else throw RunError::malformed;
 if(tail>text.size()-i)throw RunError::malformed;
 while(tail--){const auto d=static_cast<unsigned char>(text[i++]);if((d&192)!=128)throw RunError::malformed;cp=(cp<<6)|(d&63);}
 if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw RunError::malformed;
 return cp;
}
[[noreturn]] void binding_error(truetype::BindingError e){
 switch(e){case truetype::BindingError::out_of_memory:throw std::bad_alloc();case truetype::BindingError::cancelled:throw aether::layout::DocumentTextCancelled{};case truetype::BindingError::limit_exceeded:throw RunError::limit_exceeded;case truetype::BindingError::malformed:throw RunError::malformed;default:throw RunError::unsupported;}
}
[[noreturn]] void cmap_error(truetype::CmapError e){
 switch(e){case truetype::CmapError::out_of_memory:throw std::bad_alloc();case truetype::CmapError::cancelled:throw aether::layout::DocumentTextCancelled{};case truetype::CmapError::limit_exceeded:throw RunError::limit_exceeded;case truetype::CmapError::malformed:throw RunError::malformed;default:throw RunError::unsupported;}
}
[[noreturn]] void outline_error(truetype::Error e){
 switch(e){case truetype::Error::out_of_memory:throw std::bad_alloc();case truetype::Error::cancelled:throw aether::layout::DocumentTextCancelled{};case truetype::Error::limit_exceeded:throw RunError::limit_exceeded;case truetype::Error::malformed:throw RunError::malformed;default:throw RunError::unsupported;}
}
}
DocumentTextProvider::DocumentTextProvider(DocumentFontCatalog fonts,truetype::Cancellation* cancel)
 :fonts_(std::move(fonts)),cancel_(cancel){
 if(!fonts_.regular)throw RunError::invalid_input;
 work.font_source_bytes=fonts_.regular->bytes.size();if(fonts_.bold&&fonts_.bold!=fonts_.regular){if(fonts_.bold->bytes.size()>max_font_bytes-work.font_source_bytes)throw RunError::limit_exceeded;work.font_source_bytes+=fonts_.bold->bytes.size();}
 check();
 // Physical sources are explicit and previously bounded; no IO or path lookup.
 if(physical_weight(*fonts_.regular)!=400)throw RunError::unsupported;
 if(fonts_.bold&&physical_weight(*fonts_.bold)!=700)throw RunError::unsupported;
}
std::shared_ptr<DocumentTextProvider> DocumentTextProvider::create(DocumentFontCatalog fonts,DocumentTextBudget& budget,truetype::Cancellation* cancel){budget.bytes(1,sizeof(DocumentTextProvider)+2*sizeof(void*));return std::shared_ptr<DocumentTextProvider>(new DocumentTextProvider(std::move(fonts),cancel));}
void DocumentTextProvider::check(){slice_phase_.reset();if(failure_)std::rethrow_exception(failure_);if(cancel_&&cancel_->cancelled())throw aether::layout::DocumentTextCancelled{};}
void DocumentTextProvider::validate_style(const aether::style::FrozenNodeRef& anchor,const aether::layout::BoxStyleSnapshot& s) try {
 using namespace aether::style;
 if(slice_busy_)throw std::invalid_argument("DOCUMENT_SLICE_REENTRANT");
 check();
 if(!anchor||!s.style_owner()||!s.measures_owner())throw RunError::invalid_input;
 const MeasuresReadView measures(*s.measures_owner());
 if(measures.direction_rtl()||measures.writing_mode()!=0||s.text_transform!=AETHER_TEXT_TRANSFORM_NONE||
    (s.white_space!=AETHER_WHITE_SPACE_NORMAL&&s.white_space!=AETHER_WHITE_SPACE_NOWRAP&&s.white_space!=AETHER_WHITE_SPACE_PRE&&!(drawing_&&s.white_space==AETHER_WHITE_SPACE_PRE_WRAP))||
    (s.font_weight!=400&&s.font_weight!=700))throw RunError::unsupported;
 if(s.font_weight==700&&!fonts_.bold)throw RunError::unsupported;
 if(drawing_){
  const auto& record=anchor->typography();
  if(!record||!record->inputs||!record->inputs->inputs_accounted||record->inputs->external_pending||record->inputs->losses)
   throw std::invalid_argument("DOCUMENT_BREAK_POLICY_UNPROVEN");
  const auto& fields=record->evidence.break_policy.fields;
  if(!fields[0].known||!fields[1].known||fields[0].computed!=measures.word_break()||fields[1].computed!=measures.overflow_wrap())
   throw std::invalid_argument("DOCUMENT_BREAK_POLICY_UNPROVEN");
  if((measures.word_break()!=0&&measures.word_break()!=3)||measures.overflow_wrap()<0||measures.overflow_wrap()>2)
   throw RunError::unsupported;
 }

 if(const auto& record=anchor->typography()){
  const auto& e=record->evidence;
  if(e.coverage_gaps&2u)throw RunError::unsupported;
  if(e.style_state==TypographyFieldState::unsupported||e.spacing_state==TypographyFieldState::unsupported||
     e.size_state==TypographyFieldState::unsupported||e.weight_state==TypographyFieldState::unsupported||e.line_height_state==TypographyFieldState::unsupported)throw RunError::unsupported;
  if(e.style_state==TypographyFieldState::known&&e.font_style!="normal")throw RunError::unsupported;
  if(e.spacing_state==TypographyFieldState::known&&!e.spacing_normal&&e.spacing_px!=0)throw RunError::unsupported;
 }
 // Unknown/unmodeled author intent is NOT proof of zero. The persistent simple
 // typography warning and INCOMPLETE status are unconditional in this provider.
}catch(...){if(!failure_)failure_=std::current_exception();throw;}
const DocumentTextProvider::InstanceEntry& DocumentTextProvider::binding(const aether::layout::BoxStyleSnapshot& s,DocumentTextBudget& budget){
 check();const double q=double(s.font_size)*64.0;if(!std::isfinite(q)||q<1||q>8192||std::floor(q)!=q)throw RunError::unsupported;
 const auto source=s.font_weight==700?fonts_.bold:fonts_.regular;if(!source)throw RunError::unsupported;
 const auto key=std::make_pair(source.get(),std::int32_t(q));if(auto found=instances_.find(key);found!=instances_.end())return found->second;
 budget.instance();budget.bytes(1,sizeof(truetype::RealFontBinding)+sizeof(FontInstance)+sizeof(decltype(instances_)::value_type)+4*sizeof(void*));
 auto value=truetype::RealFontBinding::create(source,std::int32_t(q),truetype::q1_unhinted,cancel_);if(auto* error=std::get_if<truetype::BindingError>(&value))binding_error(*error);
 auto result=std::get<std::shared_ptr<const truetype::RealFontBinding>>(std::move(value));
 auto vertical=truetype::read_vertical_metrics(result);if(auto* error=std::get_if<truetype::BindingError>(&vertical))binding_error(*error);
 auto inserted=instances_.emplace(key,InstanceEntry{result,std::get<truetype::FontVerticalMetrics>(vertical)});
 DocumentTextBudget::add(work.binding_geometry_bytes,result->face()->logical_payload_bytes()+result->map()->logical_payload_bytes(),std::numeric_limits<std::uint64_t>::max());return inserted.first->second;
}
std::pair<float,float> DocumentTextProvider::admission_metrics(const aether::layout::BoxStyleSnapshot& style,DocumentTextBudget& budget) try {
 if(slice_busy_)throw std::invalid_argument("DOCUMENT_SLICE_REENTRANT");
 budget.normalized(1);
 const auto& instance=binding(style,budget);const auto vertical=instance.vertical;
 const aether::style::MeasuresReadView measures(*style.measures_owner());
 const float height=aether::style::measure_used_line_height(&measures,style.scalars());
 const double scale=double(instance.binding->font()->size_26_6)/(64.0*vertical.units_per_em);
 const double baseline=vertical.ascent*scale+(double(height)-(vertical.ascent+vertical.descent)*scale)/2.0;
 if(!std::isfinite(height)||height<=0||height>65536||!std::isfinite(baseline)||baseline<0||baseline>height)throw RunError::unsupported;
 return {height,float(baseline)};
}catch(...){if(!failure_)failure_=std::current_exception();throw;}
Measurement DocumentTextProvider::measure(const std::shared_ptr<const aether::layout::DocumentTextFragment>& fragment,DocumentTextBudget& budget) try {
 if(slice_busy_)throw std::invalid_argument("DOCUMENT_SLICE_REENTRANT");
 if(!fragment)throw RunError::invalid_input;
 check();++work.requests;const auto& style=fragment->style();validate_style(fragment->owner_record(),style);
 const auto& instance=binding(style,budget);auto font=instance.binding;const auto vertical=instance.vertical;const aether::style::MeasuresReadView measures(*style.measures_owner());
 const float height=aether::style::measure_used_line_height(&measures,style.scalars());const double scale=double(font->font()->size_26_6)/(64.0*vertical.units_per_em);
 const double baseline=vertical.ascent*scale+(double(height)-(vertical.ascent+vertical.descent)*scale)/2.0;
 if(!std::isfinite(height)||height<=0||height>65536||!std::isfinite(baseline)||baseline<0||baseline>height)throw RunError::unsupported;
 const auto& source=fragment->normalized;
 std::uint64_t hash=1469598103934665603ULL;std::size_t scalars=0;
 for(std::size_t i=0;i<source.size();){check();budget.visited(1);++work.measured_scalars;++scalars;const auto cp=scalar(source,i);hash^=cp;hash*=1099511628211ULL;}
 if(!scalars||scalars>max_glyphs)throw RunError::limit_exceeded;
 const ProductKey key{font->font().get(),hash,height,float(baseline)};
 std::shared_ptr<const DocumentLogicalProduct> logical;auto bucket=products_.find(key);
 if(bucket!=products_.end())for(const auto& cached:bucket->second){check();if(cached->source.size()!=source.size())continue;bool equal=true;
  for(std::size_t i=0;i<source.size();++i){if((i&255)==0)check();if((static_cast<unsigned char>(source[i])&192)!=128){budget.visited(1);++work.measured_scalars;}++work.cache_compared_bytes;if(source[i]!=cached->source[i]){equal=false;break;}}
  if(equal){logical=cached;break;}
 }
 if(logical)++work.measurement_hits;
 else {
  ++work.measurement_misses;budget.bytes(1,sizeof(DocumentLogicalProduct));budget.bytes(source.size());budget.bytes(scalars,sizeof(DocumentGlyphMeasure));
  auto product=std::make_shared<DocumentLogicalProduct>();product->binding=font;product->source=source;product->height=height;product->baseline=float(baseline);product->glyphs.reserve(scalars);
  std::uint64_t design_pen=0;
  for(std::size_t i=0;i<source.size();){check();budget.visited(1);++work.measured_scalars;const auto cluster=i;const auto cp=scalar(source,i);std::uint16_t gid=0;
   const auto cmap_key=std::make_pair(font->source().get(),cp);
   if(auto found=cmap_.find(cmap_key);found!=cmap_.end()){++work.cmap_hits;gid=found->second;}
   else {++work.cmap_misses;reserve(font_budget.cmap,17);budget.bytes(1,sizeof(decltype(cmap_)::value_type)+4*sizeof(void*));auto mapped=font->map()->lookup(cp);if(auto* error=std::get_if<truetype::CmapError>(&mapped))cmap_error(*error);if(std::holds_alternative<truetype::Missing>(mapped))throw RunError::missing_glyph;gid=std::get<truetype::Mapped>(mapped).glyph_id;cmap_.emplace(cmap_key,gid);}
   const auto outline_key=std::make_pair(font->source().get(),std::uint32_t(gid));std::shared_ptr<const truetype::Outline> outline;
   if(auto found=outlines_.find(outline_key);found!=outlines_.end()){++work.outline_hits;outline=found->second;}
   else {++work.outline_misses;budget.outline();reserve(font_budget.outline,1000000);budget.bytes(1,sizeof(decltype(outlines_)::value_type)+4*sizeof(void*));auto result=font->face()->outline(gid,cancel_);if(auto* error=std::get_if<truetype::Error>(&result))outline_error(*error);outline=std::get<std::shared_ptr<const truetype::Outline>>(std::move(result));outlines_.emplace(outline_key,outline);DocumentTextBudget::add(work.outline_geometry_bytes,sizeof(truetype::Outline)+outline->segments.size()*sizeof(truetype::Segment)+outline->contours.size()*sizeof(truetype::Contour),std::numeric_limits<std::uint64_t>::max());}
   if(outline->source()!=font->source()||outline->units_per_em()!=vertical.units_per_em||outline->glyph_id!=gid)throw RunError::invalid_input;
   const double prefix=double(design_pen)*scale;const double advance=double(outline->metrics.advance)*scale;
   if(outline->metrics.advance>std::numeric_limits<std::uint64_t>::max()-design_pen)throw RunError::limit_exceeded;
   design_pen+=outline->metrics.advance;
   product->glyphs.push_back({cp,std::uint32_t(cluster),gid,prefix,advance,outline,design_pen-outline->metrics.advance});
  }
  product->design_end=design_pen;product->design_scale=scale;
  product->width=double(design_pen)*scale;if(!std::isfinite(product->width)||product->width>65536)throw RunError::limit_exceeded;logical=std::move(product);
  if(bucket==products_.end()){budget.bytes(1,sizeof(decltype(products_)::value_type)+4*sizeof(void*));bucket=products_.emplace(key,std::vector<std::shared_ptr<const DocumentLogicalProduct>>{}).first;}
  budget.bytes(1,sizeof(std::shared_ptr<const DocumentLogicalProduct>));bucket->second.push_back(logical);
 }
 DocumentTextBudget::add(work.occurrence_reserved,1,65536);
 budget.bytes(1,sizeof(ShapedRun)+sizeof(DocumentOccurrence)+sizeof(decltype(occurrences_)::value_type)+4*sizeof(void*));budget.bytes(source.size());budget.bytes(logical->glyphs.size(),sizeof(Glyph));
 std::vector<Glyph> transport;transport.reserve(logical->glyphs.size());
 for(const auto& glyph:logical->glyphs){check();budget.visited(1);++work.measured_scalars;const auto advance=std::round(glyph.advance*64.0);if(!std::isfinite(advance)||advance<0||advance>65536.0*64)throw RunError::limit_exceeded;transport.push_back({glyph.glyph,glyph.cluster,std::int32_t(advance),0,0,0});}
 // Integer advances here are transport placeholders only. Logical width comes
 // from exact design-unit prefix; paint finalization MUST replace the run with
 // absolute-prefix quantized deltas before the unchanged renderer sees it.
 auto measured=std::make_shared<const ShapedRun>(font->font(),source,std::move(transport),aether_text_metrics_t{float(logical->width),height,logical->baseline});
 occurrences_.emplace(measured.get(),DocumentOccurrence{fragment,logical,measured});return {measured->metrics,std::move(measured)};
}catch(...){if(!failure_)failure_=std::current_exception();throw;}
namespace {
bool independent_unit(std::uint32_t cp){
 return (cp>=32&&cp<127)||(cp>=160&&cp<=255&&cp!=173)||cp==0x2013||cp==0x2014||
  cp==0x2018||cp==0x2019||cp==0x201c||cp==0x201d||cp==0x2022||cp==0x2026||(cp>=0x2190&&cp<=0x2193);
}
}
double DocumentTextProvider::interval_width(const DocumentBoundaryTable& table,std::size_t begin,std::size_t end){
 if(begin>=end||end>=table.boundaries_.size())throw std::invalid_argument("DOCUMENT_SLICE_INTERVAL");
 const auto& a=table.boundaries_[begin];const auto& b=table.boundaries_[end];
 if(b.design<a.design||b.byte<=a.byte||b.glyph<=a.glyph||b.range<=a.range)throw std::invalid_argument("DOCUMENT_SLICE_ORDER");
 const double width=double(b.design-a.design)*table.occurrence_.logical->design_scale;
 if(!std::isfinite(width)||width<0||width>65536)throw std::length_error("DOCUMENT_SLICE_WIDTH");
 return width;
}
std::shared_ptr<const aether::layout::DocumentFitCatalog> DocumentTextProvider::prepare_fit_catalog(
 const aether_layout_engine_s* engine,std::span<const std::shared_ptr<const ShapedRun>> runs){
 using Access=aether::layout::DocumentTextAccess;
 auto& budget=Access::slice_entry(engine,this); // thread first, no callback
 if(fit_preparing_||slice_busy_||final_stage_busy_)throw std::invalid_argument("DOCUMENT_FIT_REENTRANT");
 if(runs.empty()||runs.size()>65536)throw std::invalid_argument("DOCUMENT_FIT_CATALOG");
 struct Restore{bool& value;~Restore()noexcept{value=false;}}restore{fit_preparing_};fit_preparing_=true;
 // All caller input retained before the first callback. Quotas never call out.
 budget.bytes(runs.size(),sizeof(std::shared_ptr<const ShapedRun>));
 std::vector<std::shared_ptr<const ShapedRun>> owned(runs.begin(),runs.end());
 budget.bytes(owned.size(),sizeof(std::shared_ptr<const DocumentBoundaryTable>));
 std::vector<std::shared_ptr<const DocumentBoundaryTable>> tables;tables.reserve(owned.size());
 budget.bytes(1,sizeof(aether::layout::DocumentFitCatalog)+4*sizeof(void*));
 auto result=std::shared_ptr<aether::layout::DocumentFitCatalog>(new aether::layout::DocumentFitCatalog);
 result->backend_=this;
 try{
  for(const auto& run:owned){budget.normalized(1);tables.push_back(boundaries(engine,run));}
  // Every construction callback precedes the final full phase certification.
  result->phase_=seal_slices(engine,tables);
  return result;
 }catch(...){if(!failure_)failure_=std::current_exception();throw;}
}
const DocumentBoundaryTable& DocumentTextProvider::fit_table(const aether_layout_engine_s* engine,
 const std::shared_ptr<const aether::layout::DocumentFitCatalog>& catalog,std::size_t index){
 auto& budget=aether::layout::DocumentTextAccess::slice_entry(engine,this);
 if(fit_preparing_||slice_busy_||final_stage_busy_)throw std::invalid_argument("DOCUMENT_FIT_REENTRANT");
 if(failure_)std::rethrow_exception(failure_);
 if(!catalog||catalog->backend_!=this||!catalog->phase_)throw std::invalid_argument("DOCUMENT_FIT_CATALOG");
 const auto& phase=*catalog->phase_;
 if(phase.provider_!=this||!phase.provider_token_||slice_phase_!=phase.provider_token_||index>=phase.tables_.size())
  throw std::invalid_argument("DOCUMENT_SLICE_PHASE_EXPIRED");
 const auto& table=phase.tables_[index];
 if(!table||table->provider_!=this)throw std::invalid_argument("DOCUMENT_FIT_TABLE");
 aether::layout::DocumentTextBudget* actual=nullptr;
 aether::layout::DocumentTextAccess::validate_slice_phase(engine,phase.core_,table->occurrence_.fragment,this,actual);
 if(actual!=&budget)throw std::invalid_argument("DOCUMENT_FIT_BUDGET");
 budget.normalized(1);
 return *table;
}
aether::layout::DocumentFitBoundary DocumentTextProvider::fit_boundary(const aether_layout_engine_s* engine,
 const std::shared_ptr<const aether::layout::DocumentFitCatalog>& catalog,std::size_t index,std::size_t boundary){
 const auto& table=fit_table(engine,catalog,index);
 if(boundary>=table.boundaries_.size())throw std::invalid_argument("DOCUMENT_FIT_BOUNDARY");
 const auto& point=table.boundaries_[boundary];
 return {table.boundaries_.size(),point.byte,point.glyph};
}
double DocumentTextProvider::fit_width(const aether_layout_engine_s* engine,
 const std::shared_ptr<const aether::layout::DocumentFitCatalog>& catalog,std::size_t index,std::size_t begin,std::size_t end){
 const auto& table=fit_table(engine,catalog,index);
 return interval_width(table,begin,end); // no allocation or callback
}
std::shared_ptr<const aether::layout::DocumentTextInterval> DocumentTextProvider::fit_interval(
 const aether_layout_engine_s* engine,const std::shared_ptr<const aether::layout::DocumentFitCatalog>& catalog,
 std::size_t index,std::size_t begin,std::size_t end){
 (void)fit_table(engine,catalog,index);
 const auto view=slice(engine,catalog->phase_,index,begin,end);
 return retain_interval(engine,view); // authentic final role required by issuer
}

std::shared_ptr<const DocumentBoundaryTable> DocumentTextProvider::boundaries(
 const aether_layout_engine_s* engine,const std::shared_ptr<const ShapedRun>& run){
 auto& entry_budget=aether::layout::DocumentTextAccess::slice_entry(engine,this);
 const auto owned_run=run; // retain caller handle before callbacks

 if(slice_busy_)throw std::invalid_argument("DOCUMENT_SLICE_REENTRANT");
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}}restore{slice_busy_};slice_busy_=true;
 if(!owned_run)throw std::invalid_argument("DOCUMENT_SLICE_RUN");
 auto charge_lookup=[&](std::size_t size){std::uint64_t work=1;for(;size;size>>=1)work+=2;entry_budget.normalized(work);};
 charge_lookup(occurrences_.size());
 auto found=occurrences_.find(owned_run.get());
 if(found==occurrences_.end()||found->second.measured!=owned_run)throw std::invalid_argument("DOCUMENT_SLICE_FOREIGN_RUN");
 const auto occurrence=found->second; // owns all resources across callbacks
 check();aether::layout::DocumentTextAccess::check(engine);aether::layout::DocumentTextBudget* budget=nullptr;
 const auto operation=aether::layout::DocumentTextAccess::slice_token(engine,occurrence.fragment,this,budget);
 charge_lookup(boundary_tables_.size());
 if(auto cached=boundary_tables_.find(owned_run.get());cached!=boundary_tables_.end())return cached->second;
 const auto& logical=*occurrence.logical;const auto n=logical.glyphs.size();
 if(!n||n>max_glyphs||!logical.binding||logical.binding->font()!=owned_run->font||owned_run->source!=occurrence.fragment->normalized||
    logical.source!=owned_run->source||owned_run->glyphs.size()!=n||!std::isfinite(logical.design_scale)||logical.design_scale<=0)
  throw std::invalid_argument("DOCUMENT_SLICE_PRODUCT");
 budget->bytes(1,sizeof(DocumentBoundaryTable)+2*sizeof(void*));
 budget->bytes(1,sizeof(decltype(boundary_tables_)::value_type)+4*sizeof(void*));
 budget->bytes(n+1,sizeof(DocumentBoundaryTable::Boundary));
 auto table=std::shared_ptr<DocumentBoundaryTable>(new DocumentBoundaryTable);
 table->provider_=this;table->operation_=operation;table->occurrence_=occurrence;table->boundaries_.reserve(n+1);
 std::size_t byte=0,range=0;std::uint64_t pen=0;
 const auto& ranges=occurrence.fragment->ranges;
 for(std::size_t i=0;i<n;++i){
  if((i&255)==0){check();aether::layout::DocumentTextAccess::check(engine);}
  budget->visited(1);const auto start=byte;const auto cp=scalar(logical.source,byte);const auto& g=logical.glyphs[i];
  if(!independent_unit(cp)||g.scalar!=cp||g.cluster!=start||g.design_prefix!=pen||!g.outline||
     g.outline->source()!=logical.binding->source()||g.glyph!=g.outline->glyph_id||
     g.prefix!=double(pen)*logical.design_scale||g.advance!=double(g.outline->metrics.advance)*logical.design_scale||
     owned_run->glyphs[i].id!=g.glyph||owned_run->glyphs[i].cluster!=g.cluster)
   throw std::invalid_argument("DOCUMENT_SLICE_BOUNDARY_UNSUPPORTED");
  table->boundaries_.push_back({start,i,range,pen});
  const auto first=range;
  while(range<ranges.size()&&ranges[range].output_begin==start){
   budget->normalized(1);
   if(ranges[range].output_length!=byte-start)throw std::invalid_argument("DOCUMENT_SLICE_RANGE");
   ++range;
  }
  if(first==range)throw std::invalid_argument("DOCUMENT_SLICE_COVERAGE");
  const auto advance=g.outline->metrics.advance;
  if(advance>std::numeric_limits<std::uint64_t>::max()-pen)throw std::length_error("DOCUMENT_SLICE_DESIGN_OVERFLOW");
  pen+=advance;
 }
 if(byte!=logical.source.size()||range!=ranges.size()||pen!=logical.design_end||
    double(pen)*logical.design_scale!=logical.width)throw std::invalid_argument("DOCUMENT_SLICE_END");
 table->boundaries_.push_back({byte,n,range,pen});
 check();aether::layout::DocumentTextAccess::check(engine);aether::layout::DocumentTextBudget* final_budget=nullptr;
 if(aether::layout::DocumentTextAccess::slice_token(engine,occurrence.fragment,this,final_budget)!=operation||final_budget!=budget)
  throw std::invalid_argument("DOCUMENT_SLICE_CHANGED");
 charge_lookup(boundary_tables_.size()+1);
 boundary_tables_.emplace(owned_run.get(),table);
 return table;
}
std::shared_ptr<const DocumentSlicePhase> DocumentTextProvider::seal_slices(const aether_layout_engine_s* engine,
 std::span<const std::shared_ptr<const DocumentBoundaryTable>> tables){
 auto& entry_budget=aether::layout::DocumentTextAccess::slice_entry(engine,this);
 if(slice_busy_||tables.empty()||tables.size()>65536)throw std::invalid_argument("DOCUMENT_SLICE_CATALOG");
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}}restore{slice_busy_};slice_busy_=true;
 entry_budget.bytes(tables.size(),sizeof(std::shared_ptr<const DocumentBoundaryTable>));
 std::vector<std::shared_ptr<const DocumentBoundaryTable>> owned_tables(tables.begin(),tables.end());
 check();aether::layout::DocumentTextAccess::check(engine);
 aether::layout::DocumentTextBudget* budget=nullptr;
 if(!owned_tables[0]||owned_tables[0]->provider_!=this)throw std::invalid_argument("DOCUMENT_SLICE_CATALOG");
 (void)aether::layout::DocumentTextAccess::slice_token(engine,owned_tables[0]->occurrence_.fragment,this,budget);
 budget->bytes(1,sizeof(DocumentSlicePhase)+sizeof(int)+4*sizeof(void*));
 budget->bytes(owned_tables.size(),sizeof(std::shared_ptr<const aether::layout::DocumentTextFragment>));
 auto phase=std::shared_ptr<DocumentSlicePhase>(new DocumentSlicePhase);phase->provider_=this;
 auto provider_token=std::make_shared<const int>(0);
 phase->tables_=std::move(owned_tables);std::vector<std::shared_ptr<const aether::layout::DocumentTextFragment>> fragments;fragments.reserve(phase->tables_.size());
 for(const auto& table:phase->tables_){
  std::uint64_t lookup_work=1;for(auto count=boundary_tables_.size();count;count>>=1)lookup_work+=2;
  budget->normalized(lookup_work);
  if(!table||table->provider_!=this)throw std::invalid_argument("DOCUMENT_SLICE_CATALOG");
  auto found=boundary_tables_.find(table->occurrence_.measured.get());
  if(found==boundary_tables_.end()||found->second!=table)throw std::invalid_argument("DOCUMENT_SLICE_UNREGISTERED_TABLE");
  fragments.push_back(table->occurrence_.fragment);
 }
 phase->core_=aether::layout::DocumentTextAccess::seal_slice_phase(engine,fragments,this);
 if(failure_)std::rethrow_exception(failure_);
 phase->provider_token_=provider_token;slice_phase_=std::move(provider_token);return phase;
}
DocumentSliceView DocumentTextProvider::slice(const aether_layout_engine_s* engine,
 const std::shared_ptr<const DocumentSlicePhase>& phase,std::size_t table_index,std::size_t begin,std::size_t end){
 (void)aether::layout::DocumentTextAccess::slice_entry(engine,this);
 if(slice_busy_||!phase||phase->provider_!=this||!phase->provider_token_||slice_phase_!=phase->provider_token_||table_index>=phase->tables_.size())
  throw std::invalid_argument("DOCUMENT_SLICE_PHASE_EXPIRED");
 const auto& table=phase->tables_[table_index];
 if(begin>=end||end>=table->boundaries_.size())throw std::invalid_argument("DOCUMENT_SLICE_INTERVAL");
 aether::layout::DocumentTextBudget* budget=nullptr;
 aether::layout::DocumentTextAccess::validate_slice_phase(engine,phase->core_,table->occurrence_.fragment,this,budget);
 const double width=interval_width(*table,begin,end);
 budget->bytes(1,sizeof(DocumentSliceView));
 return DocumentSliceView(table,phase,table_index,begin,end,width);
}
std::shared_ptr<const aether::layout::DocumentConstructionInterval> DocumentTextProvider::fit_construction(
 const aether_layout_engine_s* engine,const std::shared_ptr<const aether::layout::DocumentFitCatalog>& catalog,
 std::size_t index,std::size_t begin,std::size_t end){
 (void)fit_table(engine,catalog,index);
 const auto view=slice(engine,catalog->phase_,index,begin,end);
 return retain_construction(engine,view);
}
std::shared_ptr<const aether::layout::DocumentConstructionInterval> DocumentTextProvider::retain_construction(
 const aether_layout_engine_s* engine,const DocumentSliceView& view){
 auto& budget=aether::layout::DocumentTextAccess::slice_entry(engine,this); // thread before provider fields
 if(slice_busy_)throw std::invalid_argument("DOCUMENT_INTERVAL_REENTRANT");
 if(failure_)std::rethrow_exception(failure_);
 // This entire issuance is callback-free. Owned copies retain all resources
 // through allocation/failure; there is no borrowed-input callback window.
 const auto table=view.table_;const auto phase=view.phase_;
 if(!table||!phase||phase->provider_!=this||table->provider_!=this||
    !phase->provider_token_||slice_phase_!=phase->provider_token_||
    view.table_index_>=phase->tables_.size()||phase->tables_[view.table_index_]!=table)
  throw std::invalid_argument("DOCUMENT_INTERVAL_PHASE");
 aether::layout::DocumentTextBudget* actual=nullptr;
 aether::layout::DocumentTextAccess::validate_slice_phase(engine,phase->core_,table->occurrence_.fragment,this,actual);
 if(actual!=&budget||table->operation_!=aether::layout::DocumentTextAccess::slice_token(engine,table->occurrence_.fragment,this,actual))
  throw std::invalid_argument("DOCUMENT_INTERVAL_OPERATION");
 std::uint64_t lookup_work=1;for(auto n=boundary_tables_.size();n;n>>=1)lookup_work+=2;
 budget.normalized(lookup_work);
 const auto registered=boundary_tables_.find(table->occurrence_.measured.get());
 if(registered==boundary_tables_.end()||registered->second!=table)
  throw std::invalid_argument("DOCUMENT_INTERVAL_TABLE");
 if(view.begin_>=view.end_||view.end_>=table->boundaries_.size()||!table->occurrence_.logical||!table->occurrence_.measured)
  throw std::invalid_argument("DOCUMENT_INTERVAL_RANGE");
 const auto& a=table->boundaries_[view.begin_];const auto& b=table->boundaries_[view.end_];
 const auto& product=*table->occurrence_.logical;
 if(a.glyph!=view.begin_||b.glyph!=view.end_||b.glyph>product.glyphs.size()||
    b.glyph>table->occurrence_.measured->glyphs.size()||a.byte>=b.byte||b.byte>product.source.size()||
    b.byte>table->occurrence_.fragment->normalized.size()||a.range>=b.range||b.range>table->occurrence_.fragment->ranges.size()||
    b.design<a.design||!std::isfinite(product.design_scale)||product.design_scale<=0.)
  throw std::invalid_argument("DOCUMENT_INTERVAL_BOUNDARIES");
 const double width=double(b.design-a.design)*product.design_scale; // subtract integers BEFORE conversion
 if(!std::isfinite(width)||width<0.||width>65536.||width!=view.width_)
  throw std::invalid_argument("DOCUMENT_INTERVAL_EXACT_WIDTH");
 // C1b constructor compares whole original strings, so charge that actual work
 // here; no fake constant-time claim and no allocation of a substring/run.
 budget.normalized(table->occurrence_.fragment->normalized.size()+1);
 budget.bytes(1,sizeof(aether::layout::DocumentConstructionInterval)+2*sizeof(void*));
 return std::shared_ptr<const aether::layout::DocumentConstructionInterval>(new aether::layout::DocumentConstructionInterval(
  table->occurrence_.fragment,table->occurrence_.measured,table,a.glyph,b.glyph,a.byte,b.byte,width));
}
std::shared_ptr<const aether::layout::DocumentTextInterval> DocumentTextProvider::retain_interval(
 const aether_layout_engine_s* engine,const DocumentSliceView& view){
 auto& budget=aether::layout::DocumentTextAccess::slice_entry(engine,this); // thread before provider fields
 if(slice_busy_)throw std::invalid_argument("DOCUMENT_INTERVAL_REENTRANT");
 if(failure_)std::rethrow_exception(failure_);
 // This entire issuance is callback-free. Owned copies retain all resources
 // through allocation/failure; there is no borrowed-input callback window.
 const auto table=view.table_;const auto phase=view.phase_;
 if(!table||!phase||phase->provider_!=this||table->provider_!=this||
    !phase->provider_token_||slice_phase_!=phase->provider_token_||
    view.table_index_>=phase->tables_.size()||phase->tables_[view.table_index_]!=table)
  throw std::invalid_argument("DOCUMENT_INTERVAL_PHASE");
 aether::layout::DocumentTextBudget* actual=nullptr;
 aether::layout::DocumentTextAccess::validate_slice_phase(engine,phase->core_,table->occurrence_.fragment,this,actual);
 if(actual!=&budget||table->operation_!=aether::layout::DocumentTextAccess::final_fragment_identity(engine,table->occurrence_.fragment,this))
  throw std::invalid_argument("DOCUMENT_INTERVAL_OPERATION");
 std::uint64_t lookup_work=1;for(auto n=boundary_tables_.size();n;n>>=1)lookup_work+=2;
 budget.normalized(lookup_work);
 const auto registered=boundary_tables_.find(table->occurrence_.measured.get());
 if(registered==boundary_tables_.end()||registered->second!=table)
  throw std::invalid_argument("DOCUMENT_INTERVAL_TABLE");
 if(view.begin_>=view.end_||view.end_>=table->boundaries_.size()||!table->occurrence_.logical||!table->occurrence_.measured)
  throw std::invalid_argument("DOCUMENT_INTERVAL_RANGE");
 const auto& a=table->boundaries_[view.begin_];const auto& b=table->boundaries_[view.end_];
 const auto& product=*table->occurrence_.logical;
 if(a.glyph!=view.begin_||b.glyph!=view.end_||b.glyph>product.glyphs.size()||
    b.glyph>table->occurrence_.measured->glyphs.size()||a.byte>=b.byte||b.byte>product.source.size()||
    b.byte>table->occurrence_.fragment->normalized.size()||a.range>=b.range||b.range>table->occurrence_.fragment->ranges.size()||
    b.design<a.design||!std::isfinite(product.design_scale)||product.design_scale<=0.)
  throw std::invalid_argument("DOCUMENT_INTERVAL_BOUNDARIES");
 const double width=double(b.design-a.design)*product.design_scale; // subtract integers BEFORE conversion
 if(!std::isfinite(width)||width<0.||width>65536.||width!=view.width_)
  throw std::invalid_argument("DOCUMENT_INTERVAL_EXACT_WIDTH");
 // C1b constructor compares whole original strings, so charge that actual work
 // here; no fake constant-time claim and no allocation of a substring/run.
 budget.normalized(table->occurrence_.fragment->normalized.size()+1);
 budget.bytes(1,sizeof(aether::layout::DocumentTextInterval)+2*sizeof(void*));
 return std::shared_ptr<const aether::layout::DocumentTextInterval>(new aether::layout::DocumentTextInterval(
  table->occurrence_.fragment,table->occurrence_.measured,table,a.glyph,b.glyph,a.byte,b.byte,width));
}
std::vector<std::shared_ptr<const aether::layout::DocumentTextInterval>> DocumentTextProvider::promote_constructions(
 const aether_layout_engine_s* engine,std::span<const std::shared_ptr<const aether::layout::DocumentConstructionInterval>> input){
 using namespace aether::layout;
 auto& budget=DocumentTextAccess::slice_entry(engine,this);
 budget.bytes(input.size(),sizeof(std::shared_ptr<const DocumentConstructionInterval>)+sizeof(std::shared_ptr<const ShapedRun>)+sizeof(std::shared_ptr<const DocumentTextInterval>));
 const std::vector<std::shared_ptr<const DocumentConstructionInterval>> owned(input.begin(),input.end());
 std::vector<std::shared_ptr<const ShapedRun>> runs;runs.reserve(owned.size());
 for(const auto& c:owned){budget.normalized(1);if(!c)throw std::invalid_argument("DOCUMENT_PROMOTION_NULL");runs.push_back(c->run_);}
 std::vector<std::shared_ptr<const DocumentTextInterval>> output;output.reserve(owned.size());
 if(owned.empty())return output;
 const auto catalog=prepare_fit_catalog(engine,runs); // owns inputs before callbacks
 for(std::size_t i=0;i<owned.size();++i){
  const auto& c=owned[i];const auto& table=fit_table(engine,catalog,i);
  if(&table!=c->table_.get()||table.occurrence_.fragment!=c->fragment_)
   throw std::invalid_argument("DOCUMENT_PROMOTION_TABLE");
  output.push_back(fit_interval(engine,catalog,i,c->glyph_begin_,c->glyph_end_));
 }
 return output;
}
void DocumentTextProvider::partition_geometry(const DocumentBoundaryTable& table,std::size_t begin,std::size_t end,
 const DocumentPlacementGeometry& geometry)const{
 if(begin>=end||end>=table.boundaries_.size())throw std::invalid_argument("DOCUMENT_PARTITION_RANGE");
 const auto& a=table.boundaries_[begin];const auto& b=table.boundaries_[end];
 if(b.design<a.design)throw std::invalid_argument("DOCUMENT_PARTITION_DESIGN");
 const auto& logical=*table.occurrence_.logical;
 const double width=double(b.design-a.design)*logical.design_scale;
 for(double v:{geometry.x,geometry.y,geometry.width,geometry.height,geometry.baseline})
  if(!std::isfinite(v)||std::abs(v)>65536)throw std::invalid_argument("DOCUMENT_PARTITION_GEOMETRY");
 if(geometry.width<0||geometry.height<0||geometry.width!=width||geometry.height!=double(logical.height)||
    geometry.baseline!=geometry.y+double(logical.baseline)||!std::isfinite(width)||width>65536)
  throw std::invalid_argument("DOCUMENT_PARTITION_METRICS");
}
void DocumentTextProvider::partition_identity(const aether_layout_engine_s* engine,
 const std::shared_ptr<DocumentPartitionLedger>& ledger,aether::layout::DocumentTextBudget& budget){
 if(!ledger||ledger->provider_!=this||!ledger->proof_||ledger->sealed_)throw std::invalid_argument("DOCUMENT_PARTITION_IDENTITY");
 aether::layout::DocumentTextAccess::final_pass_identity(engine,ledger->proof_,this);
 std::uint64_t work=1;for(auto n=partitions_.size();n;n>>=1)work+=2;budget.normalized(work);
 auto found=partitions_.find(ledger->proof_->generation_);
 if(found==partitions_.end()||found->second!=ledger||ledger->progress_.size()!=ledger->proof_->fragments_.size())
  throw std::invalid_argument("DOCUMENT_PARTITION_FOREIGN");
}
std::shared_ptr<DocumentPartitionLedger> DocumentTextProvider::begin_partition(const aether_layout_engine_s* engine,std::uint64_t pass){
 auto& budget=aether::layout::DocumentTextAccess::slice_entry(engine,this);
 if(slice_busy_)throw std::invalid_argument("DOCUMENT_PARTITION_REENTRANT");
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}} restore{slice_busy_};slice_busy_=true;
 check();auto proof=aether::layout::DocumentTextAccess::final_inventory(engine,pass,this);
 return register_partition(std::move(proof),budget);
}
std::shared_ptr<DocumentPartitionLedger> DocumentTextProvider::begin_partition(const aether_layout_engine_s* engine,
 const std::shared_ptr<const aether::layout::DocumentFinalPass>& input){
 auto& budget=aether::layout::DocumentTextAccess::slice_entry(engine,this);
 if(slice_busy_)throw std::invalid_argument("DOCUMENT_PARTITION_REENTRANT");
 const auto proof=input; // retain exact core proof before callbacks
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}} restore{slice_busy_};slice_busy_=true;
 aether::layout::DocumentTextAccess::final_pass_identity(engine,proof,this);
 check();aether::layout::DocumentTextAccess::check(engine);
 aether::layout::DocumentTextAccess::validate_final_inventory(engine,proof,this);
 return register_partition(proof,budget); // no reissued proof, no callback after audit
}
std::shared_ptr<DocumentPartitionLedger> DocumentTextProvider::register_partition(
 std::shared_ptr<const aether::layout::DocumentFinalPass> proof,aether::layout::DocumentTextBudget& budget){
 std::uint64_t work=1;for(auto n=partitions_.size()+1;n;n>>=1)work+=2;budget.normalized(work*2);
 if(partitions_.find(proof->generation_)!=partitions_.end())throw std::invalid_argument("DOCUMENT_PARTITION_ALREADY_OPENED");
 budget.bytes(1,sizeof(DocumentPartitionLedger)+2*sizeof(void*));
 budget.bytes(proof->fragments_.size(),sizeof(DocumentPartitionLedger::Progress));
 budget.bytes(1,sizeof(decltype(partitions_)::value_type)+4*sizeof(void*));
 auto ledger=std::shared_ptr<DocumentPartitionLedger>(new DocumentPartitionLedger);
 ledger->provider_=this;ledger->proof_=std::move(proof);ledger->progress_.resize(ledger->proof_->fragments_.size());
 for(const auto& event:ledger->proof_->suppressed_){
  budget.normalized(1);const auto& fragments=ledger->proof_->fragments_;
  if(fragments.empty()||event->fragment_->occurrence<fragments.front()->occurrence)throw std::invalid_argument("DOCUMENT_PARTITION_SUPPRESSION");
  const auto index=event->fragment_->occurrence-fragments.front()->occurrence;
  if(index>=fragments.size()||fragments[index]!=event->fragment_||ledger->progress_[index].suppressed)
   throw std::invalid_argument("DOCUMENT_PARTITION_SUPPRESSION");
  ledger->progress_[index].suppressed=true;
 }

 // No callback after full inventory validation; registration is atomic on OOM.
 partitions_.emplace(ledger->proof_->generation_,ledger);return ledger;
}
std::uint64_t DocumentTextProvider::append_partition(const aether_layout_engine_s* engine,
 const std::shared_ptr<DocumentPartitionLedger>& input,const DocumentSliceView& view,DocumentPlacementGeometry geometry){
 auto& budget=aether::layout::DocumentTextAccess::slice_entry(engine,this);
 if(slice_busy_)throw std::invalid_argument("DOCUMENT_PARTITION_REENTRANT");
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}} restore{slice_busy_};slice_busy_=true;
 const auto ledger=input;partition_identity(engine,ledger,budget); // no callbacks in append
 const auto table=view.table_;const auto phase=view.phase_;
 if(!table||table->provider_!=this||!phase||phase->provider_!=this||!phase->provider_token_||slice_phase_!=phase->provider_token_)
  throw std::invalid_argument("DOCUMENT_PARTITION_PHASE");
 aether::layout::DocumentTextBudget* actual=nullptr;
 aether::layout::DocumentTextAccess::validate_slice_phase(engine,phase->core_,table->occurrence_.fragment,this,actual);
 if(actual!=&budget||table->operation_!=ledger->proof_->operation_)throw std::invalid_argument("DOCUMENT_PARTITION_OPERATION");
 std::uint64_t lookup_work=1;for(auto n=boundary_tables_.size();n;n>>=1)lookup_work+=2;budget.normalized(lookup_work);
 const auto registered=boundary_tables_.find(table->occurrence_.measured.get());
 if(registered==boundary_tables_.end()||registered->second!=table)throw std::invalid_argument("DOCUMENT_PARTITION_TABLE");
 const auto& fragments=ledger->proof_->fragments_;const auto& fragment=table->occurrence_.fragment;
 if(fragments.empty()||fragment->occurrence<fragments.front()->occurrence)throw std::invalid_argument("DOCUMENT_PARTITION_FRAGMENT");
 const auto index=fragment->occurrence-fragments.front()->occurrence;
 if(index>=fragments.size()||fragments[index]!=fragment||fragment->normalized.empty())throw std::invalid_argument("DOCUMENT_PARTITION_FRAGMENT");
 auto& progress=ledger->progress_[index];
 if(progress.suppressed)throw std::invalid_argument("DOCUMENT_PARTITION_SUPPRESSION_OVERLAP");
 if((progress.table&&progress.table!=table)||view.begin_!=progress.next)throw std::invalid_argument("DOCUMENT_PARTITION_ADJACENCY");
 partition_geometry(*table,view.begin_,view.end_,geometry);
 if(view.width_!=geometry.width)throw std::invalid_argument("DOCUMENT_PARTITION_VIEW");
 const auto id=aether::layout::DocumentTextAccess::reserve_placement(engine,ledger->proof_,this,view.end_-view.begin_);
 if(ledger->placements_.size()==ledger->placements_.capacity()){
  const auto old=ledger->placements_.capacity();const auto next=old?std::min(std::size_t{1048576},old*2):std::size_t{1};
  if(next<=old)throw std::length_error("DOCUMENT_PARTITION_CAPACITY");
  budget.bytes(next,sizeof(DocumentPartition::Placement));ledger->placements_.reserve(next);
 }
 // Shared-reference copies/geometry construction cannot throw after reserve.
 ledger->placements_.push_back({table,id,view.begin_,view.end_,geometry});
 progress.table=table;progress.expected=table->boundaries_.size()-1;progress.next=view.end_;return id;
}
std::shared_ptr<const DocumentPartition> DocumentTextProvider::seal_partition(const aether_layout_engine_s* engine,
 const std::shared_ptr<DocumentPartitionLedger>& input){
 auto& budget=aether::layout::DocumentTextAccess::slice_entry(engine,this);
 if(slice_busy_)throw std::invalid_argument("DOCUMENT_PARTITION_REENTRANT");
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}} restore{slice_busy_};slice_busy_=true;
 const auto ledger=input;partition_identity(engine,ledger,budget);
 for(std::size_t i=0;i<ledger->progress_.size();++i){
  budget.normalized(1);const auto& p=ledger->progress_[i];const bool empty=ledger->proof_->fragments_[i]->normalized.empty();
  if((empty||p.suppressed)?(p.table||p.next||p.expected):(!p.table||!p.expected||p.next!=p.expected))
   throw std::invalid_argument("DOCUMENT_PARTITION_INCOMPLETE");
 }
 budget.bytes(1,sizeof(DocumentPartition)+2*sizeof(void*));
 budget.bytes(ledger->placements_.size(),sizeof(DocumentPartition::Placement));
 auto result=std::shared_ptr<DocumentPartition>(new DocumentPartition);
 result->provider_=this;result->proof_=ledger->proof_;result->placements_=ledger->placements_;
 budget.bytes(ledger->progress_.size(),sizeof(std::size_t));std::vector<std::size_t> ends(ledger->progress_.size());
 // Last callbacks may invalidate all A phases. They are not final authority.
 check();aether::layout::DocumentTextAccess::check(engine);
 partition_identity(engine,ledger,budget);
 aether::layout::DocumentTextAccess::validate_final_inventory(engine,ledger->proof_,this);
 // Validate complete placement partition afresh without callbacks before publish.
 std::uint64_t previous=0;
 for(const auto& p:result->placements_){
  budget.normalized(1);const auto& f=p.table->occurrence_.fragment;const auto& fragments=ledger->proof_->fragments_;
  if(fragments.empty()||f->occurrence<fragments.front()->occurrence)throw std::invalid_argument("DOCUMENT_PARTITION_FRAGMENT");
  const auto index=f->occurrence-fragments.front()->occurrence;
  if(index>=fragments.size()||fragments[index]!=f||ledger->progress_[index].table!=p.table||p.begin!=ends[index]||p.id<=previous)
   throw std::invalid_argument("DOCUMENT_PARTITION_PLACEMENT");
  std::uint64_t work=1;for(auto n=boundary_tables_.size();n;n>>=1)work+=2;budget.normalized(work);
  auto found=boundary_tables_.find(p.table->occurrence_.measured.get());
  if(found==boundary_tables_.end()||found->second!=p.table||p.table->operation_!=ledger->proof_->operation_)
   throw std::invalid_argument("DOCUMENT_PARTITION_TABLE");
  partition_geometry(*p.table,p.begin,p.end,p.geometry);ends[index]=p.end;previous=p.id;
 }
 for(std::size_t i=0;i<ends.size();++i){budget.normalized(1);if(ends[i]!=ledger->progress_[i].expected)throw std::invalid_argument("DOCUMENT_PARTITION_SUFFIX");}
 if(failure_)std::rethrow_exception(failure_);
 ledger->sealed_=true;return result; // no callback or throwing operation after seal
}
std::shared_ptr<const aether::layout::DocumentFinalizationProduct> DocumentTextProvider::stage_final_geometry(
 const aether_layout_engine_s* engine,const aether::layout::DocumentLayoutSnapshot& snapshot,
 std::span<const std::shared_ptr<const aether::layout::DocumentFinalPass>> input,aether::layout::DocumentTextBudget& supplied){
 using Access=aether::layout::DocumentTextAccess;
 using Product=aether::layout::DocumentFinalizationProduct;
 auto& budget=Access::final_stage_entry(engine,snapshot,this); // thread/real stage before provider state
 if(&budget!=&supplied||slice_busy_||final_stage_busy_)throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_REENTRANT");
 if(failure_)std::rethrow_exception(failure_);
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}} restore{final_stage_busy_};final_stage_busy_=true;
 try {
  const auto work=[](std::size_t n){std::uint64_t w=1;for(;n;n>>=1)w+=2;return w;};
  budget.bytes(input.size(),sizeof(std::shared_ptr<const aether::layout::DocumentFinalPass>));
  const std::vector<std::shared_ptr<const aether::layout::DocumentFinalPass>> proofs(input.begin(),input.end());
  auto extracted=Access::final_snapshot_intervals(engine,snapshot,this);
  budget.bytes(proofs.size(),sizeof(std::pair<const std::uint64_t,std::size_t>)+4*sizeof(void*));
  std::map<std::uint64_t,std::size_t> pass_index;
  for(std::size_t i=0;i<proofs.size();++i){
   Access::validate_final_inventory(engine,proofs[i],this);budget.normalized(work(proofs.size()));
   if((i&&proofs[i-1]->pass_>=proofs[i]->pass_)||!pass_index.emplace(proofs[i]->pass_,i).second)
    throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_PASS_ORDER");
  }
  struct Item {aether::layout::DocumentFinalIntervalPlacement placement;std::size_t slot{},table_slot{};std::uint64_t ordinal{};std::size_t begin{},end{};};
  budget.bytes(extracted.size(),sizeof(Item));std::vector<Item> items;items.reserve(extracted.size());
  for(auto& placement:extracted){
   budget.normalized(work(proofs.size()));const auto interval=placement.interval;
   if(!interval||!interval->fragment_)throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_INTERVAL");
   const auto& f=interval->fragment_;const auto found=pass_index.find(f->normalization_pass);
   if(found==pass_index.end())throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_FOREIGN_PASS");
   const auto& proof=proofs[found->second];
   if(proof->fragments_.empty()||f->occurrence<proof->fragments_.front()->occurrence)
    throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_FRAGMENT");
   const auto ordinal=f->occurrence-proof->fragments_.front()->occurrence;
   if(ordinal>=proof->fragments_.size()||proof->fragments_[ordinal]!=f)
    throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_FRAGMENT");
   items.push_back({std::move(placement),found->second,0,ordinal,interval->byte_begin_,interval->byte_end_});
  }
  budget.normalized(items.size()*work(items.size()));
  std::sort(items.begin(),items.end(),[](const Item& a,const Item& b){return std::tie(a.slot,a.ordinal,a.begin,a.end)<std::tie(b.slot,b.ordinal,b.begin,b.end);});
  budget.bytes(proofs.size(),sizeof(Product::Pass));std::vector<Product::Pass> passes;passes.reserve(proofs.size());
  budget.bytes(items.size(),sizeof(Product::Binding));std::vector<Product::Binding> bindings;bindings.reserve(items.size());
  std::size_t next=0;
  for(std::size_t slot=0;slot<proofs.size();++slot){
   auto ledger=begin_partition(engine,proofs[slot]);
   Access::final_stage_entry(engine,snapshot,this); // begin callbacks ended: validate before further placement work
   const auto begin=next;while(next<items.size()&&items[next].slot==slot){budget.normalized(1);++next;}const auto count=next-begin;
   budget.bytes(count,sizeof(std::pair<const DocumentBoundaryTable* const,std::size_t>)+4*sizeof(void*)+sizeof(std::shared_ptr<const DocumentBoundaryTable>));
   std::map<const DocumentBoundaryTable*,std::size_t> table_index;
   std::vector<std::shared_ptr<const DocumentBoundaryTable>> tables;tables.reserve(count);
   for(std::size_t i=begin;i<next;++i){
    auto& item=items[i];const auto& table=item.placement.interval->table_;budget.normalized(work(count));
    if(!table||table->provider_!=this||table->occurrence_.fragment!=item.placement.interval->fragment_||
       table->occurrence_.measured!=item.placement.interval->run_)
     throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_TABLE");
    auto [it,inserted]=table_index.emplace(table.get(),tables.size());
    if(inserted)tables.push_back(table);item.table_slot=it->second;
   }
   std::shared_ptr<const DocumentSlicePhase> phase;
   if(!tables.empty()){
    phase=seal_slices(engine,tables);Access::final_stage_entry(engine,snapshot,this);
   }
   const auto binding_begin=bindings.size();
   for(std::size_t i=begin;i<next;++i){
    const auto& item=items[i];const auto& interval=item.placement.interval;
    const auto view=slice(engine,phase,item.table_slot,interval->glyph_begin_,interval->glyph_end_);
    if(view.table_!=interval->table_||view.width_!=interval->exact_width_)
     throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_VIEW");
    const auto& g=item.placement;
    const auto id=append_partition(engine,ledger,view,{g.x,g.y,g.width,g.height,g.baseline});
    bindings.push_back({g,proofs[slot],id}); // reserved before callbacks; glyphs charged by ledger only
   }
   const auto sealed=seal_partition(engine,ledger);Access::final_stage_entry(engine,snapshot,this);
   if(!sealed||sealed->provider_!=this||sealed->proof_!=proofs[slot]||sealed->placements_.size()!=count||
      bindings.size()-binding_begin!=count)throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_SEAL");
   for(std::size_t i=0;i<count;++i){
    budget.normalized(1);const auto& b=bindings[binding_begin+i];const auto& placement=sealed->placements_[i];
    const auto& interval=b.placement.interval;const auto& g=b.placement;const auto& actual=placement.geometry;
    if(placement.id!=b.id||b.proof!=sealed->proof_||placement.table!=interval->table_||
       placement.begin!=interval->glyph_begin_||placement.end!=interval->glyph_end_||
       actual.x!=g.x||actual.y!=g.y||actual.width!=g.width||actual.height!=g.height||actual.baseline!=g.baseline)
     throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_BINDING");
   }
   passes.push_back({proofs[slot],sealed});
  }
  if(next!=items.size()||bindings.size()!=items.size()||passes.size()!=proofs.size())throw std::invalid_argument("DOCUMENT_FINAL_PROVIDER_INCOMPLETE");
  budget.bytes(1,sizeof(Product)+2*sizeof(void*));
  auto product=std::shared_ptr<const Product>(new Product(std::move(passes),std::move(bindings)));
  // Product allocation precedes last callback-free aggregate audit. Earlier
  // sealed ledgers retained in provider are not a publishable aggregate.
  Access::final_stage_entry(engine,snapshot,this);
  for(const auto& proof:proofs)Access::validate_final_inventory(engine,proof,this);
  if(failure_)std::rethrow_exception(failure_);
  return product;
 }catch(...){if(!failure_)failure_=std::current_exception();throw;}
}

}
