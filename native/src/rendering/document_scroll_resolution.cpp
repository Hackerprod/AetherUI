#include "document_scroll_resolution.h"
#include "../dom/dom_types.h"
#include <set>
namespace aether::rendering {
namespace {
[[noreturn]]void stale(){throw DocumentScrollResolutionError::stale;}
}
DocumentScrollRequests DocumentScrollResolution::own(const DocumentScrollRequests& in,aether::layout::DocumentTextBudget& budget){
 budget.bytes(in.entries().size(),sizeof(DocumentScrollRequestInput));budget.normalized(in.entries().size());
 std::vector<DocumentScrollRequestInput> input;input.reserve(in.entries().size());
 for(const auto& e:in.entries())input.push_back({e.id,e.x,e.y});
 return DocumentScrollRequests::prepare(input,budget);
}
DocumentScrollResolution::DocumentScrollResolution(aether_doc_s* doc,aether_style_map_s* map,
 const DocumentScrollRequests& requests,aether::layout::DocumentTextBudget& budget,std::thread::id owner_thread,bool storage_precharged)
 :thread_(owner_thread),budget_(&budget),requests_([&]{
  // Before copying or accessing any caller DOM/map state.
  if(owner_thread!=std::this_thread::get_id()||!doc||!map)stale();
  auto version=aether::style::pin_style_map(map);auto identity=doc->identity;
  if(!identity||!identity->alive||identity->owner!=doc||!version||!version->contextual()||version->document_lifetime()!=identity||!aether::style::inputs_current(version.get())||version->style_tree_epoch()!=identity->style_tree_epoch)stale();
  if(!storage_precharged)budget.bytes(1,sizeof(DocumentScrollResolution));return own(requests,budget);
 }()){
 doc_=doc;map_=map;version_=aether::style::pin_style_map(map);document_=doc->identity;
 structure_=document_->structure_epoch;mutation_=document_->mutation_epoch;style_=document_->style_tree_epoch;nonstate_=document_->non_state_epoch;
 struct Less {aether::layout::DocumentTextBudget* budget;bool operator()(std::string_view a,std::string_view b)const {budget->normalized(1+std::min(a.size(),b.size()));return a<b;}};
 budget.bytes(requests_.entries().size(),sizeof(std::pair<const std::string_view,std::size_t>)+4*sizeof(void*));
 std::map<std::string_view,std::size_t,Less> index{Less{&budget}};
 budget.bytes(requests_.entries().size(),sizeof(Target));std::vector<Target> matches(requests_.entries().size());
 for(std::size_t i=0;i<requests_.entries().size();++i){budget.normalized(1);const auto&r=requests_.entries()[i];index.emplace(r.id,i);matches[i]={i,{},r.x,r.y};}
 struct Pending {const aether_node_s* node;const aether_node_s* parent;std::size_t depth;};
 budget.bytes(1,sizeof(Pending));std::vector<Pending> todo{{doc,nullptr,0}};std::set<const aether_node_s*> seen;std::size_t edges=0;
 while(!todo.empty()){
  const auto item=todo.back();todo.pop_back();if(nodes_.size()>=16384||item.depth>64)throw std::length_error("SCROLL_DOM_LIMIT");
  budget.normalized(1);budget.bytes(1,sizeof(const aether_node_s*)+4*sizeof(void*));
  if(!item.node||!seen.insert(item.node).second)stale();
  // Initially valid DOM is a capture precondition, no callback has run.
  const auto& n=*item.node;const auto token=n.identity;
  if(!token||!token->alive||!token->attached||token->owner!=item.node||n.parent!=item.parent)stale();
  if(n.children.size()>16383-edges||n.children.size()>16384-nodes_.size()-1-todo.size())throw std::length_error("SCROLL_DOM_LIMIT");
  edges+=n.children.size();budget.normalized(n.children.size());
  const auto attr=n.attributes.find("id");const bool has=attr!=n.attributes.end();const auto id=has?std::string_view(attr->second):std::string_view{};
  budget.normalized(id.size()+1);budget.bytes(1,sizeof(Node));budget.bytes(n.children.size(),sizeof(aether_node_s*)+sizeof(Pending));budget.bytes(id.size()+1);
  Node snapshot;snapshot.key=item.node;snapshot.parent=item.parent;snapshot.identity=token;snapshot.type=n.type;snapshot.has_id=has;snapshot.id=id;snapshot.children=n.children;
  if(n.type==AETHER_NODE_ELEMENT&&has){auto requested=index.find(id);if(requested!=index.end()){auto&match=matches[requested->second];if(match.identity)throw DocumentScrollResolutionError::ambiguous;match.identity=token;}}
  validation_work_+=1+n.children.size()+id.size();
  nodes_.push_back(std::move(snapshot));
  for(auto it=n.children.rbegin();it!=n.children.rend();++it)todo.push_back({*it,item.node,item.depth+1});
 }
 for(const auto&match:matches){budget.normalized(1);if(!match.identity)throw DocumentScrollResolutionError::missing;budget.bytes(1,sizeof(decltype(targets_)::value_type)+4*sizeof(void*));if(!targets_.emplace(static_cast<const aether_node_s*>(match.identity->owner),match).second)stale();}
 if(!accepts())stale();
}
std::unique_ptr<DocumentScrollResolution> DocumentScrollResolution::capture(aether_doc_s* doc,aether_style_map_s* map,const DocumentScrollRequests& requests,aether::layout::DocumentTextBudget& budget,std::thread::id thread){
 if(thread!=std::this_thread::get_id())stale();
 budget.bytes(1,sizeof(DocumentScrollResolution));
 return std::unique_ptr<DocumentScrollResolution>(new DocumentScrollResolution(doc,map,requests,budget,thread,true));
}
bool DocumentScrollResolution::stamp()const noexcept{
 if(std::this_thread::get_id()!=thread_||!document_||!document_->alive||document_->owner!=doc_||!map_)return false;
 if(document_->structure_epoch!=structure_||document_->mutation_epoch!=mutation_||document_->style_tree_epoch!=style_||document_->non_state_epoch!=nonstate_)return false;
 return aether::style::pin_style_map(map_)==version_&&aether::style::inputs_current(version_.get());
}
bool DocumentScrollResolution::accepts()const{
 if(!stamp())return false;
 budget_->normalized(validation_work_);
 if(nodes_.empty()||nodes_.front().key!=doc_)return false;
 for(const auto& node:nodes_){
  if(!node.identity||!node.identity->alive||!node.identity->attached||node.identity->owner!=node.key)return false;
  const auto& live=*node.key;
  if(live.identity!=node.identity||live.parent!=node.parent||live.type!=node.type||live.children!=node.children)return false;
  auto id=live.attributes.find("id");if((id!=live.attributes.end())!=node.has_id)return false;
  if(node.has_id&&id->second!=node.id)return false;
 }
 return true;
}
}
