#pragma once
#include "../../layout/document_text_context.h"
#include "real_font_metrics.h"
#include "simple_ltr_run.h"
#include <map>
#include <exception>
namespace aether::rendering {
class DocumentCaptureOperation;
namespace text {
struct DocumentFontCatalog {std::shared_ptr<const FontSource> regular,bold;};
struct DocumentGlyphMeasure {std::uint32_t scalar{},cluster{},glyph{};double prefix{},advance{};std::shared_ptr<const truetype::Outline> outline;std::uint64_t design_prefix{};};
struct DocumentLogicalProduct {
 std::shared_ptr<const truetype::RealFontBinding> binding;
 std::vector<DocumentGlyphMeasure> glyphs;
 std::string source;double width{};float height{},baseline{};
 std::uint64_t design_end{};double design_scale{};
};
struct DocumentOccurrence {
 std::shared_ptr<const aether::layout::DocumentTextFragment> fragment;
 std::shared_ptr<const DocumentLogicalProduct> logical;
 std::shared_ptr<const ShapedRun> measured;
};
class DocumentBoundaryTable final {
 friend class DocumentSliceView;
 friend class DocumentTextProvider;
 friend class aether::layout::DocumentTextTestAccess;
 struct Boundary {std::size_t byte{},glyph{},range{};std::uint64_t design{};};
 const DocumentTextProvider* provider_{};
 std::shared_ptr<const int> operation_;
 DocumentOccurrence occurrence_;
 std::vector<Boundary> boundaries_;
 DocumentBoundaryTable()=default;
public:
 DocumentBoundaryTable(const DocumentBoundaryTable&)=delete;
 std::size_t size()const noexcept{return boundaries_.size();}
};
class DocumentSlicePhase final {
 friend class DocumentTextProvider;
 friend class aether::layout::DocumentTextTestAccess;
 const DocumentTextProvider* provider_{};
 std::shared_ptr<const int> core_,provider_token_;
 std::vector<std::shared_ptr<const DocumentBoundaryTable>> tables_;
 DocumentSlicePhase()=default;
public:
 DocumentSlicePhase(const DocumentSlicePhase&)=delete;
 std::size_t size()const noexcept{return tables_.size();}
};
// Retained immutable view, not final placement authority. A later use must
// revalidate its table through the live provider/transaction; no public issuer.
class DocumentSliceView final {
 friend class DocumentTextProvider;
 friend class aether::layout::DocumentTextTestAccess;
 std::shared_ptr<const DocumentBoundaryTable> table_;
 std::shared_ptr<const DocumentSlicePhase> phase_;
 std::size_t table_index_{},begin_{},end_{};
 double width_{};
 DocumentSliceView(std::shared_ptr<const DocumentBoundaryTable> t,std::shared_ptr<const DocumentSlicePhase> p,std::size_t index,std::size_t b,std::size_t e,double w)
  :table_(std::move(t)),phase_(std::move(p)),table_index_(index),begin_(b),end_(e),width_(w){}
public:
 double width()const noexcept{return width_;}
 std::size_t glyph_count()const noexcept{return end_-begin_;}
 std::string_view source()const noexcept { const auto& a=table_->boundaries_[begin_];const auto& b=table_->boundaries_[end_];return std::string_view(table_->occurrence_.logical->source).substr(a.byte,b.byte-a.byte); }
 std::span<const DocumentGlyphMeasure> glyphs()const noexcept { return std::span<const DocumentGlyphMeasure>(table_->occurrence_.logical->glyphs).subspan(begin_,end_-begin_); }
 std::span<const aether::layout::DocumentTextRange> ranges()const noexcept { const auto& a=table_->boundaries_[begin_];const auto& b=table_->boundaries_[end_];return std::span<const aether::layout::DocumentTextRange>(table_->occurrence_.fragment->ranges).subspan(a.range,b.range-a.range); }
};
struct DocumentPlacementGeometry {double x{},y{},width{},height{},baseline{};};
class DocumentPartition final {
 friend class DocumentPartitionLedger;
 friend class DocumentTextProvider;
 friend class aether::layout::DocumentTextTestAccess;
 struct Placement {
  std::shared_ptr<const DocumentBoundaryTable> table;
  std::uint64_t id{};std::size_t begin{},end{};
  DocumentPlacementGeometry geometry;
 };
 const DocumentTextProvider* provider_{};
 std::shared_ptr<const aether::layout::DocumentFinalPass> proof_;
 std::vector<Placement> placements_;
 DocumentPartition()=default;
public:
 DocumentPartition(const DocumentPartition&)=delete;
 std::size_t size()const noexcept{return placements_.size();}
};
class DocumentPartitionLedger final {
 friend class DocumentTextProvider;
 friend class aether::layout::DocumentTextTestAccess;
 struct Progress {std::shared_ptr<const DocumentBoundaryTable> table;std::size_t next{},expected{};bool suppressed=false;};
 const DocumentTextProvider* provider_{};
 std::shared_ptr<const aether::layout::DocumentFinalPass> proof_;
 std::vector<Progress> progress_;
 std::vector<DocumentPartition::Placement> placements_;
 bool sealed_=false;
 DocumentPartitionLedger()=default;
public:
 DocumentPartitionLedger(const DocumentPartitionLedger&)=delete;
};
struct DocumentProviderWork {std::uint64_t occurrence_reserved{},requests{},measurement_hits{},measurement_misses{},cmap_hits{},cmap_misses{},outline_hits{},outline_misses{},cache_compared_bytes{},measured_scalars{};std::uint64_t binding_geometry_bytes{},outline_geometry_bytes{},font_source_bytes{};bool typography_incomplete=true;};
class DocumentTextPainter;
class DocumentTextProvider final:public aether::layout::DocumentTextBackend {
 friend class aether::rendering::DocumentCaptureOperation;
 friend class DocumentTextPainter;
 friend class aether::layout::DocumentTextTestAccess;
 const DocumentFontCatalog fonts_;
 truetype::Cancellation* cancel_{};
 std::exception_ptr failure_;
 bool drawing_=false;
 struct InstanceEntry {std::shared_ptr<const truetype::RealFontBinding> binding;truetype::FontVerticalMetrics vertical;};
 std::map<std::pair<const FontSource*,std::int32_t>,InstanceEntry> instances_;
 std::map<std::pair<const FontSource*,std::uint32_t>,std::uint16_t> cmap_;
 std::map<std::pair<const FontSource*,std::uint32_t>,std::shared_ptr<const truetype::Outline>> outlines_;
 struct ProductKey {
  const FontInstance* font{};std::uint64_t hash{};float height{},baseline{};
  bool operator<(const ProductKey& b)const{return std::tie(font,hash,height,baseline)<std::tie(b.font,b.hash,b.height,b.baseline);}
 };
 std::map<ProductKey,std::vector<std::shared_ptr<const DocumentLogicalProduct>>> products_;
 std::map<const ShapedRun*,DocumentOccurrence> occurrences_;
 std::map<const ShapedRun*,std::shared_ptr<const DocumentBoundaryTable>> boundary_tables_;
 std::shared_ptr<const int> slice_phase_;
 bool slice_busy_=false;
 bool final_stage_busy_=false;
 bool fit_preparing_=false;
 static double interval_width(const DocumentBoundaryTable&,std::size_t,std::size_t);
 const DocumentBoundaryTable& fit_table(const aether_layout_engine_s*,const std::shared_ptr<const aether::layout::DocumentFitCatalog>&,std::size_t);
 std::map<std::uint64_t,std::shared_ptr<DocumentPartitionLedger>> partitions_;
 // Inactive private API: no collector, painter or admission callsites yet.
 std::shared_ptr<const DocumentBoundaryTable> boundaries(const aether_layout_engine_s*,const std::shared_ptr<const ShapedRun>&);
 std::shared_ptr<const DocumentSlicePhase> seal_slices(const aether_layout_engine_s*,std::span<const std::shared_ptr<const DocumentBoundaryTable>>);
 DocumentSliceView slice(const aether_layout_engine_s*,const std::shared_ptr<const DocumentSlicePhase>&,std::size_t,std::size_t,std::size_t);
 std::shared_ptr<const aether::layout::DocumentConstructionInterval> retain_construction(const aether_layout_engine_s*,const DocumentSliceView&);
 std::shared_ptr<const aether::layout::DocumentTextInterval> retain_interval(const aether_layout_engine_s*,const DocumentSliceView&);
 std::shared_ptr<DocumentPartitionLedger> begin_partition(const aether_layout_engine_s*,std::uint64_t);
 std::shared_ptr<DocumentPartitionLedger> begin_partition(const aether_layout_engine_s*,const std::shared_ptr<const aether::layout::DocumentFinalPass>&);
 std::shared_ptr<DocumentPartitionLedger> register_partition(std::shared_ptr<const aether::layout::DocumentFinalPass>,aether::layout::DocumentTextBudget&);

 std::uint64_t append_partition(const aether_layout_engine_s*,const std::shared_ptr<DocumentPartitionLedger>&,
  const DocumentSliceView&,DocumentPlacementGeometry);
 std::shared_ptr<const DocumentPartition> seal_partition(const aether_layout_engine_s*,const std::shared_ptr<DocumentPartitionLedger>&);
 void partition_identity(const aether_layout_engine_s*,const std::shared_ptr<DocumentPartitionLedger>&,aether::layout::DocumentTextBudget&);
 void partition_geometry(const DocumentBoundaryTable&,std::size_t,std::size_t,const DocumentPlacementGeometry&)const;
 explicit DocumentTextProvider(DocumentFontCatalog,truetype::Cancellation*);
 static std::shared_ptr<DocumentTextProvider> create(DocumentFontCatalog,aether::layout::DocumentTextBudget&,truetype::Cancellation*);
 void check();
 void rethrow_failure()const {if(failure_)std::rethrow_exception(failure_); }
 const InstanceEntry& binding(const aether::layout::BoxStyleSnapshot&,aether::layout::DocumentTextBudget&);
public:
 std::shared_ptr<const aether::layout::DocumentFitCatalog> prepare_fit_catalog(
  const aether_layout_engine_s*,std::span<const std::shared_ptr<const ShapedRun>>)override;
 aether::layout::DocumentFitBoundary fit_boundary(const aether_layout_engine_s*,
  const std::shared_ptr<const aether::layout::DocumentFitCatalog>&,std::size_t,std::size_t)override;
 double fit_width(const aether_layout_engine_s*,
  const std::shared_ptr<const aether::layout::DocumentFitCatalog>&,std::size_t,std::size_t,std::size_t)override;
 std::shared_ptr<const aether::layout::DocumentTextInterval> fit_interval(const aether_layout_engine_s*,
  const std::shared_ptr<const aether::layout::DocumentFitCatalog>&,std::size_t,std::size_t,std::size_t)override;
 std::shared_ptr<const aether::layout::DocumentConstructionInterval> fit_construction(const aether_layout_engine_s*,
  const std::shared_ptr<const aether::layout::DocumentFitCatalog>&,std::size_t,std::size_t,std::size_t)override;
 std::vector<std::shared_ptr<const aether::layout::DocumentTextInterval>> promote_constructions(
  const aether_layout_engine_s*,std::span<const std::shared_ptr<const aether::layout::DocumentConstructionInterval>>)override;
 truetype::RunBudget font_budget;
 DocumentProviderWork work;
 std::pair<float,float> admission_metrics(const aether::layout::BoxStyleSnapshot&,aether::layout::DocumentTextBudget&)override;
 void validate_style(const aether::style::FrozenNodeRef&,const aether::layout::BoxStyleSnapshot&)override;
 // Access validates operation/pass/style before this backend boundary.
 Measurement measure(const std::shared_ptr<const aether::layout::DocumentTextFragment>&,aether::layout::DocumentTextBudget&)override;
 std::shared_ptr<const aether::layout::DocumentFinalizationProduct> stage_final_geometry(
  const aether_layout_engine_s*,const aether::layout::DocumentLayoutSnapshot&,
  std::span<const std::shared_ptr<const aether::layout::DocumentFinalPass>>,aether::layout::DocumentTextBudget&)override;
};
}
}
