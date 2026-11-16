#pragma once
#include "document_text_context.h"
#include <limits>

namespace aether::layout {
// Inactive bookkeeping only. No source/final/paint authority and no live box
// pointers. A future reviewed Access issuer must supply the genuine transaction
// operation/thread/budget and enumerate permitted control-flow callsites.
class DocumentAttemptTopology final {
 friend class DocumentTextAccess;
 friend class DocumentAttemptCapsule;
 friend class DocumentTextTestAccess;
 static constexpr std::size_t none=std::numeric_limits<std::size_t>::max();
 enum class State {open,closed,abandoned};
 enum class Custody {none,open_scope,closed_ticket,adopted_parent,abandoned};
 struct Node {
  std::uint64_t id{};
  std::size_t parent=none,first_child=none,last_child=none,next_sibling=none;
  std::uint16_t depth=1;
  State state=State::open;
  Custody custody=Custody::none;
 };
 class Handle final {
  friend class DocumentAttemptTopology;
  friend class DocumentAttemptCapsule;
  friend class DocumentTextTestAccess;
  std::shared_ptr<const int> identity_;
  std::size_t index_=none;
  std::uint64_t id_{};
  Handle(std::shared_ptr<const int> identity,std::size_t index,std::uint64_t id)
   :identity_(std::move(identity)),index_(index),id_(id){}
 public:
  Handle()=default;
 };
 const std::shared_ptr<const int> operation_,identity_;
 const std::thread::id thread_;
 std::vector<Node> nodes_;
 std::uint64_t next_id_=1;
 std::size_t edges_=0,selected_=none;
 bool failed_=false,terminal_=false;
 DocumentAttemptTopology(std::shared_ptr<const int> operation,std::shared_ptr<const int> identity,
  std::thread::id thread)
  :operation_(std::move(operation)),identity_(std::move(identity)),thread_(thread){}
 static std::unique_ptr<DocumentAttemptTopology> create(std::shared_ptr<const int> operation,
  std::thread::id thread,DocumentTextBudget& budget){
  if(thread!=std::this_thread::get_id()||!operation)throw std::invalid_argument("DOCUMENT_ATTEMPT_OPERATION");
  budget.bytes(1,sizeof(DocumentAttemptTopology)+sizeof(int)+4*sizeof(void*));
  auto identity=std::make_shared<const int>(0);
  return std::unique_ptr<DocumentAttemptTopology>(new DocumentAttemptTopology(std::move(operation),std::move(identity),thread));
 }
 void entry()const{
  if(thread_!=std::this_thread::get_id())throw std::invalid_argument("DOCUMENT_ATTEMPT_THREAD");
  if(failed_||terminal_||selected_!=none)throw std::invalid_argument("DOCUMENT_ATTEMPT_STATE");
 }
 bool owns(const Handle& h)const noexcept{
  return h.identity_==identity_&&h.index_<nodes_.size()&&nodes_[h.index_].id==h.id_;
 }
 // Parent is created earlier and must be open: cycles/reparenting are impossible.
 // IDs consumed by failed allocation are not reused, and no quota is refunded.
 Handle begin(DocumentTextBudget& budget,const Handle* parent=nullptr){
  entry();
  try{budget.normalized(1);
  std::size_t p=none;std::uint16_t depth=1;
  if(parent){
   if(!owns(*parent)||nodes_[parent->index_].state!=State::open)throw std::invalid_argument("DOCUMENT_ATTEMPT_PARENT");
   p=parent->index_;
   if(nodes_[p].depth>=256)throw std::length_error("DOCUMENT_ATTEMPT_DEPTH");
   depth=static_cast<std::uint16_t>(nodes_[p].depth+1);
  }
  if(nodes_.size()>=65536||(p!=none&&edges_>=65535)||next_id_==std::numeric_limits<std::uint64_t>::max())
   throw std::length_error("DOCUMENT_ATTEMPT_LIMIT");
  budget.retained();
  // Credit for one stackless subtree revocation: enter/exit plus parent/sibling
  // edges. It is charged before construction, never charged during cleanup.
  budget.normalized(8);
  const auto id=next_id_++;
  if(nodes_.size()==nodes_.capacity()){
   const auto capacity=nodes_.capacity()?std::min<std::size_t>(65536,nodes_.capacity()*2):1;
   budget.bytes(capacity,sizeof(Node)); // whole replacement storage, no refunds
   nodes_.reserve(capacity);
  }
  const auto index=nodes_.size();Node node;node.id=id;node.parent=p;node.depth=depth;
  nodes_.push_back(node); // capacity reserved; primitive Node moves cannot throw
  if(p!=none){
   auto& owner=nodes_[p];
   if(owner.last_child!=none)nodes_[owner.last_child].next_sibling=index;
   else owner.first_child=index;
   owner.last_child=index;++edges_;
  }
  return Handle(identity_,index,id);
  }catch(...){failed_=true;throw;}
 }
 void close(DocumentTextBudget& budget,const Handle& h){
  entry();
  try{budget.normalized(1);
  if(!owns(h)||nodes_[h.index_].state!=State::open)throw std::invalid_argument("DOCUMENT_ATTEMPT_CLOSE");
  for(auto child=nodes_[h.index_].first_child;child!=none;child=nodes_[child].next_sibling){
   budget.normalized(1);
   if(nodes_[child].state==State::open)throw std::invalid_argument("DOCUMENT_ATTEMPT_OPEN_CHILD");
  }
  nodes_[h.index_].state=State::closed;
  }catch(...){failed_=true;throw;}
 }
 // No allocation/callback/budget throw, no pointer to geometry. Wrong-thread
 // does not mutate non-atomic state. Caller must treat false as operation failure
 // before deleting its tree; a later integration supplies that failure latch.
 bool abandon(const Handle& h)noexcept{
  if(thread_!=std::this_thread::get_id())return false;
  if(!owns(h)){failed_=true;return false;}
  if(nodes_[h.index_].state==State::abandoned)return true;
  if(selected_!=none){selected_=none;failed_=true;}
  const auto root=h.index_;auto current=root;
  for(;;){
   auto& node=nodes_[current];
   const bool fresh=node.state!=State::abandoned;
   node.state=State::abandoned;node.custody=Custody::abandoned;
   if(fresh&&node.first_child!=none){current=node.first_child;continue;}
   for(;;){
    if(current==root)return true;
    const auto sibling=nodes_[current].next_sibling;
    if(sibling!=none){current=sibling;break;}
    current=nodes_[current].parent;
   }
  }
 }
 // Selection is private and control-flow-only. Surviving geometry pointers do
 // not authorize selection. All other root attempts must already be abandoned.
 void select_root(DocumentTextBudget& budget,const Handle& root){
  entry();
  try{budget.normalized(1);
  if(!owns(root)||nodes_[root.index_].parent!=none||nodes_[root.index_].state!=State::closed)
   throw std::invalid_argument("DOCUMENT_ATTEMPT_ROOT");
  // Parent index precedes child. One precharged scratch bit per node computes
  // membership without a walk per node or dereferencing any geometry key.
  budget.bytes(nodes_.size(),sizeof(unsigned char));
  std::vector<unsigned char> selected(nodes_.size(),0);
  for(std::size_t i=0;i<nodes_.size();++i){
   budget.normalized(1);const auto& node=nodes_[i];
   if(node.state==State::abandoned)continue;
   if(node.state!=State::closed)throw std::invalid_argument("DOCUMENT_ATTEMPT_UNRESOLVED");
   if(i==root.index_||(node.parent!=none&&selected[node.parent]))selected[i]=1;
   else throw std::invalid_argument("DOCUMENT_ATTEMPT_UNSELECTED");
  }
  selected_=root.index_; // only publication, no callbacks or allocation after
  }catch(...){failed_=true;throw;}
 }
 void terminate()noexcept{
  if(thread_!=std::this_thread::get_id())return;
  // Whole-operation terminal flag revokes every handle logically in O(1).
  selected_=none;terminal_=true;
 }
 DocumentAttemptTopology(const DocumentAttemptTopology&)=delete;
};
}
