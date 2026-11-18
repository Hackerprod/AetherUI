#include "b2_session_provenance.h"
#include "session_validation.h"
#include "raster_coordinate.h"
#include "../../layout/layout_internal.h"
#include <unordered_map>
#include <thread>
#include <optional>
namespace aether::rendering::html_ltr {
namespace b2=aether::layout::b2;
using session_validation::require;
struct B2Provenance::State {
 b2::Ledger ledger;
 aether_doc_t doc;aether_style_map_t map;
 std::shared_ptr<const text::FontSource> regular,bold;
 uint64_t epoch,nonstate;std::thread::id thread=std::this_thread::get_id();
 struct Source {aether_node_s* node;std::shared_ptr<node_identity_s> identity;aether::style::FrozenNodeRef record; b2::Id origin,context;};
 struct Context {aether_node_s* owner;size_t sequence;bool finalized=false,preflight=false,collection_seen=false;bool flex=false;size_t tokens=0,visible=0,token_base=0,range_begin=0,range_count=0;};
 struct FinalRun {std::shared_ptr<const text::ShapedRun> run;aether_node_s* owner;const void* style;aether_rectf rect;float baseline;bool seen=false;};
 struct Measurement {std::shared_ptr<const text::ShapedRun> run;aether_node_s* owner;const void* style;size_t id;};
 std::pmr::unordered_map<const text::ShapedRun*,Measurement> measurements{ledger.resource()};
 size_t next_token=0,next_fragment=0,next_group=0;
 std::pmr::vector<B2Range> canonical_ranges{ledger.resource()};
 std::pmr::unordered_map<const text::ShapedRun*,FinalRun> final_runs{ledger.resource()};
 std::pmr::unordered_map<aether_node_s*,size_t> ordinals{ledger.resource()};
 std::pmr::unordered_map<aether_node_s*,Source> sources{ledger.resource()};
 std::pmr::vector<Context> contexts{ledger.resource()};
 bool b3=false,sealed=false;
 aether_layout_result_s* active_result=nullptr;uint64_t result_id=0;
 struct BoxRecord {
  aether_layout_box_s* box=nullptr;uint64_t id=0,generation=0,parent_generation=0;bool live=true,complete=false,seen=false;
  aether_node_s* owner=nullptr;const void* style=nullptr;std::shared_ptr<node_identity_s> identity;
  aether_rectf content{},padding{},border{},margin{};
  const void* control=nullptr;uint64_t occurrence=0;const void* run=nullptr;
  bool width_seen=false,percentage=false,reference_definite=false;
  uint64_t containing=0;float reference_width=0,used_width=0;B3WidthReason width_reason=B3WidthReason::indefinite;
 };
 struct Generation {uint64_t parent;bool live=true;};
 struct Event {uint64_t result,generation,box;float dx,dy;bool replacement;uint64_t target_generation=0;};
 struct Candidate {
  uint64_t id=0,anchor=0,generation=0;size_t event_start=0,final_line_begin=0,final_placement_begin=0; b2::Id context=b2::none;bool live=true,committed=false,discard=false;
  std::optional<B2InlineResult> snapshot;
  std::pmr::vector<B2Line> expected_lines;
  std::pmr::vector<aether_rectf> expected_placements;
  std::pmr::vector<uint64_t> bindings;
  explicit Candidate(std::pmr::memory_resource* r):expected_lines(r),expected_placements(r),bindings(r){}
  Candidate(Candidate&&)=default;Candidate& operator=(Candidate&&)=default;
 };
 std::pmr::unordered_map<uint64_t,BoxRecord> boxes{ledger.resource()};
 std::pmr::unordered_map<aether_layout_box_s*,uint64_t> box_ids{ledger.resource()};
 std::pmr::unordered_map<uint64_t,Generation> generations{ledger.resource()};
 struct Replacement {aether_node_s* owner;uint64_t parent;bool fulfilled=false;};
 std::pmr::unordered_map<uint64_t,Replacement> replacements{ledger.resource()};
 std::pmr::vector<Event> events{ledger.resource()};
 std::pmr::vector<Candidate> candidates{ledger.resource()};
 struct FlexItem {aether_node_s* node;std::shared_ptr<node_identity_s> identity;aether::style::FrozenNodeRef record;};
 std::pmr::unordered_map<aether_node_s*,std::pmr::vector<FlexItem>> flex_items{ledger.resource()};
 static bool rect_equal(aether_rectf a,aether_rectf b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
 static void translate(aether_rectf& r,float dx,float dy){r.x+=dx;r.y+=dy;}
 BoxRecord& box_record(aether_layout_box_s* box){auto i=box_ids.find(box);require(i!=box_ids.end());auto& r=boxes.at(i->second);require(r.live&&r.box==box&&box->b3_box_id==r.id&&box->b3_generation_id==r.generation&&box->b3_parent_generation_id==r.parent_generation&&box->owner==active_result&&active_result->b3_result_id==result_id);return r;}
 void begin_result(aether_layout_result_s* result,uint64_t identity){if(!b3)return;valid();require(!active_result&&!sealed&&result&&identity&&result->b3_result_id==identity);active_result=result;result_id=identity;}
 void box_begin(aether_layout_result_s* result,aether_layout_box_s* box,uint64_t identity,uint64_t generation,uint64_t parent){
  if(!b3)return;valid();require(!sealed&&result==active_result&&identity&&generation&&box&&box->element&&box->b3_box_id==identity&&box->b3_generation_id==generation&&box->b3_parent_generation_id==parent);
  require(!boxes.contains(identity)&&!box_ids.contains(box));
  if(auto replacement=replacements.find(generation);replacement!=replacements.end()&&box->kind==LAYOUT_BOX_ELEMENT){require(!replacement->second.fulfilled&&replacement->second.owner==box->element&&replacement->second.parent==parent);replacement->second.fulfilled=true;}
  auto found=generations.find(generation);if(found==generations.end()){ledger.counters().charge(b2::Quota::generations);if(parent)require(generations.contains(parent)&&generations.at(parent).live);generations.emplace(generation,Generation{parent});}else require(found->second.live&&found->second.parent==parent);
  BoxRecord r;r.box=box;r.id=identity;r.generation=generation;r.parent_generation=parent;r.owner=box->element;r.style=box->style.style_owner().get();r.identity=box->element->identity;
  require(r.style&&r.identity&&r.identity->alive&&r.identity->owner==r.owner);
  boxes.emplace(identity,std::move(r));box_ids.emplace(box,identity);
 }
 void complete(aether_layout_box_s* box){if(!b3)return;require(!sealed);auto& r=box_record(box);require(!r.complete);r.content=box->content_box;r.padding=box->padding_box;r.border=box->border_box;r.margin=box->margin_box;r.complete=true;
  if(box->is_atomic_control){r.control=box->atomic_control.presentation.get();r.occurrence=box->atomic_control.occurrence;r.run=box->atomic_control.text.run.get();}
 }
 void replace(aether_layout_result_s* result,aether_layout_box_s* old){
  if(!b3||!old)return;valid();require(!sealed&&result==active_result);auto& root=box_record(old);ledger.counters().charge(b2::Quota::events);const auto target=active_result->b3_next_generation_id+1;require(target>root.generation&&target);events.push_back({result_id,root.generation,root.id,0,0,true,target});require(replacements.emplace(target,Replacement{root.owner,root.parent_generation}).second);
  std::pmr::vector<aether_layout_box_s*> pending(ledger.resource());pending.push_back(old);
  while(!pending.empty()){auto* box=pending.back();pending.pop_back();ledger.counters().charge(b2::Quota::token_visits);auto& r=box_record(box);require(r.complete);r.live=false;generations.at(r.generation).live=false;
   for(auto& candidate:candidates){ledger.counters().charge(b2::Quota::token_visits);if(candidate.live&&candidate.anchor==r.id){candidate.live=false;candidate.snapshot.reset();std::pmr::vector<B2Line>(ledger.resource()).swap(candidate.expected_lines);std::pmr::vector<aether_rectf>(ledger.resource()).swap(candidate.expected_placements);std::pmr::vector<uint64_t>(ledger.resource()).swap(candidate.bindings);}}
   box_ids.erase(box);for(auto* child:box->children)pending.push_back(child);
  }
 }
 void offset(aether_layout_box_s* box,float dx,float dy){
  if(!b3)return;require(!sealed&&std::isfinite(dx)&&std::isfinite(dy));auto& record=box_record(box);if(dx==0&&dy==0)return;
  ledger.counters().charge(b2::Quota::events);events.push_back({result_id,record.generation,record.id,dx,dy,false});
  if(record.complete){require(rect_equal(record.content,box->content_box)&&rect_equal(record.padding,box->padding_box)&&rect_equal(record.border,box->border_box)&&rect_equal(record.margin,box->margin_box));translate(record.content,dx,dy);translate(record.padding,dx,dy);translate(record.border,dx,dy);translate(record.margin,dx,dy);}
  for(auto& c:candidates){ledger.counters().charge(b2::Quota::token_visits);if(c.live){
   if(c.anchor==record.id)for(auto& line:c.expected_lines){line.x+=dx;line.y+=dy;line.baseline+=dy;}
   const auto& snapshot=*c.snapshot;for(size_t i=0;i<snapshot.placements.size();++i){ledger.counters().charge(b2::Quota::token_visits);if(c.bindings[i]==record.id)translate(c.expected_placements[i],dx,dy);}
  }}
 }
 void width(aether_layout_result_s* result,aether_layout_box_s* box,aether_layout_box_s* containing,float reference,float used,B3WidthReason reason,bool percentage,bool definite){
  if(!b3)return;require(!sealed&&result==active_result&&std::isfinite(reference)&&reference>=0&&std::isfinite(used)&&used>=0);auto& r=box_record(box);require(!r.width_seen);r.width_seen=true;r.reference_width=reference;r.used_width=used;r.width_reason=reason;r.percentage=percentage;r.reference_definite=definite;r.containing=containing?box_record(containing).id:0;
 }
 void validate_snapshot(const B2InlineResult& result,b2::Id c){
  auto& context=contexts[c];require(context.preflight&&!context.finalized&&result.block_owner==context.owner&&context.range_count==result.ranges.size());
  for(size_t i=0;i<result.ranges.size();++i){ledger.counters().charge(b2::Quota::token_visits);const auto& a=canonical_ranges[context.range_begin+i];const auto& z=result.ranges[i];require(a.node==z.node&&a.begin==z.begin&&a.end==z.end&&a.token==z.token&&a.fragment==z.fragment&&(a.group==z.group||(z.disposition==B2Disposition::soft_break&&z.group==b2_no_index))&&(a.disposition==z.disposition||(a.disposition==B2Disposition::contributor&&z.disposition==B2Disposition::soft_break)));}
  validate_geometry(result,c);
  for(const auto& r:result.ranges){const auto& source=sources.at(r.node);const auto& o=ledger.origins()[source.origin];if(r.disposition==B2Disposition::visible||r.disposition==B2Disposition::contributor){require(r.placement<result.placements.size());const auto& p=result.placements[r.placement];require(p.token==r.token&&p.fragment==r.fragment&&p.line==r.line&&p.run);if(r.disposition==B2Disposition::visible)require(p.owner==source.record->node()&&p.style==source.record->style().get()&&p.run->source==std::string_view(o.text).substr(r.begin,r.end-r.begin)&&p.group==b2_no_index);else require(p.run->source==" "&&p.group==r.group);}}
  std::pmr::unordered_set<aether_node_s*> breaks_seen(ledger.resource());for(const auto& br:result.breaks){auto source=sources.find(br.node);require(source!=sources.end()&&source->second.context==c&&br.node->tag_name=="br"&&br.line<result.lines.size()&&breaks_seen.insert(br.node).second);}for(const auto& o:ledger.origins())if(o.context==c&&o.kind==b2::Kind::br)require(breaks_seen.contains(reinterpret_cast<aether_node_s*>(o.identity)));
  std::pmr::unordered_set<size_t> fragments_seen(ledger.resource());std::pmr::unordered_set<const text::ShapedRun*> runs_seen(ledger.resource());
  for(const auto& p:result.placements){ledger.counters().charge(b2::Quota::token_visits);require(fragments_seen.insert(p.fragment).second&&runs_seen.insert(p.run.get()).second);require(p.run&&p.line<result.lines.size()&&p.token<context.tokens&&p.fragment!=b2_no_index);auto record=map->current->backing()->find_owner(p.owner);require(record&&record->style().get()==p.style);aether::layout::BoxStyleSnapshot st(record->style(),record->measures());const auto m=measurements.find(p.run.get());require(m!=measurements.end()&&m->second.owner==p.owner&&m->second.style==p.style);const auto& run=*p.run;require(run.font&&run.font->source==(st.font_weight==700?bold:regular)&&run.font->size_26_6==int(st.font_size*64)&&run.font->face_index==0&&run.metrics.width==p.width&&run.metrics.height==p.height&&run.metrics.baseline==p.baseline);validate_run_transport(run.source,run.glyphs,p.width,p.height,p.baseline);}
 }
 Candidate& capture(const B2InlineResult& input,b2::Id context,bool discard){
  require(!sealed&&input.anchor);auto& anchor=box_record(input.anchor);require(input.result_id==result_id&&input.generation_id==anchor.generation&&input.anchor_box_id==anchor.id);require(anchor.box->kind==LAYOUT_BOX_ELEMENT&&anchor.owner==input.block_owner&&generations.at(anchor.generation).live);
  for(const auto& c:candidates)require(!c.live||c.context!=context);
  ledger.counters().charge(b2::Quota::candidates);ledger.counters().charge(b2::Quota::bindings,input.placements.size());ledger.counters().charge(b2::Quota::lines,input.lines.size());ledger.counters().charge(b2::Quota::placements,input.placements.size());ledger.counters().charge(b2::Quota::fragments,input.ranges.size());
  candidates.emplace_back(ledger.resource());auto& c=candidates.back();c.id=candidates.size();c.event_start=events.size();c.context=context;c.anchor=anchor.id;c.generation=anchor.generation;c.discard=discard;c.snapshot.emplace(ledger.resource());*c.snapshot=input;c.expected_lines.assign(input.lines.begin(),input.lines.end());
  for(const auto& p:input.placements){auto& r=box_record(p.box);require(p.box_id==r.id&&r.complete&&r.generation==anchor.generation&&r.owner==p.owner&&r.style==p.style&&p.box->native_text.run==p.run&&p.box->native_text.baseline==p.baseline&&p.box->text==p.run->source&&rect_equal(r.content,{p.x,p.y,p.width,p.height}));c.bindings.push_back(r.id);c.expected_placements.push_back({p.x,p.y,p.width,p.height});}
  return c;
 }
 void discard(aether_layout_result_s* result,aether_layout_box_s* anchor,const B2InlineResult& input,std::span<aether_node_s* const> items){
  if(!b3)return;valid();require(!sealed&&(!result||result==active_result));const auto c=match(input);require(contexts[c].flex&&input.block_owner==contexts[c].owner&&input.lines.empty()&&input.placements.empty()&&input.breaks.empty()&&input.soft_breaks.empty());
  const auto f=flex_items.find(input.block_owner);require(f!=flex_items.end()&&f->second.size()==items.size());for(size_t i=0;i<items.size();++i){ledger.counters().charge(b2::Quota::token_visits);const auto& frozen=f->second[i];require(frozen.node==items[i]&&frozen.identity==items[i]->identity&&frozen.identity->alive&&frozen.identity->attached&&frozen.identity->owner==items[i]&&frozen.record==map->current->backing()->find_owner(items[i]));}
  size_t range=0;for(const auto& p:input.participants){ledger.counters().charge(b2::Quota::token_visits);const auto& source=sources.at(p.node);const auto& o=ledger.origins()[source.origin];require((o.kind==b2::Kind::text||o.kind==b2::Kind::comment)&&std::string_view(o.text)==std::string_view(p.node->text));if(o.kind==b2::Kind::text){require(b2::whitespace(o.text));if(!o.text.empty()){require(range<input.ranges.size());const auto& r=input.ranges[range++];require(r.node==p.node&&r.begin==0&&r.end==o.text.size()&&r.disposition==B2Disposition::flex_whitespace&&r.placement==b2_no_index&&r.group==b2_no_index&&r.line==b2_no_index&&r.token==b2_no_index&&r.fragment==b2_no_index);}}}
  require(range==input.ranges.size());if(!anchor)return;require(input.anchor==anchor);auto& ar=box_record(anchor);require(input.result_id==result_id&&input.generation_id==ar.generation&&input.anchor_box_id==ar.id);
  for(const auto& existing:candidates)if(existing.live&&existing.context==c){require(existing.discard&&existing.anchor==ar.id&&existing.generation==ar.generation);return;}
  capture(input,c,true);
 }
 void seal(aether_layout_result_s* result){
  if(!b3)return;valid();require(!sealed&&result==active_result&&result->root);sealed=true;
  std::pmr::vector<aether_layout_box_s*> pending(ledger.resource());pending.push_back(result->root);
  while(!pending.empty()){auto* box=pending.back();pending.pop_back();ledger.counters().charge(b2::Quota::final_visits);auto& r=box_record(box);require(!r.seen&&r.complete&&generations.at(r.generation).live);r.seen=true;require(r.owner==box->element&&r.style==box->style.style_owner().get()&&r.identity==box->element->identity&&r.identity->alive&&r.identity->attached&&r.identity->owner==box->element);require(rect_equal(r.content,box->content_box)&&rect_equal(r.padding,box->padding_box)&&rect_equal(r.border,box->border_box)&&rect_equal(r.margin,box->margin_box));
   if(box->is_atomic_control)require(r.control==box->atomic_control.presentation.get()&&r.occurrence==box->atomic_control.occurrence&&r.run==box->atomic_control.text.run.get());
   if(r.width_seen){require(r.used_width==r.content.width);if(r.containing){auto& parent=boxes.at(r.containing);require(parent.live&&generations.at(parent.generation).live);if(r.percentage)require(parent.content.width==r.reference_width);}if(r.percentage&&!r.reference_definite)throw Error{AETHER_ERROR_UNSUPPORTED};
    if(r.width_reason==B3WidthReason::block_fill||r.percentage||r.width_reason==B3WidthReason::flex_final){if(!r.reference_definite) {if(r.percentage)throw Error{AETHER_ERROR_UNSUPPORTED};}else{std::pmr::unordered_set<uint64_t> chain(ledger.resource());uint64_t at=r.id;while(at){ledger.counters().charge(b2::Quota::token_visits);require(chain.insert(at).second);const auto& dependency=boxes.at(at);require(dependency.live&&dependency.width_seen);if((dependency.width_reason==B3WidthReason::px&&!dependency.percentage)||dependency.width_reason==B3WidthReason::viewport)break;if(!dependency.reference_definite)throw Error{AETHER_ERROR_UNSUPPORTED};if(!dependency.containing){require(dependency.box==result->root&&dependency.reference_width==result->viewport.width);break;}at=dependency.containing;}}}
   }
   for(auto* child:box->children)pending.push_back(child);
  }
  for(const auto& [id,r]:boxes)require(r.live==r.seen);
  for(const auto& [id,replacement]:replacements)require(replacement.fulfilled);
  for(auto& candidate:candidates){if(!candidate.live){require(!candidate.snapshot);continue;}require(!candidate.committed&&boxes.at(candidate.anchor).seen&&generations.at(candidate.generation).live);auto& context=contexts[candidate.context];require(!context.finalized);const auto& snapshot=*candidate.snapshot;
   // Rebuild expected transport from immutable local snapshots and ordered
   // journal entries. Incremental expected values are checked, never trusted
   // as a substitute for the original snapshot or the authentic box binding.
   std::pmr::vector<B2Line> replay_lines(snapshot.lines.begin(),snapshot.lines.end(),ledger.resource());
   std::pmr::vector<aether_rectf> replay_placements(ledger.resource());for(const auto& placement:snapshot.placements)replay_placements.push_back({placement.x,placement.y,placement.width,placement.height});
   for(size_t index=candidate.event_start;index<events.size();++index){ledger.counters().charge(b2::Quota::token_visits);const auto& event=events[index];require(event.result==result_id);if(event.replacement){require(event.generation!=candidate.generation);continue;}if(event.box==candidate.anchor)for(auto& line:replay_lines){line.x+=event.dx;line.y+=event.dy;line.baseline+=event.dy;}for(size_t i=0;i<candidate.bindings.size();++i){ledger.counters().charge(b2::Quota::token_visits);if(candidate.bindings[i]==event.box)translate(replay_placements[i],event.dx,event.dy);}}
   for(size_t i=0;i<replay_lines.size();++i){const auto& a=replay_lines[i];const auto& b=candidate.expected_lines[i];require(a.x==b.x&&a.y==b.y&&a.baseline==b.baseline&&a.width==b.width&&a.height==b.height&&a.end==b.end);}for(size_t i=0;i<replay_placements.size();++i)require(rect_equal(replay_placements[i],candidate.expected_placements[i]));
   if(candidate.discard){for(const auto& range:snapshot.ranges){const auto& source=sources.at(range.node);b2::Span span{source.origin,range.begin,range.end,b2::Fate::discarded};span.decision=ledger.decision(candidate.context,b2::none,b2::Discard::flex_whitespace);ledger.span(span);}context.finalized=true;}
   else {const auto line_begin=ledger.lines().size(),placement_begin=ledger.placements().size();candidate.final_line_begin=line_begin;candidate.final_placement_begin=placement_begin;commit_inline(snapshot);for(size_t i=0;i<candidate.expected_lines.size();++i){const auto& expected=candidate.expected_lines[i];ledger.transport_line(id(line_begin+i),expected.x,expected.baseline);}for(size_t i=0;i<candidate.expected_placements.size();++i){const auto& expected=candidate.expected_placements[i];const auto& original=snapshot.placements[i];const auto& r=boxes.at(candidate.bindings[i]);require(r.seen&&r.id==original.box->b3_box_id&&r.box==original.box&&rect_equal(r.content,expected));auto& final=final_runs.at(original.run.get());final.rect=expected;ledger.transport_placement(id(placement_begin+i),expected.x,expected.y);}}
   candidate.committed=true;
  }
  for(const auto& context:contexts)require(context.finalized);
 }

 State(aether_doc_t d,aether_style_map_t m,std::shared_ptr<const text::FontSource> r,std::shared_ptr<const text::FontSource> b,bool enable_b3):doc(d),map(m),regular(std::move(r)),bold(std::move(b)),epoch(d->identity->structure_epoch),nonstate(d->identity->non_state_epoch){
  b3=enable_b3;require(doc&&map&&regular&&bold);valid();inventory(doc);catalog(aether_doc_get_body(doc));ledger.freeze();valid();
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
  const bool flex=b3&&st.display==AETHER_DISPLAY_FLEX;
  if(flex){auto [it,inserted]=flex_items.try_emplace(block);require(inserted);for(auto* child:block->children)if(child->type==AETHER_NODE_ELEMENT)it->second.push_back({child,child->identity,map->current->backing()->find_owner(child)});}
  size_t sequence=0;b2::Id context=b2::none;
  for(auto* child:block->children){if(inline_node(child)){
    if(context==b2::none){context=ledger.context(reinterpret_cast<uintptr_t>(block),sequence++,st.white_space,st.text_align);require(context==contexts.size());contexts.push_back({block,sequence-1});contexts.back().flex=flex;}
    participant(child,context);
   }else {context=b2::none;catalog(child);}
  }
 }
 static b2::Id id(size_t n){require(n<b2::none);return static_cast<b2::Id>(n);}
 b2::Id match(const B2InlineResult& result){
  require(!result.participants.empty());auto found=sources.find(result.participants.front().node);require(found!=sources.end());const auto context=found->second.context;
  size_t expected=0;for(const auto& o:ledger.origins())if(o.context==context)++expected;require(expected==result.participants.size());
  size_t ordinal=0;bool first=true;
  for(const auto& p:result.participants){ledger.counters().charge(b2::Quota::token_visits);auto i=sources.find(p.node);require(i!=sources.end());const auto& source=i->second;require(source.context==context&&source.record->node()==p.owner&&source.record->style().get()==p.style);if(b3&&(p.node->type==AETHER_NODE_TEXT||p.node->type==AETHER_NODE_COMMENT))require(std::string_view(ledger.origins()[source.origin].text)==std::string_view(p.node->text));const auto current=ledger.origins()[source.origin].ordinal;require(first||current>ordinal);ordinal=current;first=false;}
  return context;
 }
 bool begin_collection(const B2InlineResult& r){valid();auto& context=contexts[match(r)];const bool first=!context.collection_seen;context.collection_seen=true;return first;}
 void record_measurement(std::shared_ptr<const text::ShapedRun> run,aether_node_s* owner,const void* style){valid();require(bool(run));const auto id=ledger.counters().used(b2::Quota::bridge_calls);require(id>0);require(measurements.emplace(run.get(),Measurement{run,owner,style,id}).second);valid();}
 void work(B2Work work,size_t amount){valid();switch(work){case B2Work::token_visits:ledger.counters().charge(b2::Quota::token_visits,amount);break;case B2Work::tokens:ledger.counters().charge(b2::Quota::tokens,amount);break;case B2Work::fragments:ledger.counters().charge(b2::Quota::fragments,amount);break;case B2Work::lines:ledger.counters().charge(b2::Quota::lines,amount);break;case B2Work::placements:ledger.counters().charge(b2::Quota::placements,amount);break;case B2Work::glyphs:ledger.counters().charge(b2::Quota::glyphs,amount);break;}valid();}
 void preflight(const B2InlineResult& result,size_t tokens,size_t visible){
  valid();const auto c=match(result);auto& context=contexts[c];
  std::pmr::vector<size_t> covered(ledger.origins().size(),0,ledger.resource());std::pmr::unordered_set<size_t> groups(ledger.resource());size_t normalized=0;
  size_t last_ordinal=0;bool first=true;
  for(const auto& r:result.ranges){ledger.counters().charge(b2::Quota::token_visits);auto found=sources.find(r.node);require(found!=sources.end()&&found->second.context==c);const auto origin=found->second.origin;const auto& o=ledger.origins()[origin];require(o.kind==b2::Kind::text&&r.begin==covered[origin]&&r.end>r.begin&&b2::boundary(o.text,r.begin)&&b2::boundary(o.text,r.end)&&r.placement==b2_no_index);require(first||o.ordinal>=last_ordinal);first=false;last_ordinal=o.ordinal;covered[origin]=r.end;auto raw=std::string_view(o.text).substr(r.begin,r.end-r.begin);
   if(r.disposition==B2Disposition::visible){normalized=b2::checked_add(normalized,b2::scalar_count(raw));require(r.group==b2_no_index&&r.token<tokens&&r.fragment!=b2_no_index);}else{require(b2::whitespace(raw));if(r.disposition==B2Disposition::contributor){require(r.group<tokens&&r.token==r.group&&r.fragment!=b2_no_index);if(groups.insert(r.group).second)normalized=b2::checked_add(normalized,1);}else require((r.disposition==B2Disposition::leading||r.disposition==B2Disposition::trailing||r.disposition==B2Disposition::empty_context)&&r.token==b2_no_index&&r.fragment==b2_no_index);}
  }
  for(size_t i=0;i<ledger.origins().size();++i){const auto& o=ledger.origins()[i];if(o.context==c&&o.kind==b2::Kind::text)require(covered[i]==o.text.size());}
  require(normalized==visible);
  if(context.preflight){require(context.tokens==tokens&&context.visible==visible&&context.range_count==result.ranges.size());for(size_t i=0;i<result.ranges.size();++i){ledger.counters().charge(b2::Quota::token_visits);const auto& a=canonical_ranges[context.range_begin+i];const auto& b=result.ranges[i];require(a.node==b.node&&a.begin==b.begin&&a.end==b.end&&a.disposition==b.disposition&&a.group==b.group&&a.token==b.token&&a.fragment==b.fragment);}}
  else {require(context.collection_seen);ledger.counters().charge(b2::Quota::visible_scalars,visible);context.tokens=tokens;context.visible=visible;context.token_base=next_token;next_token=b2::checked_add(next_token,tokens);context.range_begin=canonical_ranges.size();context.range_count=result.ranges.size();for(const auto& r:result.ranges){ledger.counters().charge(b2::Quota::fragments);canonical_ranges.push_back(r);}context.preflight=true;}
  valid();
 }
 void validate_geometry(const B2InlineResult& result,b2::Id context){
  auto record=map->current->backing()->find_owner(contexts[context].owner);require(bool(record));aether::layout::BoxStyleSnapshot st(record->style(),record->measures());
  for(float value:{result.start_x,result.start_y,result.available_width,result.minimum_line_height})require(std::isfinite(value));
  require(result.available_width>=0&&result.minimum_line_height==st.line_height);
  std::pmr::vector<size_t> forced(result.lines.size(),0,ledger.resource());for(const auto& b:result.breaks){require(b.line<forced.size());++forced[b.line];}
  size_t position=0;float y=result.start_y;
  std::pmr::unordered_map<size_t,size_t> token_line(ledger.resource());
  for(size_t i=0;i<result.lines.size();++i){ledger.counters().charge(b2::Quota::token_visits);const auto& line=result.lines[i];const auto begin=position;float width=0,ascent=0,descent=0,height=result.minimum_line_height;
   while(position<result.placements.size()&&result.placements[position].line==i){ledger.counters().charge(b2::Quota::token_visits);const auto& p=result.placements[position++];require(bool(p.run));width+=p.run->metrics.width;ascent=std::max(ascent,p.run->metrics.baseline);descent=std::max(descent,p.run->metrics.height-p.run->metrics.baseline);height=std::max(height,p.run->metrics.height);auto [it,inserted]=token_line.emplace(p.token,i);require(inserted||it->second==i);}
   height=std::max(height,ascent+descent);const float x=result.start_x+(st.text_align==AETHER_TEXT_ALIGN_CENTER?std::max(0.f,(result.available_width-width)/2.f):0.f);
   require(line.x==x&&line.y==y&&line.baseline==y+ascent&&line.width==width&&line.height==height);
   require(forced[i]==(line.end==B2LineEnd::forced?1u:0u));if(position==begin)require(line.end==B2LineEnd::forced);if(line.end==B2LineEnd::paragraph)require(i+1==result.lines.size());if(line.end==B2LineEnd::soft)require(st.white_space==AETHER_WHITE_SPACE_NORMAL&&i+1<result.lines.size());
   float xx=x;for(size_t p=begin;p<position;++p){ledger.counters().charge(b2::Quota::token_visits);const auto& placement=result.placements[p];require(placement.x==xx&&placement.y==y+std::max(0.f,ascent-placement.baseline));xx+=placement.width;}
   y+=height;
  }
  require(position==result.placements.size());
  std::pmr::vector<unsigned> soft_counts(result.lines.size(),0,ledger.resource());std::pmr::unordered_set<size_t> soft_groups(ledger.resource());
  for(const auto& soft:result.soft_breaks){ledger.counters().charge(b2::Quota::token_visits);require(soft.line+1<result.lines.size()&&soft.space&&soft.space->source==" "&&soft_groups.insert(soft.group).second&&++soft_counts[soft.line]==1&&result.lines[soft.line].end==B2LineEnd::soft);auto measurement=measurements.find(soft.space.get());require(measurement!=measurements.end());
   const B2Range* original=nullptr;const auto& ctx=contexts[context];for(size_t i=0;i<ctx.range_count;++i){ledger.counters().charge(b2::Quota::token_visits);const auto& candidate=canonical_ranges[ctx.range_begin+i];if(candidate.disposition==B2Disposition::contributor&&candidate.group==soft.group){original=&candidate;break;}}require(original);const auto& source=sources.at(original->node);require(measurement->second.owner==source.record->node()&&measurement->second.style==source.record->style().get());
   const auto& run=*soft.space;aether::layout::BoxStyleSnapshot style(source.record->style(),source.record->measures());require(run.font&&run.font->source==(style.font_weight==700?bold:regular)&&run.font->size_26_6==int(style.font_size*64));validate_run_transport(run.source,run.glyphs,run.metrics.width,run.metrics.height,run.metrics.baseline);
   size_t token=b2_no_index;float next_width=0;for(const auto& p:result.placements){ledger.counters().charge(b2::Quota::token_visits);if(p.line!=soft.line+1)continue;if(token==b2_no_index)token=p.token;if(p.token!=token)break;next_width+=p.width;}require(token!=b2_no_index&&(result.lines[soft.line].width+run.metrics.width)+next_width>result.available_width);
  }
  for(size_t i=0;i<result.lines.size();++i)require(soft_counts[i]==(result.lines[i].end==B2LineEnd::soft?1u:0u));
  const auto& ctx=contexts[context];for(size_t i=0;i<result.ranges.size();++i){const auto& r=result.ranges[i];if(r.disposition==B2Disposition::soft_break){ledger.counters().charge(b2::Quota::token_visits);const auto group=canonical_ranges[ctx.range_begin+i].group;bool found=false;for(const auto& soft:result.soft_breaks){ledger.counters().charge(b2::Quota::token_visits);if(soft.group==group&&soft.line==r.line)found=true;}require(found);}}
 }
 void inline_result(const B2InlineResult& result){if(b3){valid();auto c=match(result);require(!contexts[c].flex);validate_snapshot(result,c);capture(result,c,false);}else commit_inline(result);}
 void commit_inline(const B2InlineResult& result){
  valid();const auto c=match(result);auto& context=contexts[c];require(context.preflight&&!context.finalized);
  require(result.block_owner==context.owner&&context.range_count==result.ranges.size());
  for(size_t i=0;i<result.ranges.size();++i){ledger.counters().charge(b2::Quota::token_visits);const auto& original=canonical_ranges[context.range_begin+i];const auto& actual=result.ranges[i];require(original.node==actual.node&&original.begin==actual.begin&&original.end==actual.end&&original.token==actual.token&&original.fragment==actual.fragment&&(original.group==actual.group||(actual.disposition==B2Disposition::soft_break&&actual.group==b2_no_index)));require(original.disposition==actual.disposition||(original.disposition==B2Disposition::contributor&&actual.disposition==B2Disposition::soft_break));}
  validate_geometry(result,c);
  std::pmr::vector<b2::Id> lines(ledger.resource()),placements(ledger.resource());
  std::pmr::unordered_map<size_t,b2::Id> groups(ledger.resource()),fragment_ids(ledger.resource());
  auto group=[&](size_t local){if(local==b2_no_index)return b2::none;auto it=groups.find(local);if(it!=groups.end())return it->second;const auto value=id(next_group++);groups.emplace(local,value);return value;};
  for(const auto& l:result.lines){ledger.counters().charge(b2::Quota::token_visits);require(std::isfinite(l.height)&&l.height>=0);lines.push_back(ledger.line(c,l.baseline,l.x,l.width,false));}
  for(const auto& p:result.placements){
   ledger.counters().charge(b2::Quota::token_visits);require(p.run&&p.line<lines.size()&&p.token<context.tokens&&p.fragment!=b2_no_index);
   auto record=map->current->backing()->find_owner(p.owner);require(record&&record->style().get()==p.style);aether::layout::BoxStyleSnapshot st(record->style(),record->measures());
   const auto receipt=measurements.find(p.run.get());require(receipt!=measurements.end()&&receipt->second.owner==p.owner&&receipt->second.style==p.style);
   const auto& run=*p.run;require(run.font&&run.font->source==(st.font_weight==700?bold:regular)&&run.font->size_26_6==int(st.font_size*64)&&run.font->face_index==0&&run.metrics.width==p.width&&run.metrics.height==p.height&&run.metrics.baseline==p.baseline);
   validate_run_transport(run.source,run.glyphs,p.width,p.height,p.baseline);
   const auto fragment=id(next_fragment++);require(fragment_ids.emplace(p.fragment,fragment).second);
   placements.push_back(ledger.placement(c,lines[p.line],id(context.token_base+p.token),fragment,group(p.group),reinterpret_cast<uintptr_t>(p.owner),reinterpret_cast<uintptr_t>(p.style),reinterpret_cast<uintptr_t>(run.font->source.get()),p.x,p.y,p.width,p.height,p.baseline,run.glyphs.size(),run.source,false,false));
   require(final_runs.emplace(p.run.get(),FinalRun{p.run,p.owner,p.style,{p.x,p.y,p.width,p.height},p.baseline,false}).second);
  }
  for(const auto& r:result.ranges){
   ledger.counters().charge(b2::Quota::token_visits);auto found=sources.find(r.node);require(found!=sources.end()&&found->second.context==c&&r.end>=r.begin);const auto origin=found->second.origin;
   b2::Span span{origin,r.begin,r.end,b2::Fate::discarded};
   if(r.disposition==B2Disposition::visible||r.disposition==B2Disposition::contributor){require(r.placement<placements.size()&&r.token==result.placements[r.placement].token&&r.fragment==result.placements[r.placement].fragment);span.expected_token=id(context.token_base+r.token);span.expected_fragment=fragment_ids.at(r.fragment);span.placement=placements[r.placement];if(r.disposition==B2Disposition::visible){span.fate=b2::Fate::visible;span.run_begin=0;span.run_end=r.end-r.begin;}else{span.fate=b2::Fate::contributor;span.group=group(r.group);span.run_begin=0;span.run_end=1;}}
   else {b2::Discard reason;switch(r.disposition){case B2Disposition::leading:reason=b2::Discard::leading;break;case B2Disposition::trailing:reason=b2::Discard::trailing;break;case B2Disposition::soft_break:reason=b2::Discard::soft_break;break;case B2Disposition::empty_context:reason=b2::Discard::empty_context;break;default:throw Error{AETHER_ERROR_INTERNAL};}require(r.line==b2_no_index||r.line<lines.size());span.decision=ledger.decision(c,r.line==b2_no_index?b2::none:lines[r.line],reason);}
   ledger.span(span);
  }
  for(const auto& b:result.breaks){ledger.counters().charge(b2::Quota::token_visits);auto found=sources.find(b.node);require(found!=sources.end()&&found->second.context==c&&b.line<lines.size());ledger.forced_break(found->second.origin,lines[b.line]);}
  context.finalized=true;valid();
 }
 void validate_committed(){
  if(!b3)return;require(sealed);size_t line_count=0,placement_count=0;
  for(const auto& c:candidates){if(!c.live)continue;require(c.committed&&c.snapshot);const auto& original=*c.snapshot;if(c.discard)continue;
   require(c.final_line_begin==line_count&&c.final_placement_begin==placement_count);
   for(size_t i=0;i<c.expected_lines.size();++i){ledger.counters().charge(b2::Quota::token_visits);require(line_count<ledger.lines().size());const auto& actual=ledger.lines()[line_count++];const auto& expected=c.expected_lines[i];require(actual.context==c.context&&actual.x==expected.x&&actual.baseline==expected.baseline&&actual.width==expected.width);}
   for(size_t i=0;i<c.expected_placements.size();++i){ledger.counters().charge(b2::Quota::token_visits);require(placement_count<ledger.placements().size());const auto& actual=ledger.placements()[placement_count++];const auto& expected=c.expected_placements[i];const auto& frozen=original.placements[i];require(actual.context==c.context&&actual.line==c.final_line_begin+frozen.line&&actual.owner==reinterpret_cast<uintptr_t>(frozen.owner)&&actual.style==reinterpret_cast<uintptr_t>(frozen.style)&&actual.font==reinterpret_cast<uintptr_t>(frozen.run->font->source.get())&&actual.x==expected.x&&actual.y==expected.y&&actual.width==expected.width&&actual.height==expected.height&&actual.baseline==frozen.baseline&&actual.glyphs==frozen.run->glyphs.size()&&std::string_view(actual.text)==std::string_view(frozen.run->source));}
  }
  require(line_count==ledger.lines().size()&&placement_count==ledger.placements().size());
 }
 void validate_final(aether_layout_result_s* layout){
  if(b3)seal(layout);valid();require(layout&&layout->root);for(const auto& c:contexts)require(c.finalized);
#ifdef AETHER_HTML_LTR_TESTING
  testing::b2_inspect(ledger);
#endif
  ledger.validate();validate_committed();
  std::pmr::vector<aether_layout_box_s*> pending(ledger.resource());pending.push_back(layout->root);
  while(!pending.empty()){auto* box=pending.back();pending.pop_back();if(box->kind==LAYOUT_BOX_TEXT&&box->element&&!control(box->element)){
    const auto run=box->native_text.run;require(bool(run));auto i=final_runs.find(run.get());require(i!=final_runs.end());auto& expected=i->second;const auto r=box->content_box;require(!expected.seen&&expected.owner==box->element&&expected.style==box->style.style_owner().get()&&expected.run==run&&r.x==expected.rect.x&&r.y==expected.rect.y&&r.width==expected.rect.width&&r.height==expected.rect.height&&box->native_text.baseline==expected.baseline&&box->text==run->source);expected.seen=true;
   }for(auto* child:box->children)pending.push_back(child);
  }
  for(const auto& [run,expected]:final_runs)require(expected.seen);
  valid();
 }

};
B2Provenance::B2Provenance(aether_doc_t d,aether_style_map_t m,std::shared_ptr<const text::FontSource> r,std::shared_ptr<const text::FontSource> b,bool b3):state_(std::make_unique<State>(d,m,std::move(r),std::move(b),b3)){}
B2Provenance::~B2Provenance()=default;
void B2Provenance::valid()const{state_->valid();}
std::pmr::memory_resource* B2Provenance::resource(){return state_->ledger.resource();}
b2::Counters& B2Provenance::counters(){return state_->ledger.counters();}
const b2::Ledger& B2Provenance::ledger()const{return state_->ledger;}
bool B2Provenance::begin_collection(const B2InlineResult& r){return state_->begin_collection(r);}
void B2Provenance::record_measurement(std::shared_ptr<const text::ShapedRun> run,aether_node_s* owner,const void* style){state_->record_measurement(std::move(run),owner,style);}
void B2Provenance::work(B2Work w,size_t n){state_->work(w,n);}
void B2Provenance::preflight(const B2InlineResult& r,size_t t,size_t v){state_->preflight(r,t,v);}
void B2Provenance::inline_result(const B2InlineResult& r){
#ifdef AETHER_HTML_LTR_TESTING
 B2InlineResult inspected(state_->ledger.resource());inspected=r;testing::b2_result_inspect(inspected);state_->inline_result(inspected);
#else
 state_->inline_result(r);
#endif
}
void B2Provenance::b3_begin_result(aether_layout_result_s* r,uint64_t id){state_->begin_result(r,id);}
void B2Provenance::b3_box(aether_layout_result_s* r,aether_layout_box_s* box,uint64_t id,uint64_t gen,uint64_t parent){state_->box_begin(r,box,id,gen,parent);}
void B2Provenance::b3_complete(aether_layout_box_s* box){state_->complete(box);}
void B2Provenance::b3_replace(aether_layout_result_s* r,aether_layout_box_s* old){state_->replace(r,old);}
void B2Provenance::b3_offset(aether_layout_box_s* box,float dx,float dy){state_->offset(box,dx,dy);}
void B2Provenance::b3_discard(aether_layout_result_s* r,aether_layout_box_s* anchor,const B2InlineResult& input,std::span<aether_node_s* const> items){state_->discard(r,anchor,input,items);}
void B2Provenance::b3_width(aether_layout_result_s* r,aether_layout_box_s* box,aether_layout_box_s* containing,float reference,float used,B3WidthReason reason,bool percentage,bool definite){state_->width(r,box,containing,reference,used,reason,percentage,definite);}
void B2Provenance::validate_final(aether_layout_result_s* r){state_->validate_final(r);}
}
