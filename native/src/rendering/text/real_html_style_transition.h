#pragma once
#include "../../layout/operation_pin.h"
#include "../../css/style_map_types.h"
#include "real_html_blocks.h"
#include <thread>
namespace aether::rendering {class SelectedCaptureOperation;class DocumentCaptureOperation;}
namespace aether::layout {class RealHtmlTextTestAccess;}
namespace aether::rendering::text::truetype {
// Private, operation-local snapshot. No callback or test mutation hook.
class RealHtmlStyleTransition final {
 friend class aether::rendering::SelectedCaptureOperation;
 friend class aether::rendering::DocumentCaptureOperation;
 friend class aether::layout::RealHtmlTextTestAccess;
 friend HtmlTextResult render_real_html_text(std::string_view,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,Cancellation*) noexcept;
 friend HtmlBlocksResult render_real_html_blocks(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,Cancellation*) noexcept;
 friend HtmlBlocksImagesResult render_real_html_blocks_images(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,const NativeImageCatalog&,Cancellation*) noexcept;
 friend HtmlViewportResult render_real_html_viewport(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,const NativeImageCatalog&,ViewportOffset,Cancellation*) noexcept;
 const std::thread::id thread_=std::this_thread::get_id();
 std::shared_ptr<node_identity_s> identity_;
 std::uint64_t structure_,mutation_,nonstate_;
 explicit RealHtmlStyleTransition(aether_doc_s* doc)
 :identity_(doc->identity),structure_(identity_->structure_epoch),mutation_(identity_->mutation_epoch),nonstate_(identity_->non_state_epoch){}
 bool accepts(aether_doc_s* doc,aether_style_map_s* map) const {
  if(std::this_thread::get_id()!=thread_)return false;
  if(!identity_ || !identity_->alive || identity_->owner!=doc)return false;
  if(!doc || doc->identity!=identity_ || identity_->structure_epoch!=structure_ || identity_->mutation_epoch!=mutation_ || identity_->non_state_epoch!=nonstate_)return false;
  const auto version=aether::style::pin_style_map(map);
  return version && version->contextual() && version->document_lifetime()==identity_ &&
    version->style_tree_epoch()==identity_->style_tree_epoch && aether::style::inputs_current(version.get());
 }
};
}
