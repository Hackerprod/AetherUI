#pragma once
#include "layout_types.h"
#include <thread>
namespace aether::rendering { class SelectedCaptureOperation; }
namespace aether::layout {
class SelectedCaptureTestAccess;
// Private capability. Creation requires live handles on the operation thread,
// after selected-scope and ancestor CSS/dependency admission. The wrapper owns
// doc/map for the entire operation. accepts() also requires its map handle live;
// tokens do not make a freed map handle safe. No concurrent DOM mutation.
// It neither changes nor reparents the DOM.
class SelectedLayoutAuthority final {
 friend class aether::rendering::SelectedCaptureOperation;
 friend class SelectedCaptureTestAccess;
 std::thread::id thread_=std::this_thread::get_id();
 aether_doc_s* doc_{};
 aether_style_map_s* map_{};
 aether::style::MapVersionRef version_;
 std::shared_ptr<node_identity_s> document_;
 CallbackEpochGuard epochs_;
 std::uint64_t nonstate_{};
 aether::style::FrozenNodeRef target_;
 std::unordered_set<aether_node_s*> scope_;
 float width_{},height_{};
 SelectedLayoutAuthority(aether_doc_s* doc,aether_style_map_s* map,aether_node_s* target,
                         float width,float height)
  :doc_(doc),map_(map),version_(aether::style::pin_style_map(map)),
   document_(doc?doc->identity:nullptr),epochs_(document_),
   nonstate_(document_?document_->non_state_epoch:0),width_(width),height_(height) {
  if(!doc||!map||!target||!version_||!version_->contextual()||
     version_->document_lifetime()!=document_||!aether::style::inputs_current(version_.get())||
     !std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||width>2048||height>2048)
   throw std::invalid_argument("SELECTED_LAYOUT_AUTHORITY");
  target_=version_->backing()->find_owner(target);
  if(!target_||!target_->node_lifetime()||!target_->node_lifetime()->alive||
     !target_->node_lifetime()->attached||target_->node_lifetime()->owner!=target)
   throw std::invalid_argument("SELECTED_LAYOUT_TARGET");
  std::vector<std::pair<aether_node_s*,unsigned>> todo{{target,0}};
  while(!todo.empty()) {
   auto [node,depth]=todo.back();todo.pop_back();
   if(!node||depth>64||scope_.size()>=128||!scope_.insert(node).second)
    throw std::length_error("SELECTED_LAYOUT_SCOPE_LIMIT");
   if(node->children.size()>128-scope_.size()-todo.size())
    throw std::length_error("SELECTED_LAYOUT_SCOPE_LIMIT");
   for(auto* child:node->children)todo.emplace_back(child,depth+1);
  }
 }
public:
 SelectedLayoutAuthority(const SelectedLayoutAuthority&)=delete;
 SelectedLayoutAuthority& operator=(const SelectedLayoutAuthority&)=delete;
 bool accepts(aether_doc_s* doc,aether_style_map_s* map) const noexcept {
  if(std::this_thread::get_id()!=thread_)return false;
  if(!document_||!document_->alive||document_->owner!=doc_||!epochs_.valid()||
     document_->non_state_epoch!=nonstate_||doc!=doc_||map!=map_||
     !target_||!target_->node_lifetime()->alive||!target_->node_lifetime()->attached||
     target_->node_lifetime()->owner!=target_->node())return false;
  return aether::style::pin_style_map(map)==version_&&aether::style::inputs_current(version_.get());
 }
 aether_node_s* target() const noexcept{return target_->node();}
 const std::unordered_set<aether_node_s*>& scope() const noexcept{return scope_;}
 float viewport_width() const noexcept{return width_;}
 float viewport_height() const noexcept{return height_;}
};
// Retains normal layout result ownership; caller keeps original DOM/map alive.
// No viewport-overflow propagation, body stretching or canvas profile activation.
aether_status_t layout_selected_capture(aether_layout_engine_t,aether_doc_t,aether_style_map_t,
 const SelectedLayoutAuthority&,aether_layout_result_t*) noexcept;
}
