#include "document_scope_plan.h"
#include "display_list_types.h"
#include "../css/border_radii.h"
#include <algorithm>
#include <limits>
#include <set>
namespace aether::rendering {
namespace {
bool radii_same(aether_corner_radii a,aether_corner_radii b){return a.top_left_x==b.top_left_x&&a.top_left_y==b.top_left_y&&a.top_right_x==b.top_right_x&&a.top_right_y==b.top_right_y&&a.bottom_right_x==b.bottom_right_x&&a.bottom_right_y==b.bottom_right_y&&a.bottom_left_x==b.bottom_left_x&&a.bottom_left_y==b.bottom_left_y;}

using Role=NativeDocumentScopeRole;
constexpr auto absent=std::numeric_limits<std::size_t>::max();
[[noreturn]]void invalid(){throw std::invalid_argument("DOCUMENT_SCOPE_PROOF");}
bool rect(aether_rectf a,aether_rectf b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
bool chrome(const aether_scrollbar_geometry_t&a,const aether_scrollbar_geometry_t&b){return a.horizontal_visible==b.horizontal_visible&&a.vertical_visible==b.vertical_visible&&rect(a.horizontal_track,b.horizontal_track)&&rect(a.horizontal_thumb,b.horizontal_thumb)&&rect(a.vertical_track,b.vertical_track)&&rect(a.vertical_thumb,b.vertical_thumb)&&rect(a.corner,b.corner);}
bool state(const NativeScrollState&a,const NativeScrollState&b){return a.id==b.id&&a.x==b.x&&a.y==b.y&&a.range_x==b.range_x&&a.range_y==b.range_y&&chrome(a.chrome,b.chrome);}
bool scope_tag(aether_paint_command_tag_t tag){return tag==AETHER_PAINT_PUSH_CLIP||tag==AETHER_PAINT_POP_CLIP||tag==AETHER_PAINT_PUSH_SCROLL_OFFSET||tag==AETHER_PAINT_POP_SCROLL_OFFSET||tag==AETHER_PAINT_DRAW_SCROLLBAR_CHROME;}
void translation(double value){if(!std::isfinite(value)||value<0||value>65536||std::floor(value)!=value)invalid();}
}
DocumentScopePlan::DocumentScopePlan(aether_layout_result_s*l,aether_style_map_s*m,
 const DocumentViewportAuthority&a,aether::layout::DocumentTextBudget&budget,const std::function<void()>&check)
 :authority_(&a),layout_(l),map_(m),budget_(&budget){
 if(!a.registry_candidate()||!a.accepts_checked(l,m)||a.registry_.empty()||a.registry_.size()>256)invalid();
 budget.bytes(1,sizeof(DocumentScopePlan));operation_=a.registry_operation_;if(!operation_)invalid();
 budget.bytes(a.clip_chains_.size(),sizeof(DocumentClipEvidence));budget.normalized(a.clip_chains_.size());chains_=a.clip_chains_;
 // Bounded callback-free ownership copy. No live box/DOM access occurs here.
 for(const auto& context:a.paint_contexts_){
  budget.normalized(1);budget.bytes(1,sizeof(decltype(contexts_)::value_type)+4*sizeof(void*));
  if(context.operation!=operation_||!contexts_.emplace(context.box,Context{context,absent,absent}).second)invalid();
 }
 const auto checkpoint=[&]{if(check)check();if(!a.stamp_current(l,m))invalid();};
 std::set<std::int64_t> ids;
 for(const auto& record:a.registry_){
  checkpoint();budget.normalized(1);
  budget.bytes(1,sizeof(Rule)+sizeof(decltype(index_)::value_type)+sizeof(std::int64_t)+8*sizeof(void*));
  if(record.operation!=operation_||record.state.id<=0||!record.identity||!ids.insert(record.state.id).second)invalid();
  const auto context=contexts_.find(record.box);if(context==contexts_.end())invalid();
  // A hidden owner is a real clip/scroll-range scope at implicit zero, not
  // a visible-overflow alias. Existing emitter may push its zero scroll state
  // when it has native overflow; it must never emit scrollbar chrome.
  if(record.overflow_x==AETHER_OVERFLOW_HIDDEN||record.overflow_y==AETHER_OVERFLOW_HIDDEN){
   if(record.root||record.overflow_x!=AETHER_OVERFLOW_HIDDEN||record.overflow_y!=AETHER_OVERFLOW_HIDDEN||
      record.state.x!=0||record.state.y!=0||record.chrome_expected||
      record.state.chrome.horizontal_visible||record.state.chrome.vertical_visible)invalid();
  }
  Rule rule{record,context->second.paint,{}};
  if(record.root){
   if(!rules_.empty())invalid();rule.expected[0]=rule.expected[1]=1;
   if(rule.context.painted){rule.expected[2]=rule.expected[3]=rule.expected[6]=rule.expected[7]=rule.expected[8]=1;}
  }else if(rule.context.painted){
   rule.expected[4]=rule.expected[5]=1;
   if(record.push_expected)rule.expected[6]=rule.expected[7]=1;
   if(record.chrome_expected)rule.expected[8]=1;
  }
  for(auto count:rule.expected){if(count>65536-capacity_)invalid();capacity_+=count;}
  if(rule.expected[6]||rule.expected[8]){
   // One owned state plus one future NativePaintList state copy.
   budget.bytes(2,sizeof(NativeScrollState));states_.push_back(record.state);
  }
  if(!index_.emplace(record.box,rules_.size()).second)invalid();rules_.push_back(std::move(rule));
 }
 if(rules_.empty()||!rules_.front().record.root)invalid();
 // One frozen preorder pass derives persistent scroll ancestry. A unique rule
 // index is a chain node whose parent is already authenticated, not an offset.
 // Inactive auto/forced-chrome-only containers do not enter the scroll chain.
 for(const auto& box:a.boxes_){
  budget.normalized(1);const auto it=contexts_.find(box.box);if(it==contexts_.end())invalid();
  auto& context=it->second;std::size_t inherited=absent;
  if(box.parent){const auto parent=contexts_.find(box.parent);if(parent==contexts_.end())invalid();inherited=parent->second.child_scroll;}
  const auto rule=index_.find(box.box);
  context.own_scroll=inherited;context.child_scroll=inherited;
  if(rule!=index_.end()&&rules_[rule->second].expected[6]){
   context.child_scroll=rule->second;
   if(rules_[rule->second].record.root)context.own_scroll=rule->second;
  }
 }

 // Plan owns the authorized count. Future emitter reserves before appending;
 // callers cannot substitute an untrusted capacity/count.
 budget.bytes(capacity_,sizeof(NativeDocumentScopeEntry)+sizeof(aether_paint_command_t));
 std::size_t comparisons=0;
 std::sort(states_.begin(),states_.end(),[&](const auto&a,const auto&b){if((comparisons++&255u)==0)checkpoint();return a.id<b.id;});
 checkpoint();if(!accepts_checked(l,m))invalid();
}
bool DocumentScopePlan::accepts(aether_layout_result_s*l,aether_style_map_s*m)const noexcept{
 try{return accepts_checked(l,m);}catch(...){return false;}
}
bool DocumentScopePlan::accepts_checked(aether_layout_result_s*l,aether_style_map_s*m)const{
 return authority_&&l==layout_&&m==map_&&authority_->registry_candidate()&&authority_->accepts_checked(l,m);
}
void DocumentScopePlan::verify(const NativePaintList&list)const{
 if(!authority_||!authority_->stamp_current(layout_,map_))invalid();
 const auto* raw=NativeListAccess::list(list);
 if(!raw||raw->commands.size()>65536||list.document_scopes.size()!=capacity_||list.scroll.size()!=states_.size()||list.shadows.size()>65536)invalid();
 budget_->normalized(raw->commands.size()+list.document_scopes.size()+list.shadows.size()+states_.size());
 for(std::size_t i=0;i<states_.size();++i)if(!state(list.scroll[i],states_[i]))invalid();
 struct Frame {Role role;const aether_layout_box_s* box;std::size_t chain,scroll;double x,y;};
 budget_->bytes(rules_.size(),sizeof(std::array<unsigned,9>));budget_->bytes(256,sizeof(Frame));
 std::vector<std::array<unsigned,9>> seen(rules_.size());std::vector<Frame> stack;stack.reserve(256);
 std::size_t active_scroll=absent,chain=absent,entry_index=0,shadow_index=0;double x=0,y=0;
 const auto push=[&](Role role,const aether_layout_box_s*box){if(stack.size()>=256)invalid();stack.push_back({role,box,chain,active_scroll,x,y});};
 for(std::size_t i=0;i<raw->commands.size();++i){
  const auto&command=raw->commands[i];const auto tag=command.tag;
  if(tag==AETHER_PAINT_PUSH_TRANSFORM||tag==AETHER_PAINT_POP_TRANSFORM)invalid();
  if(scope_tag(tag)){
   if(entry_index>=list.document_scopes.size())invalid();const auto&entry=list.document_scopes[entry_index++];
   if(entry.command_index!=i)invalid();const auto found=index_.find(entry.box_identity);if(found==index_.end())invalid();
   const auto ri=found->second;const auto&rule=rules_[ri];const auto role=entry.role;
   const auto context=contexts_.find(entry.box_identity);if(context==contexts_.end())invalid();
   const auto parent_scroll=rule.record.root?absent:context->second.own_scroll;
   const bool open=tag==AETHER_PAINT_PUSH_CLIP||tag==AETHER_PAINT_PUSH_SCROLL_OFFSET;
   const bool close=tag==AETHER_PAINT_POP_CLIP||tag==AETHER_PAINT_POP_SCROLL_OFFSET;
   unsigned slot=0;std::size_t expected_chain=absent;double expected_x=0,expected_y=0;
   if(role==Role::viewport_outer||role==Role::viewport_content||role==Role::nested_padding){
    if(tag!=AETHER_PAINT_PUSH_CLIP&&tag!=AETHER_PAINT_POP_CLIP)invalid();
    if(active_scroll!=parent_scroll)invalid();
    if(role==Role::viewport_outer){if(!rule.record.root)invalid();slot=open?0:1;if(rule.context.chain>=chains_.size())invalid();expected_chain=chains_[rule.context.chain].parent;}
    else if(role==Role::viewport_content){if(!rule.record.root)invalid();slot=open?2:3;expected_chain=rule.context.chain;}
    else{if(rule.record.root)invalid();slot=open?4:5;expected_chain=rule.context.child_chain;expected_x=rule.context.x;expected_y=rule.context.y;}
    if(expected_chain>=chains_.size())invalid();const auto&clip=chains_[expected_chain];
    const auto kind=role==Role::viewport_outer?DocumentClipKind::viewport_outer:role==Role::viewport_content?DocumentClipKind::viewport_content:DocumentClipKind::nested_padding;
    if(clip.owner!=entry.box_identity||clip.kind!=kind||x!=expected_x||y!=expected_y)invalid();
    if(open){
     if(chain!=clip.parent)invalid();if(role==Role::viewport_outer&&(i!=0||!stack.empty()))invalid();
     const auto expected=role==Role::nested_padding?rule.record.scrollport:authority_->viewport();
     if(!rect(command.data.push_clip.rect,expected)||!radii_same(command.data.push_clip.radii,clip.radii))invalid();
     const aether_rectf screen{float(double(expected.x)-x),float(double(expected.y)-y),expected.width,expected.height};
     if(!rect(screen,clip.screen))invalid();push(role,entry.box_identity);chain=expected_chain;
    }else{
     if(chain!=expected_chain||stack.empty()||stack.back().role!=role||stack.back().box!=entry.box_identity)invalid();
     const auto previous=stack.back();stack.pop_back();chain=previous.chain;active_scroll=previous.scroll;x=previous.x;y=previous.y;
     if(role==Role::viewport_outer&&i+1!=raw->commands.size())invalid();
    }
   }else if(role==Role::scroll){
    if(tag!=AETHER_PAINT_PUSH_SCROLL_OFFSET&&tag!=AETHER_PAINT_POP_SCROLL_OFFSET)invalid();slot=open?6:7;
    if(open){
     const auto expected=rule.record.root?rule.context.chain:rule.context.child_chain;
     const double before_x=rule.record.root?0:rule.context.x,before_y=rule.record.root?0:rule.context.y;
     if(active_scroll!=parent_scroll||chain!=expected||x!=before_x||y!=before_y||command.data.push_scroll_offset.scroll_container_id!=rule.record.state.id)invalid();
     push(role,entry.box_identity);active_scroll=ri;x+=rule.record.state.x;y+=rule.record.state.y;translation(x);translation(y);
    }else{
     if(active_scroll!=ri||!close||stack.empty()||stack.back().role!=role||stack.back().box!=entry.box_identity)invalid();
     const auto previous=stack.back();stack.pop_back();chain=previous.chain;active_scroll=previous.scroll;x=previous.x;y=previous.y;
    }
   }else if(role==Role::chrome){
    if(tag!=AETHER_PAINT_DRAW_SCROLLBAR_CHROME)invalid();slot=8;
    if(rule.expected[6]&&seen[ri][7]!=1)invalid();
    const auto expected=rule.record.root?chains_[rule.context.chain].parent:rule.context.child_chain;
    const double before_x=rule.record.root?0:rule.context.x,before_y=rule.record.root?0:rule.context.y;
    if(active_scroll!=parent_scroll||chain!=expected||x!=before_x||y!=before_y||command.data.draw_scrollbar_chrome.scroll_container_id!=rule.record.state.id)invalid();
   }else invalid();
   if(++seen[ri][slot]>rule.expected[slot])invalid();
  }else{
   if(entry_index<list.document_scopes.size()&&list.document_scopes[entry_index].command_index<=i)invalid();
   if(chain==absent)invalid();
  }
  if(tag==AETHER_PAINT_DRAW_BOX_SHADOW){
   if(shadow_index>=list.shadows.size())invalid();const auto&shadow=list.shadows[shadow_index++];
   if(shadow.command_index!=i)invalid();const auto owner=contexts_.find(shadow.box_identity);if(owner==contexts_.end())invalid();
   const auto&context=owner->second.paint;
   if(active_scroll!=owner->second.own_scroll)invalid();
   if(!context.painted||context.operation!=operation_||chain!=context.chain||x!=context.x||y!=context.y)invalid();
  }else if(shadow_index<list.shadows.size()&&list.shadows[shadow_index].command_index<=i)invalid();
 }
 if(!stack.empty()||active_scroll!=absent||chain!=absent||x||y||entry_index!=list.document_scopes.size()||shadow_index!=list.shadows.size())invalid();
 for(std::size_t i=0;i<rules_.size();++i)if(seen[i]!=rules_[i].expected)invalid();
 // No callbacks have occurred. Full comparison immediately precedes success.
 if(!accepts_checked(layout_,map_))invalid();
}
}
