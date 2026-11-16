#pragma once
#include "document_viewport_capture.h"
#include <array>
namespace aether::rendering {
// Inactive B2 component. No emitter consumes this plan yet. The same-operation
// authority and budget must outlive it. Pointer values below are keys, not reads.
class DocumentScopePlan final {
 friend class DocumentCaptureOperation;
 friend class aether::layout::DocumentTextTestAccess;
 struct Rule {DocumentScrollRecord record;DocumentPaintContext context;std::array<unsigned,9> expected{};};
 const DocumentViewportAuthority* authority_{};
 aether_layout_result_s* layout_{};aether_style_map_s* map_{};
 aether::layout::DocumentTextBudget* budget_{};
 std::shared_ptr<const int> operation_;
 std::vector<Rule> rules_;
 std::map<const aether_layout_box_s*,std::size_t> index_;
 struct Context {DocumentPaintContext paint;std::size_t own_scroll{},child_scroll{};};
 std::map<const aether_layout_box_s*,Context> contexts_;
 std::vector<DocumentClipEvidence> chains_;
 std::vector<NativeScrollState> states_;
 std::size_t capacity_{};
 DocumentScopePlan(aether_layout_result_s*,aether_style_map_s*,const DocumentViewportAuthority&,
                   aether::layout::DocumentTextBudget&,const std::function<void()>&);
public:
 bool bound_to(const DocumentViewportAuthority& a)const noexcept{return authority_==&a;}
 DocumentScopePlan(const DocumentScopePlan&)=delete;
 bool accepts(aether_layout_result_s*,aether_style_map_s*)const noexcept;
 bool accepts_checked(aether_layout_result_s*,aether_style_map_s*)const;
 std::size_t scope_capacity()const noexcept{return capacity_;}
 const std::vector<NativeScrollState>& states()const noexcept{return states_;}
 // Synchronous bounded verification, no external callbacks and no modification
 // of commands, text placement or shadows. Scope proof, not full render proof.
 void verify(const NativePaintList&)const;
};
}
