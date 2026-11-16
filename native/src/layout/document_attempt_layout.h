#pragma once
#include "layout_types.h"
#include "document_attempt_control.h"
#include <map>
#include <optional>
namespace aether::layout {
// Operation-owned private control state, not a resource retained by the final
// product. Keys are never dereferenced during destruction or discarded cleanup.
class DocumentAttemptLayout final {
 friend class DocumentTextAccess;
 using Capability=DocumentAttemptControl::Capability;
 using Selected=DocumentAttemptControl::Selected;
 std::optional<Capability> root_;
 Capability* active_=nullptr; // synchronous stack frame borrow; never published
 std::map<const aether_layout_box_s*,Capability> tickets_;
 std::shared_ptr<const Selected> selected_;
 explicit DocumentAttemptLayout(aether_layout_engine_s* engine)
  :root_(DocumentAttemptControl::initialize_root(engine)),active_(&*root_){}
 void adopt_descendants(aether_layout_engine_s* engine,Capability& parent,
                        const aether_layout_box_s* box){
  // The live construction path invokes this without callbacks and before any
  // cleanup. A found child ticket owns its entire subtree; never traverse it a
  // second time. Untracked intermediate text/anonymous boxes have no custody.
  auto context=DocumentTextAccess::attempt_control_entry(engine);
  auto& budget=*context.second;
  auto* layout=DocumentTextAccess::construction_layout(engine);
  if(!layout||layout->live_box_count>65536||!layout->allocated_boxes.allocation_revision_valid())
   throw std::invalid_argument("DOCUMENT_ATTEMPT_REGISTRY");
  budget.bytes(layout->live_box_count,sizeof(const aether_layout_box_s*)+4*sizeof(void*));
  std::unordered_set<const aether_layout_box_s*> members;
  members.reserve(layout->live_box_count);
  for(auto* allocated:layout->allocated_boxes){budget.normalized(1);members.insert(allocated);}
  if(!members.contains(box))throw std::invalid_argument("DOCUMENT_ATTEMPT_FOREIGN_BOX");
  if(box->children.size()>65536)throw std::length_error("DOCUMENT_ATTEMPT_PENDING_CAP");
  budget.bytes(box->children.size(),sizeof(const aether_layout_box_s*));
  std::vector<const aether_layout_box_s*> pending;
  pending.reserve(box->children.size());
  for(auto* child:box->children)pending.push_back(child);
  std::size_t visits=0;
  while(!pending.empty()){
   if(++visits>65536)throw std::length_error("DOCUMENT_ATTEMPT_GEOMETRY_CAP");
   auto* child=pending.back();pending.pop_back();budget.normalized(1);
   if(!child||!members.contains(child))throw std::invalid_argument("DOCUMENT_ATTEMPT_FOREIGN_CHILD");
   std::uint64_t lookup=1;for(auto n=tickets_.size();n;n>>=1)lookup+=2;
   budget.normalized(lookup);
   auto found=tickets_.find(child);
   if(found!=tickets_.end()){
    DocumentAttemptControl::adopt(engine,parent,found->second);
    tickets_.erase(found);
   }else{
    if(child->children.size()>65536-pending.size())throw std::length_error("DOCUMENT_ATTEMPT_PENDING_CAP");
    const auto required=pending.size()+child->children.size();
    if(required>pending.capacity()){const auto capacity=std::max(required,std::min(std::size_t{65536},std::max(std::size_t{1},pending.capacity()*2)));
     budget.bytes(capacity,sizeof(const aether_layout_box_s*));pending.reserve(capacity);}
    for(auto* nested:child->children)pending.push_back(nested);
   }
  }
 }
public:
 DocumentAttemptLayout(const DocumentAttemptLayout&)=delete;
 ~DocumentAttemptLayout()noexcept=default;
 // The frame is local to a single genuine layout_element invocation. On
 // exception its scope is abandoned before the caller deletes partial geometry.
 class Frame final {
  DocumentAttemptLayout& state_;
  aether_layout_engine_s* engine_;
  Capability* previous_;
  Capability scope_;
  bool closed_=false;
 public:
  Frame(DocumentAttemptLayout& state,aether_layout_engine_s* engine)
   :state_(state),engine_(engine),previous_(state.active_),
    scope_(DocumentAttemptControl::open_child(engine,*state.active_)){
   state_.active_=&scope_;
  }
  Frame(const Frame&)=delete;
  ~Frame()noexcept{state_.active_=previous_;} // scope destructor then abandons
  void close(aether_layout_box_s* box){
   if(closed_||!box||state_.active_!=&scope_)
    throw std::invalid_argument("DOCUMENT_ATTEMPT_FRAME");
   state_.adopt_descendants(engine_,scope_,box);
   auto context=DocumentTextAccess::attempt_control_entry(engine_);
   context.second->bytes(1,sizeof(decltype(state_.tickets_)::value_type)+4*sizeof(void*));
   context.second->normalized(64); // prepaid bounded tree lookup/erase on no-throw discard
   auto ticket=DocumentAttemptControl::close_scope(engine_,scope_);
   if(!state_.tickets_.emplace(box,std::move(ticket)).second)
    throw std::invalid_argument("DOCUMENT_ATTEMPT_DUPLICATE_BOX");
   closed_=true;state_.active_=previous_;
  }
 };
 void discard_before_delete(const aether_layout_box_s* box)noexcept{
  // No callback, allocation, charge or live-box dereference. Capability disposal
  // uses the prepaid topology cleanup and is safe even after terminal revocation.
  auto found=tickets_.find(box);
  if(found!=tickets_.end())tickets_.erase(found);
 }
 void select_root(aether_layout_engine_s* engine,const aether_layout_box_s* box){
  if(selected_||!root_||active_!=&*root_)
   throw std::invalid_argument("DOCUMENT_ATTEMPT_ROOT_STATE");
  if(box){
   auto found=tickets_.find(box);
   if(found==tickets_.end())throw std::invalid_argument("DOCUMENT_ATTEMPT_ROOT_TICKET");
   DocumentAttemptControl::adopt(engine,*root_,found->second);tickets_.erase(found);
  }
  if(!tickets_.empty())throw std::invalid_argument("DOCUMENT_ATTEMPT_ORPHAN_GEOMETRY");
  auto ticket=DocumentAttemptControl::close_scope(engine,*root_);
  selected_=DocumentAttemptControl::select(engine,ticket);
  root_.reset();active_=nullptr;
 }
 std::uint64_t active_id(aether_layout_engine_s* engine){
  if(!active_||selected_)throw std::invalid_argument("DOCUMENT_ATTEMPT_NO_ACTIVE_FRAME");
  return DocumentAttemptControl::active_id(engine,*active_);
 }
 std::shared_ptr<const int> selected_identity(const aether_layout_engine_s* engine)const{
  audit_selection(engine);return DocumentAttemptControl::selected_identity(engine,selected_);
 }
 bool selected_attempt(const aether_layout_engine_s* engine,std::uint64_t id,DocumentTextBudget& budget)const{
  audit_selection(engine);return DocumentAttemptControl::selected_attempt(engine,selected_,id,budget);
 }
 void audit_selection(const aether_layout_engine_s* engine)const{
  if(root_||active_||!tickets_.empty()||!selected_)
   throw std::invalid_argument("DOCUMENT_ATTEMPT_PENDING_GEOMETRY");
  DocumentAttemptControl::audit_selected_for_publication(engine,selected_);
 }
};
}
