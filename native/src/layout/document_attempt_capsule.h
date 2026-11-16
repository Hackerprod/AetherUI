#pragma once
#include "document_attempt_topology.h"
#include <atomic>
namespace aether::layout {
// Inactive cleanup lifetime boundary. No backend, callback, transaction, budget,
// style, node, box, result, pin, engine or weak owner references. Destruction may
// occur on any thread; owned topology contains only primitives and int tokens.
class DocumentAttemptCapsule final {
 friend class DocumentTextAccess;
 friend class DocumentAttemptControl;
 friend DocumentTextTransaction::~DocumentTextTransaction()noexcept;
 friend class DocumentTextTestAccess;
 const std::shared_ptr<const int> operation_;
 const std::thread::id thread_;
 std::unique_ptr<DocumentAttemptTopology> topology_;
 std::atomic<bool> revoked_{false},poisoned_{false};
 DocumentAttemptCapsule(std::shared_ptr<const int> operation,std::thread::id thread,
  std::unique_ptr<DocumentAttemptTopology> topology)
  :operation_(std::move(operation)),thread_(thread),topology_(std::move(topology)){}
 static std::shared_ptr<DocumentAttemptCapsule> create(std::shared_ptr<const int> operation,
  std::thread::id thread,DocumentTextBudget& budget){
  if(thread!=std::this_thread::get_id()||!operation)throw std::invalid_argument("DOCUMENT_ATTEMPT_OPERATION");
  budget.bytes(1,sizeof(DocumentAttemptCapsule)+4*sizeof(void*));
  auto topology=DocumentAttemptTopology::create(operation,thread,budget);
  return std::shared_ptr<DocumentAttemptCapsule>(new DocumentAttemptCapsule(std::move(operation),thread,std::move(topology)));
 }
 void valid_owner(const std::shared_ptr<const int>& operation)const{
  if(thread_!=std::this_thread::get_id())throw std::invalid_argument("DOCUMENT_ATTEMPT_THREAD");
  if(operation_!=operation||revoked_.load(std::memory_order_acquire)||poisoned_.load(std::memory_order_acquire))
   throw std::invalid_argument("DOCUMENT_ATTEMPT_REVOKED");
 }
 // Armed-custody foreign disposal calls this before release-decrement.
 void poison()noexcept{
  poisoned_.store(true,std::memory_order_release);
  revoked_.store(true,std::memory_order_release);
 }
 void terminate()noexcept{
  if(thread_!=std::this_thread::get_id()){poison();return;}
  revoked_.store(true,std::memory_order_release);
  topology_->terminate(); // owner-only O(1), no budget dependency
 }
 // Capabilities have one custodian; moving and disposing the SAME object
 // concurrently is unsupported. Different objects may take the atomic foreign
 // disposal path. No capability owns a Transaction or references its budget.
 enum class Kind {scope,ticket};
 class Capability final {
  friend class DocumentAttemptCapsule;
  friend class DocumentTextTestAccess;
  std::shared_ptr<DocumentAttemptCapsule> capsule_;
  DocumentAttemptTopology::Handle handle_;
  Kind kind_=Kind::scope;
  bool armed_=false;
  Capability(std::shared_ptr<DocumentAttemptCapsule> capsule,DocumentAttemptTopology::Handle handle,Kind kind)noexcept
   :capsule_(std::move(capsule)),handle_(std::move(handle)),kind_(kind),armed_(true){}
 public:
  Capability(const Capability&)=delete;
  Capability& operator=(const Capability&)=delete;
  Capability(Capability&& other)noexcept
   :capsule_(std::move(other.capsule_)),handle_(std::move(other.handle_)),kind_(other.kind_),armed_(other.armed_){other.armed_=false;}
  Capability& operator=(Capability&&)=delete;
  ~Capability()noexcept{if(armed_&&capsule_)capsule_->dispose(*this);}
 };
 class Selected final {
  friend class DocumentAttemptCapsule;
  friend class DocumentTextTestAccess;
  const std::shared_ptr<DocumentAttemptCapsule> capsule_;
  const DocumentAttemptTopology::Handle root_;
  const std::shared_ptr<const int> nonce_;
  Selected(std::shared_ptr<DocumentAttemptCapsule> capsule,DocumentAttemptTopology::Handle root,std::shared_ptr<const int> nonce)noexcept
   :capsule_(std::move(capsule)),root_(std::move(root)),nonce_(std::move(nonce)){}
 public:
  Selected(const Selected&)=delete;
 };
 std::atomic<std::uint32_t> armed_{0};
 bool root_issued_=false,issuance_sealed_=false; // owner-thread-only
 using Custody=DocumentAttemptTopology::Custody;
 void control_entry()const{
  valid_owner(operation_); // thread before non-atomic records
  if(issuance_sealed_)throw std::invalid_argument("DOCUMENT_ATTEMPT_SEALED");
 }
 DocumentAttemptTopology::Node& owned(Capability& c,Kind kind){
  if(!c.armed_||c.capsule_.get()!=this||c.kind_!=kind||!topology_->owns(c.handle_))
   throw std::invalid_argument("DOCUMENT_ATTEMPT_CUSTODIAN");
  auto& node=topology_->nodes_[c.handle_.index_];
  if(node.state==DocumentAttemptTopology::State::abandoned||
     node.custody!=(kind==Kind::scope?Custody::open_scope:Custody::closed_ticket))
   throw std::invalid_argument("DOCUMENT_ATTEMPT_CUSTODY");
  return node;
 }
 void release_obligation()noexcept{
  // Bounded atomic RMW. Underflow is impossible for authenticated single custody;
  // if an internal invariant is broken, sticky poison prevents all publication.
  if(armed_.fetch_sub(1,std::memory_order_release)==0)poison();
 }
 void dispose(Capability& c)noexcept{
  if(!c.armed_)return;
  c.armed_=false; // disarm exactly once before any path exits
  if(thread_!=std::this_thread::get_id()){
   poison(); // release stores sequenced BEFORE release decrement
   release_obligation();return; // never inspect topology or budget
  }
  if(!revoked_.load(std::memory_order_acquire)){
   if(!topology_->owns(c.handle_)){poison();}
   else{
    auto& node=topology_->nodes_[c.handle_.index_];
    if(node.state!=DocumentAttemptTopology::State::abandoned&&
       node.custody!=(c.kind_==Kind::scope?Custody::open_scope:Custody::closed_ticket))poison();
    else if(!topology_->abandon(c.handle_))poison();
   }
  }
  release_obligation();
 }
 Capability open_root(const std::shared_ptr<DocumentAttemptCapsule>& self,DocumentTextBudget& budget){
  control_entry();
  try{
   if(self.get()!=this||root_issued_||armed_.load(std::memory_order_acquire)!=0)
    throw std::invalid_argument("DOCUMENT_ATTEMPT_ROOT_CUSTODY");
   budget.bytes(1,sizeof(Capability));
   auto handle=topology_->begin(budget);
   topology_->nodes_[handle.index_].custody=Custody::open_scope;
   armed_.fetch_add(1,std::memory_order_relaxed);root_issued_=true;
   return Capability(self,std::move(handle),Kind::scope);
  }catch(...){terminate();throw;}
 }
 Capability open_child(Capability& parent,DocumentTextBudget& budget){
  control_entry();
  try{
   (void)owned(parent,Kind::scope);
   if(armed_.load(std::memory_order_acquire)>=65536)throw std::length_error("DOCUMENT_ATTEMPT_CUSTODY_LIMIT");
   budget.bytes(1,sizeof(Capability));
   auto handle=topology_->begin(budget,&parent.handle_);
   topology_->nodes_[handle.index_].custody=Custody::open_scope;
   armed_.fetch_add(1,std::memory_order_relaxed);
   return Capability(parent.capsule_,std::move(handle),Kind::scope);
  }catch(...){terminate();throw;}
 }
 Capability close_scope(Capability& scope,DocumentTextBudget& budget){
  control_entry();
  try{
   const auto& node=owned(scope,Kind::scope);
   for(auto child=node.first_child;child!=DocumentAttemptTopology::none;child=topology_->nodes_[child].next_sibling){
    budget.normalized(1);const auto& n=topology_->nodes_[child];
    if(n.state!=DocumentAttemptTopology::State::abandoned&&n.custody!=Custody::adopted_parent)
     throw std::invalid_argument("DOCUMENT_ATTEMPT_UNADOPTED_CHILD");
   }
   topology_->close(budget,scope.handle_);
   topology_->nodes_[scope.handle_.index_].custody=Custody::closed_ticket;
   scope.kind_=Kind::ticket;
   return Capability(std::move(scope)); // same obligation, no alloc/callback
  }catch(...){terminate();throw;}
 }
 void adopt(Capability& parent,Capability& ticket,DocumentTextBudget& budget){
  control_entry();
  try{
   (void)owned(parent,Kind::scope);auto& node=owned(ticket,Kind::ticket);
   budget.normalized(1);
   if(node.parent!=parent.handle_.index_)throw std::invalid_argument("DOCUMENT_ATTEMPT_ADOPT_PARENT");
   node.custody=Custody::adopted_parent;
   ticket.armed_=false;release_obligation(); // preallocated commit, noexcept
   if(poisoned_.load(std::memory_order_acquire))throw std::invalid_argument("DOCUMENT_ATTEMPT_REVOKED");
  }catch(...){terminate();throw;}
 }
 void discard(Capability& c){
  // Explicit disposal rejects wrong-thread; destructor has separate atomic path.
  valid_owner(operation_);
  if(!c.armed_||c.capsule_.get()!=this)throw std::invalid_argument("DOCUMENT_ATTEMPT_CUSTODIAN");
  dispose(c);
 }
 std::shared_ptr<const Selected> select(Capability& root,DocumentTextBudget& budget){
  control_entry();
  try{
   const auto& node=owned(root,Kind::ticket);
   if(node.parent!=DocumentAttemptTopology::none||armed_.load(std::memory_order_acquire)!=1)
    throw std::invalid_argument("DOCUMENT_ATTEMPT_PENDING_CUSTODIANS");
   budget.bytes(1,sizeof(Selected)+4*sizeof(void*));
   budget.bytes(1,sizeof(int)+4*sizeof(void*));
   auto nonce=std::make_shared<const int>(0);
   auto staged=std::shared_ptr<const Selected>(new Selected(root.capsule_,root.handle_,std::move(nonce)));
   // All fallible allocation/work precedes root consumption. No new scope can
   // be issued after this point, even if selection validation fails.
   issuance_sealed_=true;
   topology_->select_root(budget,root.handle_);
   topology_->nodes_[root.handle_.index_].custody=Custody::adopted_parent;
   root.armed_=false;release_obligation();
   // Acquire the release sequence of all custody removals, including a foreign
   // poison-before-decrement. Root is consumed; zero means no armed disposal can
   // occur later under single-custodian ownership. Recheck poison AFTER zero.
   if(armed_.load(std::memory_order_acquire)!=0||poisoned_.load(std::memory_order_acquire)||
      revoked_.load(std::memory_order_acquire))throw std::invalid_argument("DOCUMENT_ATTEMPT_REVOKED");
   return staged; // noexcept pointer move; no callback/allocation after audit
  }catch(...){terminate();throw;}
 }
 void validate_selected(const std::shared_ptr<const Selected>& selected)const{
  valid_owner(operation_);
  if(!issuance_sealed_||armed_.load(std::memory_order_acquire)!=0||!selected||selected->capsule_.get()!=this||
     !topology_->owns(selected->root_)||topology_->selected_!=selected->root_.index_)
   throw std::invalid_argument("DOCUMENT_ATTEMPT_SELECTION");
 }
 std::uint64_t scope_id(Capability& scope){
  control_entry();return owned(scope,Kind::scope).id;
 }
 std::shared_ptr<const int> selection_identity(const std::shared_ptr<const Selected>& selected)const{
  validate_selected(selected);if(!selected->nonce_)throw std::invalid_argument("DOCUMENT_ATTEMPT_SELECTION_NONCE");
  return selected->nonce_;
 }
 bool selected_attempt(const std::shared_ptr<const Selected>& selected,std::uint64_t id,DocumentTextBudget& budget)const{
  validate_selected(selected);
  // Node IDs are strictly increasing even across failed insertion. Binary search
  // does not assume dense IDs; the successful root selection already proved all
  // non-abandoned nodes closed and under this exact root.
  std::size_t first=0,last=topology_->nodes_.size();
  while(first<last){budget.normalized(1);auto mid=first+(last-first)/2;
   if(topology_->nodes_[mid].id<id)first=mid+1;else last=mid;
  }
  if(first>=topology_->nodes_.size()||topology_->nodes_[first].id!=id)
   throw std::invalid_argument("DOCUMENT_ATTEMPT_UNKNOWN_ID");
  const auto& node=topology_->nodes_[first];
  return node.state==DocumentAttemptTopology::State::closed&&node.custody==Custody::adopted_parent;
 }
 DocumentAttemptCapsule(const DocumentAttemptCapsule&)=delete;
};
}
