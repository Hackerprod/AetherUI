#pragma once
#include "document_scroll_resolution.h"
#include "native_viewport_capture.h"
#include "../layout/document_text_context.h"
#include "../layout/layout_types.h"
namespace aether::rendering {
class DocumentCaptureOperation;
class DocumentShadowPlan;
struct DocumentViewportDiagnostics {
 text::truetype::ViewportOffset offset{};
 double range_x{},range_y{},extent_width{},extent_height{};
 bool scrolling{};aether_rectf viewport{};aether_scrollbar_geometry_t chrome{};
};
// Internal registry record. Pointer/token values never enter public reports.
struct DocumentScrollRecord {
 std::shared_ptr<const int> operation;
 const aether_layout_box_s* box{};
 std::shared_ptr<node_identity_s> identity;
 NativeScrollState state{};
 aether_rectf scrollport{};aether_corner_radii clip_radii{};
 std::size_t parent_scope{};
 int overflow_x{},overflow_y{};
 bool root{},visible{},push_expected{},chrome_expected{};
 double ancestor_x{},ancestor_y{};
};
enum class DocumentClipKind { viewport_outer, viewport_content, nested_padding };
struct DocumentClipEvidence {
 const aether_layout_box_s* owner{};
 DocumentClipKind kind{};std::size_t parent{};
 aether_rectf screen{},intersection{};aether_corner_radii radii{};bool empty{};
};
struct DocumentPaintContext {
 std::shared_ptr<const int> operation;
 const aether_layout_box_s* box{};
 std::size_t chain{},child_chain{};
 double x{},y{},child_x{},child_y{};
 bool painted{};
};
// Independent document capability. Legacy NativeViewportAuthority is unchanged.
// Factory/accepts require live same-thread handles; a pin owns layout storage,
// while the operation owns doc/map handles. No concurrent-mutation guarantee.
// Captured at operation creation, never reconstructed by the factory.
class DocumentCaptureThread final {
 friend class DocumentCaptureOperation;
 friend class DocumentViewportAuthority;
 friend class aether::layout::DocumentTextTestAccess;
 const std::thread::id owner_=std::this_thread::get_id();
 DocumentCaptureThread()=default;
public:
 DocumentCaptureThread(const DocumentCaptureThread&)=delete;
};
enum class DocumentScrollBindingError {root,noncontainer,inactive,out_of_range};
class DocumentViewportAuthority final {
 friend class DocumentScopePlan;
 friend class DocumentShadowPlan;
 friend class DocumentCaptureOperation;friend class aether::layout::DocumentTextTestAccess;
 struct BoxEvidence {
  const aether_layout_box_s* box{};const aether_layout_box_s* parent{};bool abstract_margin_top{},hidden_ancestor{};std::shared_ptr<node_identity_s> identity;
  aether_corner_radii clip_radii{};
  BoxStyleSnapshot effective_style;bool html_element{};
  BoxStyleSnapshot style;std::vector<aether_layout_box_s*> children;
  std::shared_ptr<node_identity_s> presentation_identity;
  std::int64_t node_id{};int overflow_x{},overflow_y{};
  bool visible{},primary{},push_expected{},chrome_expected{};
  aether_scrollbar_geometry_t chrome{};
  text::Placement placement;std::string text;const aether_node_s* presentation_owner{};
  aether_rectf margin{},border{},padding{},content{};
  layout_scroll_proxy_s proxy{};aether_scroll_metrics_t scroll{};
  bool sampled{},transformed{};aether_transform_matrix transform{};
  float opacity{};std::int64_t timestamp{};std::weak_ptr<node_identity_s> visual_identity;
  aether_thicknessf used_border{};float before{},after{},relative_x{},relative_y{};
  bool atomic{},canvas{};layout_box_kind_t kind{};std::uint8_t completion_flags{};
 };
 std::thread::id thread_=std::this_thread::get_id();
 std::shared_ptr<node_identity_s> document_,root_identity_;
 std::uint64_t structure_{},mutation_{},style_{},nonstate_{};
 aether_doc_s* doc_{};aether_style_map_s* map_{};aether_layout_result_s* layout_{};aether_layout_box_s* root_{};
 aether::style::MapVersionRef version_;std::unique_ptr<aether::layout::LayoutOperationPin> pin_;
 std::vector<BoxEvidence> boxes_;
 std::vector<DocumentClipEvidence> clip_chains_;
 std::vector<DocumentPaintContext> paint_contexts_;
 std::map<const aether_layout_box_s*,std::size_t> paint_context_index_;
 bool registry_candidate_{};
 std::shared_ptr<const int> registry_operation_;
 std::vector<DocumentScrollRecord> registry_;
 std::map<const aether_layout_box_s*,std::size_t> registry_index_;
 std::uint64_t allocation_revision_{};
 std::vector<const aether_layout_box_s*> allocations_;
 std::vector<std::pair<const aether_node_s*,const aether_layout_box_s*>> primary_;
 aether::layout::LayoutAssetsRef assets_;float root_font_size_{};
 std::unique_ptr<DocumentScrollResolution> requests_;
 NativeScrollState state_{};DocumentViewportDiagnostics diagnostics_{};
 DocumentViewportAuthority()=default;
 static DocumentViewportAuthority create(aether_doc_s*,aether_layout_result_s*,aether_style_map_s*,std::uint32_t,std::uint32_t,text::truetype::ViewportOffset,aether::layout::DocumentTextBudget&,const std::function<void()>&);
 static DocumentViewportAuthority create_registry_candidate(aether_doc_s*,aether_layout_result_s*,aether_style_map_s*,std::uint32_t,std::uint32_t,text::truetype::ViewportOffset,aether::layout::DocumentTextBudget&,const std::function<void()>&);
 static DocumentViewportAuthority create_registry_requests(aether_doc_s*,aether_layout_result_s*,aether_style_map_s*,std::uint32_t,std::uint32_t,text::truetype::ViewportOffset,const DocumentScrollRequests&,const DocumentCaptureThread&,aether::layout::DocumentTextBudget&,const std::function<void()>&);
 static DocumentViewportAuthority create_impl(aether_doc_s*,aether_layout_result_s*,aether_style_map_s*,std::uint32_t,std::uint32_t,text::truetype::ViewportOffset,aether::layout::DocumentTextBudget&,const std::function<void()>&,bool,const DocumentScrollRequests* = nullptr,const DocumentCaptureThread* = nullptr);
public:
 DocumentViewportAuthority(const DocumentViewportAuthority&)=delete;
 DocumentViewportAuthority(DocumentViewportAuthority&&) noexcept=default;
 DocumentViewportAuthority& operator=(DocumentViewportAuthority&&)=delete;
 // O(1) lifetime/revision/epoch stamp only. It does not validate geometry.
 bool stamp_current(aether_layout_result_s*,aether_style_map_s*)const noexcept;
 bool accepts(aether_layout_result_s*,aether_style_map_s*)const noexcept;
 bool accepts_checked(aether_layout_result_s*,aether_style_map_s*)const;
 bool request_bound()const noexcept{return bool(requests_);}
 std::span<const std::uint8_t> request_manifest()const noexcept{return requests_?requests_->requests().canonical():std::span<const std::uint8_t>{};}
 std::size_t request_count()const noexcept{return requests_?requests_->size():0;}
 const DocumentPaintContext* lookup_paint_context(const aether_layout_box_s* box)const noexcept {
  const auto i=paint_context_index_.find(box);return i==paint_context_index_.end()?nullptr:&paint_contexts_[i->second];
 }
 const std::vector<DocumentClipEvidence>& clip_chains()const noexcept{return clip_chains_;}
 bool registry_candidate()const noexcept{return registry_candidate_;}
 const std::vector<DocumentScrollRecord>& registry()const noexcept{return registry_;}
 const DocumentScrollRecord* lookup_scroll(const aether_layout_box_s* box)const noexcept {
  const auto i=registry_index_.find(box);return i==registry_index_.end()?nullptr:&registry_[i->second];
 }
 const NativeScrollState& state()const noexcept{return state_;}
 const DocumentViewportDiagnostics& diagnostics()const noexcept{return diagnostics_;}
 aether_rectf viewport()const noexcept{return diagnostics_.viewport;}
 std::int64_t root_id()const noexcept{return state_.id;}
};
aether_status_t build_native_document_capture(aether_display_list_builder_t,aether_layout_result_t,aether_style_map_t,const DocumentViewportAuthority&,std::unique_ptr<NativePaintList>&,const DocumentShadowPlan* = nullptr)noexcept;
class DocumentScopePlan;
aether_status_t build_native_document_registry_candidate(aether_display_list_builder_t,aether_layout_result_t,aether_style_map_t,
 const DocumentViewportAuthority&,const DocumentScopePlan&,const DocumentShadowPlan&,std::unique_ptr<NativePaintList>&)noexcept;

// Internal request-bound entry; preserves typed quota/allocation exceptions.
aether_status_t build_native_document_requests_checked(aether_display_list_builder_t,aether_layout_result_t,aether_style_map_t,
 const DocumentViewportAuthority&,const DocumentScopePlan&,const DocumentShadowPlan&,std::unique_ptr<NativePaintList>&);

}
