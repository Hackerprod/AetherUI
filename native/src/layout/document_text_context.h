#pragma once
#include "operation_pin.h"
#include "../rendering/text/shaped_run.h"
#include <functional>
#include <exception>
#include <map>
#include <thread>
#include <unordered_set>
namespace aether::rendering {class DocumentCaptureOperation;}
namespace aether::rendering::text {class DocumentTextPainter;class DocumentTextProvider;class DocumentBoundaryTable;class DocumentSlicePhase;class DocumentPartition;}
namespace aether::layout {
class DocumentTextNormalizer;
class DocumentTextAccess;
class DocumentTextTestAccess;
class DocumentTextTransaction;
class DocumentAttemptCapsule;
class DocumentAttemptControl;
class DocumentAttemptLayout;
class DocumentTextBackend;
// Each dimension is independent. Metadata counts owned logical payload, not RSS.
struct DocumentTextCancelled {};
struct DocumentTextBudget {
 std::uint64_t decoded_scalars{},normalization_steps{},measurement_requests{},measured_scalar_visits{},source_fragments{},retained_fragments{},placed_glyphs{},font_instances{},unique_outlines{},metadata_bytes{};
 static void add(std::uint64_t& value,std::uint64_t delta,std::uint64_t limit){if(value>limit||delta>limit-value)throw std::length_error("DOCUMENT_TEXT_LIMIT");value+=delta;}
 void decoded(std::uint64_t n){add(decoded_scalars,n,262144);}
 void normalized(std::uint64_t n){add(normalization_steps,n,2097152);}
 void request(){add(measurement_requests,1,262144);}
 void visited(std::uint64_t n){add(measured_scalar_visits,n,4194304);}
 void source(){add(source_fragments,1,32768);}
 void retained(){add(retained_fragments,1,65536);}
 void placed(std::uint64_t n){add(placed_glyphs,n,1048576);}
 void instance(){add(font_instances,1,16);}
 void outline(){add(unique_outlines,1,256);}
 void bytes(std::uint64_t count,std::uint64_t size=1){constexpr std::uint64_t cap=32ULL*1024*1024;if(metadata_bytes>cap||(size&&count>(cap-metadata_bytes)/size))throw std::length_error("DOCUMENT_TEXT_BYTES");metadata_bytes+=count*size;}
};
struct DocumentTextRange {
 std::uint64_t source_ordinal{};
 std::size_t input_begin{},input_length{},output_begin{},output_length{};
};
// Core-issued proof of one actual original TEXT node. No public constructor and
// no raw node getter. The issuing transaction owns DOM/map throughout its use.
class DocumentTextSource final {
 friend class DocumentTextAccess;friend class DocumentTextTransaction;friend class DocumentTextTestAccess;
 const std::shared_ptr<const int> operation_;
 // DOM anchor only; anonymous effective style is separately authenticated.
 const aether::style::FrozenNodeRef owner_;
 const std::shared_ptr<node_identity_s> layout_owner_;
 const std::shared_ptr<node_identity_s> text_identity_;
 const aether_node_s* const text_node_;
 DocumentTextSource(std::shared_ptr<const int>,aether::style::FrozenNodeRef,
  std::shared_ptr<node_identity_s>,std::shared_ptr<node_identity_s>,const aether_node_s*,std::uint64_t,std::size_t,int,std::string);
public:
 enum class Kind {text,comment,line_break};
 const std::uint64_t ordinal;
 const std::size_t dom_ordinal;
 const Kind kind;
 const std::string decoded;
 DocumentTextSource(const DocumentTextSource&)=delete;
};
// Immutable normalized fragment provenance. Dropped ranges are retained by the
// normalization record even when no measured fragment exists for them.
class DocumentTextFragment final {
 friend class aether::rendering::text::DocumentTextPainter;
 friend class DocumentTextAccess;friend class DocumentTextTransaction;friend class DocumentTextTestAccess;
 const std::shared_ptr<const int> operation_;
 // DOM anchor only; anonymous effective style is separately authenticated.
 const aether::style::FrozenNodeRef owner_;
 const std::shared_ptr<node_identity_s> layout_owner_;
 const BoxStyleSnapshot style_;
 const std::vector<std::shared_ptr<const DocumentTextSource>> sources_;
 DocumentTextFragment(std::shared_ptr<const int>,aether::style::FrozenNodeRef,
  std::shared_ptr<node_identity_s>,BoxStyleSnapshot,std::vector<std::shared_ptr<const DocumentTextSource>>,
  std::uint64_t,std::uint64_t,std::string,std::vector<DocumentTextRange>);
public:
 const std::uint64_t occurrence;
 const std::uint64_t normalization_pass;
 const std::string normalized;
 const std::vector<DocumentTextRange> ranges;
 DocumentTextFragment(const DocumentTextFragment&)=delete;
 const BoxStyleSnapshot& style()const noexcept{return style_;}
 const aether::style::FrozenNodeRef& owner_record()const noexcept{return owner_;}
};
// This abstract boundary does not link legacy core against TrueType. Only the
// opt-in renderer implements it; authenticated fragments are not char* callbacks.
// Core-owned inventory, not a placement/paint certificate. Empty fragments and
// dropped events are covered by the completed normalization proof; nonempty
// fragments (including no-ink spaces) remain mandatory partition obligations.
class DocumentConstructionInterval;
// Issued only at the actual normalizer/resolver soft-wrap branch. Owns source
// transports and the complete pending/next units, not live boxes or callbacks.
// Geometry, never a text occurrence. Issued by the authentic inline resolver
// for one retained child layout and its parent normalization-unit ordinal.
class DocumentAtomicInlineEvent final {
 friend class DocumentTextNormalizer;friend class DocumentTextAccess;
 std::shared_ptr<const int> operation_;
 std::shared_ptr<node_identity_s> owner_token_;
 const aether_node_s* owner_{};
 const aether_layout_box_s* box_{}; // identity key only; membership before dereference
 std::uint64_t pass_{},ordinal_{},parent_attempt_{},child_attempt_{};
 double width_{},height_{},baseline_{};
 float projected_width_{};
 DocumentAtomicInlineEvent()=default;
};
class DocumentSoftWrapPlan final {
 friend class DocumentTextNormalizer;friend class DocumentTextAccess;friend class DocumentTextTestAccess;
 std::vector<std::shared_ptr<const DocumentConstructionInterval>> line_,pending_,next_;
 std::vector<std::shared_ptr<const DocumentAtomicInlineEvent>> line_atoms_;
 std::vector<std::uint8_t> line_order_;
 std::shared_ptr<const DocumentAtomicInlineEvent> next_atom_;
 double available_{};
 std::uint64_t line_number_{},attempt_{},pass_{};
 std::shared_ptr<const int> operation_;
 enum class Reason { normal_pending_soft_wrap };
 Reason reason_=Reason::normal_pending_soft_wrap;
 DocumentSoftWrapPlan()=default;
};
class DocumentSuppressedSeparator final {
 friend class DocumentTextAccess;
 friend class aether::rendering::text::DocumentTextProvider;
 const std::shared_ptr<const int> operation_;
 const std::shared_ptr<const DocumentTextFragment> fragment_;
 const std::uint64_t attempt_,line_;
 const std::shared_ptr<const DocumentSoftWrapPlan> plan_;
 DocumentSuppressedSeparator(std::shared_ptr<const int> op,std::shared_ptr<const DocumentTextFragment> fragment,
  std::uint64_t attempt,std::uint64_t line,std::shared_ptr<const DocumentSoftWrapPlan> plan):operation_(std::move(op)),fragment_(std::move(fragment)),attempt_(attempt),line_(line),plan_(std::move(plan)){}
};
class DocumentFinalPass final {
 friend class DocumentTextAccess;
 friend class DocumentTextTestAccess;
 friend class aether::rendering::text::DocumentTextProvider;
 std::shared_ptr<const int> operation_;
 std::uint64_t pass_{},generation_{};
 std::uint64_t promoted_attempt_=0;
 std::shared_ptr<const int> selected_nonce_;
 std::vector<std::shared_ptr<const DocumentSuppressedSeparator>> suppressed_;
 std::vector<std::shared_ptr<const DocumentTextFragment>> fragments_;
 DocumentFinalPass()=default;
public:
 DocumentFinalPass(const DocumentFinalPass&)=delete;
};
// Inactive immutable transport. Issuance is private to the authenticated backend;
// this is NOT a ledger seal, callback phase or paint authority. The table is an
// opaque retained resource in core. Future issuer must precharge object storage
// and prove exact table/interval/metrics against A before calling the constructor.
// Tentative, immutable geometry transport. This type cannot be passed to any
// final interval/partition/paint API. Attempt binding and promotion remain core
// responsibilities, and must prove complete selected inventory before conversion.
class DocumentConstructionInterval final {
 friend class DocumentTextAccess;
 friend class DocumentTextTestAccess;
 friend class aether::rendering::text::DocumentTextProvider;
 const std::shared_ptr<const DocumentTextFragment> fragment_;
 const std::shared_ptr<const aether::rendering::text::ShapedRun> run_;
 const std::shared_ptr<const aether::rendering::text::DocumentBoundaryTable> table_;
 const std::size_t glyph_begin_,glyph_end_,byte_begin_,byte_end_;
 const double exact_width_;
 const float projected_width_;
 DocumentConstructionInterval(std::shared_ptr<const DocumentTextFragment>,
  std::shared_ptr<const aether::rendering::text::ShapedRun>,
  std::shared_ptr<const aether::rendering::text::DocumentBoundaryTable>,
  std::size_t,std::size_t,std::size_t,std::size_t,double);
public:
 DocumentConstructionInterval(const DocumentConstructionInterval&)=delete;
 double exact_width()const noexcept{return exact_width_;}
 float projected_width()const noexcept{return projected_width_;}
};
class DocumentTextInterval final {
 friend class aether::rendering::text::DocumentTextPainter;
 friend class DocumentTextAccess;
 friend class DocumentTextTestAccess;
 friend class aether::rendering::text::DocumentTextProvider;
 const std::shared_ptr<const DocumentTextFragment> fragment_;
 const std::shared_ptr<const aether::rendering::text::ShapedRun> run_;
 const std::shared_ptr<const aether::rendering::text::DocumentBoundaryTable> table_;
 const std::size_t glyph_begin_,glyph_end_,byte_begin_,byte_end_;
 const double exact_width_;
 const float projected_width_;
 DocumentTextInterval(std::shared_ptr<const DocumentTextFragment>,
  std::shared_ptr<const aether::rendering::text::ShapedRun>,
  std::shared_ptr<const aether::rendering::text::DocumentBoundaryTable>,
  std::size_t,std::size_t,std::size_t,std::size_t,double);
public:
 DocumentTextInterval(const DocumentTextInterval&)=delete;
 // Transport check only; completed geometry/product proof remains required by painter.
 bool matches_source(const std::shared_ptr<const aether::rendering::text::ShapedRun>&,std::string_view)const noexcept;
 double exact_width()const noexcept{return exact_width_;}
 float projected_width()const noexcept{return projected_width_;}
};
// Inactive geometry snapshot: owns a result pin, never stored by engine/result.
// Caller must destroy this scope before any direct delete of the result. Future
// wiring stages it locally and publishes only after complete_layout succeeds.
class DocumentGeometryCertificate final {
 friend class DocumentTextAccess;
 friend class DocumentLayoutSnapshot;
 struct Values;
 std::unique_ptr<const Values> values_;
 DocumentGeometryCertificate();
public:
 ~DocumentGeometryCertificate();
 DocumentGeometryCertificate(const DocumentGeometryCertificate&)=delete;
};
class DocumentLayoutSnapshot final {
 friend class DocumentTextAccess;
 friend class DocumentTextTestAccess;
 struct State;
 std::unique_ptr<State> state_;
 DocumentLayoutSnapshot();
public:
 ~DocumentLayoutSnapshot();
 DocumentLayoutSnapshot(const DocumentLayoutSnapshot&)=delete;
};
// Owned values extracted from the validated snapshot. The raw key is identity
// only; consumers never dereference it. No snapshot/pin/result ownership here.
struct DocumentFinalIntervalPlacement {
 const aether_layout_box_s* box{};
 std::shared_ptr<const DocumentTextInterval> interval;
 double x{},y{},width{},height{},baseline{};
};
// Inactive aggregate protocol, not an issued paint authority. Only a future
// reviewed provider issuer may construct this from real sealed partitions and
// the exact core-owned complete inventory. It must precharge all storage first.
// The concrete field graph cannot retain a result, geometry snapshot or pin.
class DocumentFinalizationProduct final {
 friend class aether::rendering::text::DocumentTextPainter;
 friend class DocumentTextAccess;
 friend class DocumentTextTestAccess;
 friend class aether::rendering::text::DocumentTextProvider;
 struct Pass {
  std::shared_ptr<const DocumentFinalPass> proof;
  std::shared_ptr<const aether::rendering::text::DocumentPartition> partition;
 };
 struct Binding {
  DocumentFinalIntervalPlacement placement;
  std::shared_ptr<const DocumentFinalPass> proof;
  std::uint64_t id{};
 };
 const std::vector<Pass> passes_;
 const std::vector<Binding> bindings_;
 explicit DocumentFinalizationProduct(std::vector<Pass> passes,std::vector<Binding> bindings={})
  :passes_(std::move(passes)),bindings_(std::move(bindings)){}
public:
 DocumentFinalizationProduct(const DocumentFinalizationProduct&)=delete;
};
// Caller-local rollback holder. It deliberately does NOT own the separate
// snapshot/pin; that local scope must end before every delete result. No getters
// or public construction means a backend cannot expose this staging as output.
// Destruction discards the candidate, not consumed budget/provider ledger IDs.
class DocumentFinalizationPending final {
 friend class DocumentTextAccess;
 friend class DocumentTextTestAccess;
 const std::shared_ptr<const DocumentFinalizationProduct> product_;
 std::shared_ptr<const int> nonce_,selected_nonce_;
 std::shared_ptr<const DocumentGeometryCertificate> geometry_;
 explicit DocumentFinalizationPending(std::shared_ptr<const DocumentFinalizationProduct> product)
  :product_(std::move(product)){
  if(!product_)throw std::invalid_argument("DOCUMENT_FINAL_PRODUCT_MISSING");
 }
public:
 ~DocumentFinalizationPending()noexcept=default;
 DocumentFinalizationPending(const DocumentFinalizationPending&)=delete;
};
// Inactive immutable query capability. Opaque phase retains original resources;
// no raw layout/result ownership, public constructor, mutable metrics or text.
class DocumentFitCatalog final {
 friend class aether::rendering::text::DocumentTextProvider;
 const DocumentTextBackend* backend_{};
 std::shared_ptr<const aether::rendering::text::DocumentSlicePhase> phase_;
 DocumentFitCatalog()=default;
public:
 DocumentFitCatalog(const DocumentFitCatalog&)=delete;
};
struct DocumentFitBoundary {std::size_t count{},byte{},glyph{};};
class DocumentTextBackend {
public:
 virtual ~DocumentTextBackend()=default;
 // Private protocol, inactive until reviewed source-adapter wiring. Default
 // rejects; implementation must validate thread/operation before state access.
 virtual std::shared_ptr<const DocumentFitCatalog> prepare_fit_catalog(
  const aether_layout_engine_s*,std::span<const std::shared_ptr<const aether::rendering::text::ShapedRun>>){
  throw std::invalid_argument("DOCUMENT_FIT_UNSUPPORTED");
 }
 virtual DocumentFitBoundary fit_boundary(const aether_layout_engine_s*,
  const std::shared_ptr<const DocumentFitCatalog>&,std::size_t,std::size_t){
  throw std::invalid_argument("DOCUMENT_FIT_UNSUPPORTED");
 }
 virtual double fit_width(const aether_layout_engine_s*,
  const std::shared_ptr<const DocumentFitCatalog>&,std::size_t,std::size_t,std::size_t){
  throw std::invalid_argument("DOCUMENT_FIT_UNSUPPORTED");
 }
 virtual std::shared_ptr<const DocumentTextInterval> fit_interval(const aether_layout_engine_s*,
  const std::shared_ptr<const DocumentFitCatalog>&,std::size_t,std::size_t,std::size_t){
  throw std::invalid_argument("DOCUMENT_FIT_UNSUPPORTED");
 }

 virtual std::shared_ptr<const DocumentConstructionInterval> fit_construction(
  const aether_layout_engine_s*,const std::shared_ptr<const DocumentFitCatalog>&,
  std::size_t,std::size_t,std::size_t){
  throw std::invalid_argument("DOCUMENT_CONSTRUCTION_UNSUPPORTED");
 }

 virtual std::vector<std::shared_ptr<const DocumentTextInterval>> promote_constructions(
  const aether_layout_engine_s*,std::span<const std::shared_ptr<const DocumentConstructionInterval>>){
  throw std::invalid_argument("DOCUMENT_PROMOTION_UNSUPPORTED");
 }
 virtual aether::rendering::text::Measurement measure(
  const std::shared_ptr<const DocumentTextFragment>&,DocumentTextBudget&)=0;
 virtual std::pair<float,float> admission_metrics(const BoxStyleSnapshot&,DocumentTextBudget&){
  throw std::invalid_argument("DOCUMENT_MARKER_METRICS_UNSUPPORTED");
 }
 virtual void validate_style(const aether::style::FrozenNodeRef&,const BoxStyleSnapshot&)=0;
 // Synchronous private operation protocol; currently NO callsites. Arguments
 // remain exclusively core-owned and immutable through callbacks. Backend must
 // not retain borrowed snapshot/span/engine/budget or destroy/reenter caller.
 // A future issuer's returned product owns resources but never layout geometry.
 // Default rejects: absence of final passes/capability is NOT slicing success.
 virtual std::shared_ptr<const DocumentFinalizationProduct> stage_final_geometry(
  const aether_layout_engine_s*,const DocumentLayoutSnapshot&,
  std::span<const std::shared_ptr<const DocumentFinalPass>>,DocumentTextBudget&){
  throw std::invalid_argument("DOCUMENT_FINAL_GEOMETRY_UNSUPPORTED");
 }

};
class DocumentTextTransaction final {
 friend class DocumentTextAccess;friend class DocumentTextTestAccess;
 mutable std::shared_ptr<const int> slice_phase_;
 bool slice_auditing_=false;
 bool final_issuing_=false;
 bool final_geometry_mode_=false,finalizing_=false;
 // Inactive: allocated only by the private genuine-operation factory.
 std::shared_ptr<DocumentAttemptCapsule> attempt_capsule_;
 bool drawing_mode_=false;
 std::unique_ptr<DocumentAttemptLayout> attempt_layout_;
 mutable std::shared_ptr<const int> final_ready_,final_ready_selection_;
 mutable const DocumentGeometryCertificate* final_ready_geometry_=nullptr;
 std::shared_ptr<const int> completed_selection_;
 std::shared_ptr<const DocumentGeometryCertificate> completed_geometry_;
 mutable const aether_layout_result_s* final_ready_layout_=nullptr;
 mutable LayoutAssetsRef final_ready_assets_;
 mutable const DocumentFinalizationProduct* final_ready_product_=nullptr;
 mutable std::uint64_t final_ready_revision_=0;
 std::shared_ptr<const DocumentFinalizationProduct> completed_product_;
 void invalidate_final_ready()const noexcept;

 const std::thread::id thread_=std::this_thread::get_id();
 const std::shared_ptr<const int> operation_=std::make_shared<const int>(0);
 const std::shared_ptr<node_identity_s> document_;
 const aether::style::MapVersionRef version_;
 const CallbackEpochGuard epochs_;
 const std::uint64_t nonstate_;
 const std::shared_ptr<DocumentTextBackend> backend_;
 const std::function<bool()> cancelled_;
 // Borrowed only during the single active layout. Its overlay owns anonymous records.
 const StyleLookup* styles_=nullptr;
 std::map<std::pair<const aether_node_s*,const aether_node_s*>,std::shared_ptr<const DocumentTextSource>> sources_;
 std::unordered_set<const aether_node_s*> decoded_nodes_;
 std::unordered_map<const aether_node_s*,std::size_t> dom_order_;
 std::vector<std::shared_ptr<const DocumentTextSource>> sources_by_ordinal_;
 std::vector<std::shared_ptr<const DocumentTextFragment>> fragments_;
 std::vector<DocumentTextRange> dropped_;
 struct Pass {std::vector<std::shared_ptr<const DocumentTextSource>> sources;std::size_t fragment_begin{},fragment_end{},dropped_begin{},dropped_end{};bool complete=false;std::uint64_t final_generation=0;std::uint64_t construction_attempt=0;};
 std::vector<Pass> passes_;
 std::map<std::uint64_t,std::shared_ptr<const DocumentFinalPass>> promoted_passes_;
 bool promotions_committed_=false;
 struct NormalizedUnit {std::uint64_t ordinal;bool separator;};
 std::map<std::uint64_t,NormalizedUnit> normalized_units_;
 std::map<std::pair<std::uint64_t,std::uint64_t>,std::shared_ptr<const DocumentAtomicInlineEvent>> atomic_units_;
 std::map<std::uint64_t,std::shared_ptr<const DocumentSuppressedSeparator>> suppressed_separators_;
 bool pass_active_=false;
 std::uint64_t next_source_{},next_fragment_{};
 std::uint64_t next_final_generation_=1,next_placement_=1;
 std::exception_ptr failure_;
 LayoutAssetsRef completed_assets_;
 const aether_layout_result_s* completed_layout_=nullptr;
 aether_layout_result_s* active_layout_=nullptr; // non-owning, engine-owned during construction
 bool layout_completed_=false;
 bool active_=false;
 bool begun_once_=false;
 mutable bool cancelled_latched_=false;
 DocumentTextTransaction(aether_doc_s*,aether_style_map_s*,std::shared_ptr<DocumentTextBackend>,std::function<bool()>,DocumentTextBudget);
 void valid()const;
 void check()const;
public:
 DocumentTextBudget budget;
 ~DocumentTextTransaction()noexcept;
 DocumentTextTransaction(const DocumentTextTransaction&)=delete;
};
// Named private facade. Installation remains accessible only to the capture
// operation/test seam. Engine integration will use authenticated collection APIs.
class DocumentTextAccess final {
 friend class DocumentAttemptControl;
 friend class DocumentAttemptLayout;
 friend class aether::rendering::text::DocumentTextProvider;
 friend class aether::rendering::text::DocumentTextPainter;
 friend class DocumentTextNormalizer;
 friend class aether::rendering::DocumentCaptureOperation;
 friend class DocumentTextTestAccess;
 static bool install(aether_layout_engine_s&,aether_doc_s*,aether_style_map_s*,
  std::shared_ptr<DocumentTextBackend>,std::function<bool()>,DocumentTextBudget initial={});
 static bool install_tracked(aether_layout_engine_s&,aether_doc_s*,aether_style_map_s*,
  std::shared_ptr<DocumentTextBackend>,std::function<bool()>,DocumentTextBudget&,DocumentTextBudget*&);
public:
 static bool active(const aether_layout_engine_s*) noexcept;
 static void begin(aether_layout_engine_s*,aether_doc_s*,const StyleLookup&,aether_layout_result_s* = nullptr);
 static void finish(aether_layout_engine_s*) noexcept;
 static void complete_layout(aether_layout_engine_s*,const aether_layout_result_s*);
 // Internal engine-only opt-in path. Current capture wrapper never enables it.
 static bool final_geometry_mode(const aether_layout_engine_s*);
 static bool drawing_mode(const aether_layout_engine_s*);
 static void begin_drawing_attempts(aether_layout_engine_s*);
 static void promote_drawing_geometry(aether_layout_engine_s*,aether_layout_result_s*);
 static DocumentAttemptLayout& drawing_attempts(const aether_layout_engine_s*);
 static void discard_drawing_geometry(const aether_layout_engine_s*,const aether_layout_box_s*)noexcept;

 static std::unique_ptr<DocumentFinalizationPending> prepare_final_layout(aether_layout_engine_s*,aether_layout_result_s*);
 static bool commit_final_layout(aether_layout_engine_s*,aether_layout_result_s*,const std::unique_ptr<DocumentFinalizationPending>&)noexcept;
 static void capture_failure(aether_layout_engine_s*) noexcept;
 static void check(const aether_layout_engine_s*);
 // Collector precondition: owner/node are live handles in this active layout.
 static std::shared_ptr<const DocumentTextSource> source(const aether_layout_engine_s*,
  aether_node_s* owner,aether_node_s* text,const BoxStyleSnapshot&);
private:
 static std::pair<float,float> validate_admission_marker(const aether_layout_engine_s*,aether_node_s*,const BoxStyleSnapshot&,bool parent_redundant=true);
 static void validate_interval_geometry(const aether_layout_engine_s*,const aether_layout_box_s*,const DocumentTextBackend*);
 static void enable_final_geometry(aether_layout_engine_s*);
 static void enable_drawing(aether_layout_engine_s*);
 static void compare_geometry_values(const aether_layout_engine_s*,const DocumentGeometryCertificate::Values&,const DocumentTextBackend*,DocumentTextBudget&,bool);
 static void initialize_attempt_topology(aether_layout_engine_s*);
 static void bind_construction_pass(const aether_layout_engine_s*,std::uint64_t);
 static void bind_latest_construction_pass(const aether_layout_engine_s*);
 static DocumentTextBackend& drawing_backend(const aether_layout_engine_s*);
 static DocumentTextBudget& drawing_budget(const aether_layout_engine_s*);
 static void register_normalized_unit(const aether_layout_engine_s*,const std::shared_ptr<const DocumentTextFragment>&,std::uint64_t,bool);
 static std::shared_ptr<const DocumentAtomicInlineEvent> bind_atomic_unit(const aether_layout_engine_s*,aether_layout_box_s*,std::uint64_t,double,double,double);
 static void validate_atomic_event(const aether_layout_engine_s*,const std::shared_ptr<const DocumentAtomicInlineEvent>&);
 static void seal_soft_wrap_plan(const aether_layout_engine_s*,const std::shared_ptr<DocumentSoftWrapPlan>&);
 static void validate_soft_wrap_plan(const aether_layout_engine_s*,const std::shared_ptr<const DocumentSoftWrapPlan>&);
 static void suppress_normal_separator(const aether_layout_engine_s*,const std::shared_ptr<const DocumentTextFragment>&,const std::shared_ptr<const DocumentSoftWrapPlan>&);

 static void promote_selected_passes(aether_layout_engine_s*);

 static std::pair<std::shared_ptr<DocumentAttemptCapsule>,DocumentTextBudget*> attempt_control_entry(aether_layout_engine_s*);
 static std::shared_ptr<DocumentAttemptCapsule> attempt_selection_entry(const aether_layout_engine_s*);
 static aether_layout_result_s* construction_layout(const aether_layout_engine_s*);
 static void collector_entry(const aether_layout_engine_s*);
 // Private live-layout gate. Full source checks are callback-free after check().
 static std::shared_ptr<const DocumentLayoutSnapshot> capture_geometry(const aether_layout_engine_s*,aether_layout_result_s*,const DocumentTextBackend*,bool);
 static std::shared_ptr<const DocumentLayoutSnapshot> capture_construction_geometry(const aether_layout_engine_s*,aether_layout_result_s*,const DocumentTextBackend*);
 static std::shared_ptr<const DocumentLayoutSnapshot> capture_final_geometry(const aether_layout_engine_s*,aether_layout_result_s*,const DocumentTextBackend*);
 static void validate_final_geometry(const aether_layout_engine_s*,const std::shared_ptr<const DocumentLayoutSnapshot>&,const DocumentTextBackend*);
 static void validate_final_geometry(const aether_layout_engine_s*,const DocumentLayoutSnapshot&,const DocumentTextBackend*);
 static DocumentTextBudget& final_stage_entry(const aether_layout_engine_s*,const DocumentLayoutSnapshot&,const DocumentTextBackend*);
 static std::vector<DocumentFinalIntervalPlacement> final_snapshot_intervals(const aether_layout_engine_s*,const DocumentLayoutSnapshot&,const DocumentTextBackend*);

 // Inactive final-pass authority; current normalizer still uses begin_pass.
 static void begin_final_pass(const aether_layout_engine_s*,const std::vector<std::shared_ptr<const DocumentTextSource>>&);
 static std::shared_ptr<const DocumentFinalPass> final_inventory(const aether_layout_engine_s*,std::uint64_t,const DocumentTextBackend*);
 static void validate_final_inventory(const aether_layout_engine_s*,const std::shared_ptr<const DocumentFinalPass>&,const DocumentTextBackend*);
 static std::uint64_t reserve_placement(const aether_layout_engine_s*,const std::shared_ptr<const DocumentFinalPass>&,const DocumentTextBackend*,std::uint64_t);
 static void final_pass_identity(const aether_layout_engine_s*,const std::shared_ptr<const DocumentFinalPass>&,const DocumentTextBackend*);
 static std::shared_ptr<const int> final_fragment_identity(const aether_layout_engine_s*,const std::shared_ptr<const DocumentTextFragment>&,const DocumentTextBackend*);
 static DocumentTextBudget& slice_entry(const aether_layout_engine_s*,const DocumentTextBackend*);
 static std::shared_ptr<const int> seal_slice_phase(const aether_layout_engine_s*,
  std::span<const std::shared_ptr<const DocumentTextFragment>>,const DocumentTextBackend*);
 static void validate_slice_phase(const aether_layout_engine_s*,const std::shared_ptr<const int>&,
  const std::shared_ptr<const DocumentTextFragment>&,const DocumentTextBackend*,DocumentTextBudget*&);
 static std::shared_ptr<const int> slice_token(const aether_layout_engine_s*,
  const std::shared_ptr<const DocumentTextFragment>&,const DocumentTextBackend*,DocumentTextBudget*&);
 static std::shared_ptr<const int> paint_token(const aether_layout_engine_s*,const aether_layout_result_s*,const DocumentTextBackend*);
 static DocumentTextBudget& paint_budget(const aether_layout_engine_s*,const aether_layout_result_s*,const DocumentTextBackend*);
 static std::shared_ptr<const int> drawing_paint_token(const aether_layout_engine_s*,const aether_layout_result_s*,const DocumentTextBackend*);
 static DocumentTextBudget& drawing_paint_budget(const aether_layout_engine_s*,const aether_layout_result_s*,const DocumentTextBackend*);
 static std::shared_ptr<const DocumentFinalizationProduct> drawing_product(const aether_layout_engine_s*,const aether_layout_result_s*,const DocumentTextBackend*);

 static void rethrow_failure(const aether_layout_engine_s*);
 // Only the existing source-unit collector (named normalizer implementation)
 // can issue normalized fragments. Callers cannot supply arbitrary text/ranges.
 // Collector precondition: owner is live in the active layout/overlay.
 static std::shared_ptr<const DocumentTextFragment> fragment(const aether_layout_engine_s*,
  const std::vector<std::shared_ptr<const DocumentTextSource>>&,
  aether_node_s* owner,const BoxStyleSnapshot&,const std::string&,const std::vector<DocumentTextRange>&);
 static void dropped(const aether_layout_engine_s*,const std::vector<DocumentTextRange>&);
 static DocumentTextBudget& normalization_budget(const aether_layout_engine_s*);
 static void validate_source(const aether_layout_engine_s*,const std::shared_ptr<const DocumentTextSource>&);
 static void begin_pass(const aether_layout_engine_s*,const std::vector<std::shared_ptr<const DocumentTextSource>>&);
 static void finish_pass(const aether_layout_engine_s*);
 static void abort_pass(const aether_layout_engine_s*) noexcept;
public:
 static aether::rendering::text::Measurement measure(const aether_layout_engine_s*,
  const std::shared_ptr<const DocumentTextFragment>&,const BoxStyleSnapshot&,aether_node_s*);
};
}
