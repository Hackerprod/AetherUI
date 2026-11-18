#include "b2_session_provenance.h"
#include "session_validation.h"
#include "raster_coordinate.h"
#include "../../layout/layout_internal.h"
#include <unordered_map>
#include <thread>
namespace aether::rendering::html_ltr {
namespace b2=aether::layout::b2;
using session_validation::require;
struct B2Provenance::State {
 b2::Ledger ledger;
 aether_doc_t doc;aether_style_map_t map;
 std::shared_ptr<const text::FontSource> regular,bold;
 uint64_t epoch,nonstate;std::thread::id thread=std::this_thread::get_id();
 struct Source {aether_node_s* node;std::shared_ptr<node_identity_s> identity;aether::style::FrozenNodeRef record; b2::Id origin,context;};
 struct Context {aether_node_s* owner;size_t sequence;bool finalized=false,preflight=false;size_t tokens=0,visible=0,token_base=0;};
 struct FinalRun {std::shared_ptr<const text::ShapedRun> run;aether_node_s* owner;const void* style;aether_rectf rect;float baseline;bool seen=false;};
 size_t next_token=0,next_fragment=0,next_group=0;
 std::pmr::unordered_map<const text::ShapedRun*,FinalRun> final_runs{ledger.resource()};
 std::pmr::unordered_map<aether_node_s*,size_t> ordinals{ledger.resource()};
 std::pmr::unordered_map<aether_node_s*,Source> sources{ledger.resource()};
 std::pmr::vector<Context> contexts{ledger.resource()};
 State(aether_doc_t d,aether_style_map_t m,std::shared_ptr<const text::FontSource> r,std::shared_ptr<const text::FontSource> b):doc(d),map(m),regular(std::move(r)),bold(std::move(b)),epoch(d->identity->structure_epoch),nonstate(d->identity->non_state_epoch){
  require(doc&&map&&regular&&bold);valid();inventory(doc);catalog(aether_doc_get_body(doc));ledger.freeze();valid();
 }
 void valid()const {
  require(thread==std::this_thread::get_id()&&doc&&doc->identity->alive&&doc->identity->structure_epoch==epoch&&doc->identity->non_state_epoch==nonstate&&aether_style_map_structure_valid(map)&&aether_style_map_inputs_current(map));
  for(const auto& [node,s]:sources){require(s.identity&&s.identity->alive&&s.identity->attached&&s.identity->owner==node&&node->identity==s.identity);const auto& owner=s.record;require(owner&&owner->node_lifetime()->alive&&owner->node_lifetime()->attached&&owner->node_lifetime()->owner==owner->node());}
 }
 void inventory(aether_node_s* node){
  ledger.counters().charge(b2::Quota::dom_nodes);
  if(node->type==AETHER_NODE_TEXT||node->type==AETHER_NODE_COMMENT){ledger.counters().charge(node->type==AETHER_NODE_TEXT?b2::Quota::text_nodes:b2::Quota::comments);ledger.counters().charge(b2::Quota::dom_bytes,node->text.size());}
  require(!node->is_generated);ordinals.emplace(node,ordinals.size());for(auto* child:node->children)inventory(child);
 }
 static bool inline_node(aether_node_s* n){return n->type==AETHER_NODE_TEXT||n->type==AETHER_NODE_COMMENT||(n->type==AETHER_NODE_ELEMENT&&(n->tag_name=="span"||n->tag_name=="strong"||n->tag_name=="a"||n->tag_name=="br"));}
 static bool control(aether_node_s* n){return n->tag_name=="input"||n->tag_name=="button";}
 void participant(aether_node_s* node,b2::Id context){
  require(inline_node(node));auto* owner=node->type==AETHER_NODE_ELEMENT?node:node->parent;
  auto record=map->current->backing()->find_owner(owner);require(bool(record));aether::layout::BoxStyleSnapshot st(record->style(),record->measures());
  const auto kind=node->type==AETHER_NODE_TEXT?b2::Kind::text:node->type==AETHER_NODE_COMMENT?b2::Kind::comment:node->tag_name=="br"?b2::Kind::br:b2::Kind::inline_node;
  const auto* font=st.font_weight==700?bold.get():regular.get();
  auto id=ledger.origin(reinterpret_cast<uintptr_t>(node),reinterpret_cast<uintptr_t>(owner),reinterpret_cast<uintptr_t>(record->style().get()),reinterpret_cast<uintptr_t>(font),ordinals.at(node),kind,context,node->type==AETHER_NODE_TEXT||node->type==AETHER_NODE_COMMENT?std::string_view(node->text):std::string_view());
  require(sources.emplace(node,Source{node,node->identity,record,id,context}).second);
  if(kind==b2::Kind::inline_node)for(auto* child:node->children)participant(child,context);
 }
 void catalog(aether_node_s* block){
  require(block);if(control(block))return;
  auto record=map->current->backing()->find_owner(block);require(bool(record));aether::layout::BoxStyleSnapshot st(record->style(),record->measures());
  size_t sequence=0;b2::Id context=b2::none;
  for(auto* child:block->children){if(inline_node(child)){
    if(context==b2::none){context=ledger.context(reinterpret_cast<uintptr_t>(block),sequence++,st.white_space,st.text_align);require(context==contexts.size());contexts.push_back({block,sequence-1,false});}
    participant(child,context);
   }else {context=b2::none;catalog(child);}
  }
 }
 static b2::Id id(size_t n){require(n<b2::none);return static_cast<b2::Id>(n);}
 b2::Id match(const B2InlineResult& result){
  require(!result.participants.empty());auto found=sources.find(result.participants.front().node);require(found!=sources.end());const auto context=found->second.context;
  size_t expected=0;for(const auto& o:ledger.origins())if(o.context==context)++expected;require(expected==result.participants.size());
  size_t ordinal=0;bool first=true;
  for(const auto& p:result.participants){ledger.counters().charge(b2::Quota::token_visits);auto i=sources.find(p.node);require(i!=sources.end());const auto& source=i->second;require(source.context==context&&source.record->node()==p.owner&&source.record->style().get()==p.style);const auto current=ledger.origins()[source.origin].ordinal;require(first||current>ordinal);ordinal=current;first=false;}
  return context;
 }
 void work(B2Work work,size_t amount){valid();switch(work){case B2Work::token_visits:ledger.counters().charge(b2::Quota::token_visits,amount);break;case B2Work::tokens:ledger.counters().charge(b2::Quota::tokens,amount);break;case B2Work::fragments:ledger.counters().charge(b2::Quota::fragments,amount);break;}valid();}
 void preflight(const B2InlineResult& result,size_t tokens,size_t visible){
  valid();const auto c=match(result);auto& context=contexts[c];
  if(context.preflight){require(context.tokens==tokens&&context.visible==visible);}else{ledger.counters().charge(b2::Quota::tokens,tokens);ledger.counters().charge(b2::Quota::visible_scalars,visible);context.tokens=tokens;context.visible=visible;context.token_base=next_token;next_token=b2::checked_add(next_token,tokens);context.preflight=true;}
  valid();
 }
 void inline_result(const B2InlineResult& result){
  valid();const auto c=match(result);auto& context=contexts[c];require(context.preflight&&!context.finalized);
  require(result.block_owner==context.owner);
  std::pmr::vector<b2::Id> lines(ledger.resource()),placements(ledger.resource());
  std::pmr::unordered_map<size_t,b2::Id> groups(ledger.resource());std::pmr::unordered_set<size_t> fragment_ids(ledger.resource());
  auto group=[&](size_t local){if(local==b2_no_index)return b2::none;auto it=groups.find(local);if(it!=groups.end())return it->second;const auto value=id(next_group++);groups.emplace(local,value);return value;};
  for(const auto& l:result.lines){ledger.counters().charge(b2::Quota::token_visits);require(std::isfinite(l.height)&&l.height>=0);lines.push_back(ledger.line(c,l.baseline,l.x,l.width));}
  for(const auto& p:result.placements){
   ledger.counters().charge(b2::Quota::token_visits);require(p.run&&p.line<lines.size()&&p.token<context.tokens&&fragment_ids.insert(p.fragment).second);
   auto record=map->current->backing()->find_owner(p.owner);require(record&&record->style().get()==p.style);aether::layout::BoxStyleSnapshot st(record->style(),record->measures());
   const auto& run=*p.run;require(run.font&&run.font->source==(st.font_weight==700?bold:regular)&&run.font->size_26_6==int(st.font_size*64)&&run.font->face_index==0&&run.metrics.width==p.width&&run.metrics.height==p.height&&run.metrics.baseline==p.baseline);
   validate_run_transport(run.source,run.glyphs,p.width,p.height,p.baseline);
   placements.push_back(ledger.placement(c,lines[p.line],id(context.token_base+p.token),id(next_fragment++),group(p.group),reinterpret_cast<uintptr_t>(p.owner),reinterpret_cast<uintptr_t>(p.style),reinterpret_cast<uintptr_t>(run.font->source.get()),p.x,p.y,p.width,p.height,p.baseline,run.glyphs.size(),run.source,false));
   require(final_runs.emplace(p.run.get(),FinalRun{p.run,p.owner,p.style,{p.x,p.y,p.width,p.height},p.baseline,false}).second);
  }
  for(const auto& r:result.ranges){
   ledger.counters().charge(b2::Quota::token_visits);auto found=sources.find(r.node);require(found!=sources.end()&&found->second.context==c&&r.end>=r.begin);const auto origin=found->second.origin;
   b2::Span span{origin,r.begin,r.end,b2::Fate::discarded};
   if(r.disposition==B2Disposition::visible||r.disposition==B2Disposition::contributor){require(r.placement<placements.size());span.placement=placements[r.placement];if(r.disposition==B2Disposition::visible){span.fate=b2::Fate::visible;span.run_begin=0;span.run_end=r.end-r.begin;}else{span.fate=b2::Fate::contributor;span.group=group(r.group);span.run_begin=0;span.run_end=1;}}
   else {b2::Discard reason;switch(r.disposition){case B2Disposition::leading:reason=b2::Discard::leading;break;case B2Disposition::trailing:reason=b2::Discard::trailing;break;case B2Disposition::soft_break:reason=b2::Discard::soft_break;break;case B2Disposition::empty_context:reason=b2::Discard::empty_context;break;default:throw Error{AETHER_ERROR_INTERNAL};}require(r.line==b2_no_index||r.line<lines.size());span.decision=ledger.decision(c,r.line==b2_no_index?b2::none:lines[r.line],reason);}
   ledger.span(span);
  }
  for(const auto& b:result.breaks){ledger.counters().charge(b2::Quota::token_visits);auto found=sources.find(b.node);require(found!=sources.end()&&found->second.context==c&&b.line<lines.size());ledger.forced_break(found->second.origin,lines[b.line]);}
  context.finalized=true;valid();
 }
 void validate_final(aether_layout_result_s* layout){
  valid();require(layout&&layout->root);for(const auto& c:contexts)require(c.finalized);ledger.validate();
  std::pmr::vector<aether_layout_box_s*> pending(ledger.resource());pending.push_back(layout->root);
  while(!pending.empty()){auto* box=pending.back();pending.pop_back();if(box->kind==LAYOUT_BOX_TEXT&&box->element&&!control(box->element)){
    const auto run=box->native_text.run;require(bool(run));auto i=final_runs.find(run.get());require(i!=final_runs.end());auto& expected=i->second;const auto r=box->content_box;require(!expected.seen&&expected.owner==box->element&&expected.style==box->style.style_owner().get()&&expected.run==run&&r.x==expected.rect.x&&r.y==expected.rect.y&&r.width==expected.rect.width&&r.height==expected.rect.height&&box->native_text.baseline==expected.baseline&&box->text==run->source);expected.seen=true;
   }for(auto* child:box->children)pending.push_back(child);
  }
  for(const auto& [run,expected]:final_runs)require(expected.seen);
  valid();
 }

};
B2Provenance::B2Provenance(aether_doc_t d,aether_style_map_t m,std::shared_ptr<const text::FontSource> r,std::shared_ptr<const text::FontSource> b):state_(std::make_unique<State>(d,m,std::move(r),std::move(b))){}
B2Provenance::~B2Provenance()=default;
void B2Provenance::valid()const{state_->valid();}
std::pmr::memory_resource* B2Provenance::resource(){return state_->ledger.resource();}
b2::Counters& B2Provenance::counters(){return state_->ledger.counters();}
const b2::Ledger& B2Provenance::ledger()const{return state_->ledger;}
void B2Provenance::work(B2Work w,size_t n){state_->work(w,n);}
void B2Provenance::preflight(const B2InlineResult& r,size_t t,size_t v){state_->preflight(r,t,v);}
void B2Provenance::inline_result(const B2InlineResult& r){state_->inline_result(r);}
void B2Provenance::validate_final(aether_layout_result_s* r){state_->validate_final(r);}
}
