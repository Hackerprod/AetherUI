#pragma once
#include "document_viewport_capture.h"
namespace aether::rendering {
struct DocumentShadowUnsupported {};
// Created only after authenticated layout and with the same transaction budget.
// Plan lifetime is nested inside the viewport authority/layout pin lifetime.
class DocumentShadowPlan final {
 friend class DocumentCaptureOperation;
 friend class aether::layout::DocumentTextTestAccess;
 struct Layer {
  const aether_layout_box_s* box{};std::size_t declaration{};
  aether::layout::BoxStyleSnapshot style;
  DocumentPaintContext context;
  aether_rectf rect{};aether_corner_radii radii{};aether_shadow shadow{};
 };
 struct Audit {NativeShadowEntry entry;aether_paint_command_t command;};
 // Budget and authority are borrowed for this same live operation only.
 aether::layout::DocumentTextBudget* budget_{};
 std::vector<DocumentClipEvidence> clip_chains_;
 const DocumentViewportAuthority* authority_{};
 aether_layout_result_s* layout_{};aether_style_map_s* map_{};
 std::vector<Layer> layers_;
 DocumentShadowPlan(aether_layout_result_s*,aether_style_map_s*,const DocumentViewportAuthority&,
                    aether::layout::DocumentTextBudget&,const std::function<void()>&);
public:
 bool bound_to(const DocumentViewportAuthority& a)const noexcept{return authority_==&a;}
 DocumentShadowPlan(const DocumentShadowPlan&)=delete;
 bool accepts(aether_layout_result_s*,aether_style_map_s*)const noexcept;
 bool accepts_checked(aether_layout_result_s*,aether_style_map_s*)const;
 std::size_t size()const noexcept{return layers_.size();}
 void verify(const NativePaintList&,const std::function<void()>&)const;
};
}
