#include <aether/aether.h>
#include <aether/aether_state_style.h>
#include <aether/aether_transition_sync.h>
#include <aether/aether_contextual_layout.h>
#include <aether/aether_context_owners_diagnostics.h>
#include <aether/aether_work_counters.h>
#include "../src/animations/transition_runtime.h"
#include "../src/css/computed_style_handle.h"
#include "../src/layout/layout_types.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <map>
#include <set>
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);std::abort();}} while(0)
static void ok(aether_status_t s){CHECK(s==AETHER_OK);}
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){return {0,16,12};}
struct Fixture {
 aether_doc_t doc{};aether_node_t node{};aether_style_runtime_t styles{};aether_transition_runtime_t transitions{};
 aether_style_revision_t revision{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t layout{};
 Fixture(){ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,"<html><body><div id='x'><div style='position:fixed;width:4px;height:4px'></div></div></body></html>"));node=aether_doc_get_element_by_id(doc,"x");
  aether_style_environment_t env{200,200,1,16,0,0,0};ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&styles));
  ok(aether_style_runtime_add_stylesheet(styles,"*{display:block}#x{width:16px;height:20px}",0,"context",nullptr,nullptr));
  ok(aether_transition_runtime_create(&transitions));ok(aether_layout_create(measure,nullptr,&engine));resolve();prepare(true);build();}
 ~Fixture(){aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_transition_runtime_destroy(transitions);aether_style_runtime_destroy(styles);aether_doc_destroy(doc);}
 void resolve(){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(styles,doc,nullptr,&tx));aether_style_map_t next{};
  aether_style_publication_delta_t info{sizeof(info)};ok(aether_style_transaction_publication_delta(tx,map,&info));
  if(info.compact)ok(aether_style_transaction_preview_compact_map(tx,map,&next));else ok(aether_style_transaction_preview_legacy_map(tx,&next));
  aether_style_revision_t rev{};ok(aether_style_runtime_commit_revision(styles,tx,&rev));aether_style_transaction_destroy(tx);
  aether_style_map_destroy(map);map=next;aether_style_revision_destroy(revision);revision=rev;}
 void style(const char* value){ok(aether_element_set_attribute(node,"style",value));resolve();}
 void prepare(bool expected){aether_bool_t changed=99;ok(aether_layout_prepare_transition_context(engine,transitions,map,layout,&changed));CHECK(bool(changed)==expected);}
 void build(){aether_layout_result_t next{};ok(aether_layout_layout(engine,doc,map,200,200,nullptr,nullptr,&next));aether_layout_result_destroy(layout);layout=next;oracle();}
 void oracle(){aether_bool_t equal=0;ok(aether_layout_context_validate_full(transitions,map,layout,&equal));CHECK(equal);}
 uint64_t serial(){uint64_t id{},size{};ok(aether_layout_context_snapshot_info(layout,&id,&size));return id;}
 size_t owners(){return layout->effective_context_owners->owners.size();}
};
static aether_computed_style_t transformed(aether_transform_operation& operation,float x,float delay=.2f){
 auto style=aether_computed_style_default();operation={};operation.kind=AETHER_TRANSFORM_TRANSLATE;operation.x={x,AETHER_CSS_UNIT_PX};operation.y={0,AETHER_CSS_UNIT_PX};
 style.transforms={&operation,1};style.transition_properties=AETHER_TRANSITION_PROPERTY_TRANSFORM;style.transition_duration=1;style.transition_delay=delay;return style;
}
static void cause_lifecycle(){
 Fixture f;CHECK(f.owners()==0);auto base=aether_computed_style_default();aether_transform_operation operation{},retarget_operation{};
 auto target=transformed(operation,20);ok(aether_transition_runtime_observe(f.transitions,f.node,&base,&target,0));
 f.prepare(true);f.build();CHECK(f.owners()==1);const auto active_serial=f.serial();
 aether_layout_result_t clone{};ok(aether_layout_clone_snapshot(f.layout,&clone));CHECK(clone->effective_context_owners==f.layout->effective_context_owners);
 CHECK(aether_transition_runtime_tick(f.transitions,100));f.prepare(false);CHECK(f.serial()==active_serial);f.oracle();
 CHECK(aether_transition_runtime_tick(f.transitions,400));f.prepare(false);f.oracle();
 auto retarget=transformed(retarget_operation,30);ok(aether_transition_runtime_observe(f.transitions,f.node,&target,&retarget,500));f.prepare(false);f.oracle();
 f.style("transform:translateX(20px);backdrop-filter:blur(2px)");f.prepare(false);CHECK(f.serial()==active_serial);f.oracle();
 f.style("backdrop-filter:blur(2px)");f.prepare(false);f.oracle();
 f.style("");f.prepare(false);f.oracle(); // Active cause alone preserves exact effective snapshot.
 CHECK(!aether_transition_runtime_tick(f.transitions,2000));f.prepare(true);f.build();CHECK(f.owners()==0);
 CHECK(clone->effective_context_owners->membership_serial==active_serial && clone->effective_context_owners->owners.size()==1);
 aether_layout_result_destroy(clone);
 // Retained finished endpoint is not active. Cancellation removes it too.
 f.prepare(false);auto cancel=base;ok(aether_transition_runtime_observe(f.transitions,f.node,&retarget,&cancel,2100));f.prepare(false);
 ok(aether_transition_runtime_observe(f.transitions,f.node,&base,&target,2200));f.prepare(true);f.build();
 ok(aether_transition_runtime_observe(f.transitions,f.node,&target,&cancel,2300));f.prepare(true);f.build();CHECK(!f.owners());
 // Opacity leaves transform membership and effective serial untouched.
 auto opacity=base;opacity.opacity=.5f;opacity.transition_duration=1;opacity.transition_properties=AETHER_TRANSITION_PROPERTY_OPACITY;
 const auto empty=f.serial();ok(aether_transition_runtime_observe(f.transitions,f.node,&base,&opacity,0));f.prepare(false);CHECK(f.serial()==empty);f.oracle();
 // Static causes retire independently.
 f.style("transform:translateX(0px);backdrop-filter:blur(1px)");f.prepare(true);f.build();auto both=f.serial();
 f.style("backdrop-filter:blur(1px)");f.prepare(false);CHECK(f.serial()==both);f.oracle();
 f.style("");f.prepare(true);f.build();CHECK(!f.owners());
 // Failed/invalid preparation must never change old frames.
 auto retained=f.layout->effective_context_owners;CHECK(aether_layout_prepare_transition_context(f.engine,f.transitions,nullptr,f.layout,nullptr)==AETHER_ERROR_NULL_ARG);CHECK(f.layout->effective_context_owners==retained);
}
// DOM transaction and D2 membership must use the identical sampled boundary.
static void rounded_transition_membership() {
 for(int mode=0;mode<2;++mode){
  Fixture f;auto before=aether_computed_style_default();aether_transform_operation operation{};
  auto after=transformed(operation,20,mode?-1000000.0625f:0.f);
  after.transition_duration=mode?1000000.0625f:1000000.0f;
  ok(aether_transition_runtime_observe(f.transitions,f.node,&before,&after,0));
  if(!mode){f.prepare(true);f.build();CHECK(f.owners()==1);
   aether_bool_t active=1;ok(aether_transition_runtime_try_tick(f.transitions,999999999,&active));CHECK(!active);}
  CHECK(!aether_transition_runtime_has_active(f.transitions));
  CHECK(aether_active_transform_owners(f.transitions).empty());
  CHECK(aether_active_transform_snapshot(f.transitions)->owners.empty());
  f.prepare(!mode);f.build();CHECK(f.owners()==0);f.oracle();
 }
}
static void compact_provenance_and_context(){
 Fixture f;auto old=f.map->current;auto old_style=aether_style_map_get(f.map,f.node);CHECK(old_style);
 auto tx=[&]{aether_style_transaction_t value{};ok(aether_style_runtime_prepare_revision(f.styles,f.doc,nullptr,&value));return value;}();
 aether_style_publication_delta_t info{sizeof(info)};ok(aether_style_transaction_publication_delta(tx,f.map,&info));CHECK(info.compact && info.semantic_count==0);
 // Arbitrary backing wrapper cannot masquerade as the exact base projection.
 aether::style::MapVersionInput input;input.backing=old->backing();input.document_lifetime=old->document_lifetime();input.document_identity=old->document_identity();input.structure_epoch=old->structure_epoch();
 aether_style_map_s arbitrary;ok(aether::style::make_map_version(input,arbitrary.current));
 ok(aether_style_transaction_publication_delta(tx,&arbitrary,&info));CHECK(!info.compact);
 aether_style_map_t out=reinterpret_cast<aether_style_map_t>(1);CHECK(aether_style_transaction_preview_compact_map(tx,&arbitrary,&out)==AETHER_ERROR_INVALID_STATE && !out);
 aether_style_map_t contextual{};ok(aether_style_revision_create_map(f.revision,&contextual));ok(aether_style_transaction_publication_delta(tx,contextual,&info));CHECK(!info.compact);aether_style_map_destroy(contextual);
 aether_style_transaction_destroy(tx);
 // Metadata-only native map changes but projection delta has no flat handle.
 ok(aether_element_set_attribute(f.node,"style","width:1rem"));ok(aether_style_runtime_prepare_revision(f.styles,f.doc,nullptr,&tx));
 ok(aether_style_transaction_publication_delta(tx,f.map,&info));CHECK(info.compact && info.semantic_count>=1 && info.visual_impact==0);
 bool metadata=false;for(int i=0;i<info.semantic_count;++i){aether_node_t n{};aether_computed_style_handle_t value{};uint32_t impact{};ok(aether_style_transaction_get_publication_delta(tx,f.map,i,&n,&value,&impact));if(n==f.node){CHECK(!value && !impact);metadata=true;}aether_computed_style_handle_destroy(value);}CHECK(metadata);
 ok(aether_style_transaction_preview_compact_map(tx,f.map,&out));auto new_style=aether_style_map_get(out,f.node);CHECK(new_style && new_style->record->payload_owner()!=old_style->record->payload_owner());CHECK(old_style->style.width.value==16);
 aether_style_transaction_destroy(tx);aether_computed_style_handle_destroy(new_style);aether_style_map_destroy(out);
 // Environment changes cannot reuse a legacy record from its old measure owner.
 aether_style_environment_t env{300,200,2,20,0,0,0};ok(aether_style_runtime_prepare_revision(f.styles,f.doc,&env,&tx));
 ok(aether_style_transaction_publication_delta(tx,f.map,&info));CHECK(!info.compact);
 ok(aether_style_transaction_preview_legacy_map(tx,&out));new_style=aether_style_map_get(out,f.node);CHECK(new_style->measures->document().values().viewport_width==300);CHECK(old_style->measures->document().values().viewport_width==200);
 CHECK(!aether::style::accepts_override(old.get(),out->current.get()) || !old->contextual());
 aether_style_transaction_destroy(tx);aether_computed_style_handle_destroy(new_style);aether_style_map_destroy(out);aether_computed_style_handle_destroy(old_style);
}
static void generated_staging_and_contextual() {
 Fixture f;ok(aether_style_runtime_add_stylesheet(f.styles,"#x::before{content:'generated';transform:translateX(3px)}",0,"generated",nullptr,nullptr));
 aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(f.styles,f.doc,nullptr,&tx));
 aether_style_map_t staged{};ok(aether_style_transaction_preview_legacy_map(tx,&staged));
 size_t generated=0;
 staged->current->for_each_node([&](const aether::style::BoundNodeView& view){const auto& p=view.payload_owner();
  if(!p->generated_leaf() || p->style_view().transforms().empty())return;
  CHECK(!p->node_lifetime()->attached);
  CHECK(staged->current->static_context_owners()->owners.find(reinterpret_cast<uintptr_t>(p->node_lifetime().get())));++generated;});CHECK(generated==1);
 aether_style_revision_t revision{};ok(aether_style_runtime_commit_revision(f.styles,tx,&revision));aether_style_transaction_destroy(tx);
 aether_style_map_destroy(f.map);f.map=staged;aether_style_revision_destroy(f.revision);f.revision=revision;f.prepare(true);f.build();CHECK(f.owners()==1);
 aether_style_map_t contextual{};ok(aether_style_revision_create_map(f.revision,&contextual));aether_bool_t changed=1;
 ok(aether_layout_prepare_transition_context(f.engine,f.transitions,contextual,f.layout,&changed));CHECK(!changed);
 aether_layout_result_t exact{};ok(aether_layout_layout(f.engine,f.doc,contextual,200,200,nullptr,nullptr,&exact));
#if defined(AETHERUI_ENABLE_CONTEXT_OWNERS)
 CHECK(exact->effective_context_owners==f.layout->effective_context_owners);
#else
 CHECK(exact->effective_context_owners->owners.size()==f.layout->effective_context_owners->owners.size());
#endif
 aether_layout_result_destroy(exact);aether_style_map_destroy(contextual);
}
static void destroyed_owner() {
 Fixture f;f.style("transform:translateX(20px)");f.prepare(true);f.build();
 auto* old_node=f.node;auto token=f.node->identity;auto frozen=f.layout->effective_context_owners;
 auto base=aether_computed_style_default();aether_transform_operation operation{};auto target=transformed(operation,30);
 ok(aether_transition_runtime_observe(f.transitions,f.node,&base,&target,0));
 aether_doc_destroy(f.doc);f.doc=nullptr;f.node=nullptr;CHECK(!token->alive);
 CHECK(f.layout->effective_fixed_container(old_node)); // Historical lookup must not dereference dead DOM.
 aether_transform_matrix ignored{};CHECK(!aether_transition_runtime_transform_override(f.transitions,old_node,&ignored));
 CHECK(!aether_transition_runtime_tick(f.transitions,100));
 aether_bool_t changed=0;ok(aether_layout_prepare_transition_context(f.engine,f.transitions,f.map,f.layout,&changed));CHECK(changed);
 CHECK(f.engine->prepared_context_owners->owners.empty());CHECK(frozen->owners.size()==1);
 aether_layout_result_t rejected=nullptr;CHECK(aether_layout_layout(f.engine,nullptr,f.map,200,200,nullptr,nullptr,&rejected)==AETHER_ERROR_NULL_ARG);
 CHECK(!f.engine->transition_context_prepared && !f.engine->prepared_context_owners);
}
static aether::style::FrozenNodeRef unbound_record(aether_node_t node,const aether_computed_style_t& style) {
 using namespace aether::style;
 NodeRecordInput input;input.node=node;ok(freeze_style(style,input.style));
 FrozenNodeRef record;ok(freeze_node_record(input,record));CHECK(!record->node_lifetime());return record;
}
static void install_legacy(Fixture& f,const aether::style::FrozenNodeRef& record) {
 using namespace aether::style;
 MapVersionInput input;const auto previous=f.map->current;
 input.document_lifetime=previous->document_lifetime();input.document_identity=previous->document_identity();
 input.structure_epoch=previous->structure_epoch();input.input_epoch=previous->input_epoch();input.style_tree_epoch=previous->style_tree_epoch();
 BackingPatchEntry patch{f.node,record};ok(patch_backing(previous->backing(),std::span(&patch,1),input.backing));
 ok(make_map_version(input,f.map->current));
}
static void unbound_legacy_fallback() {
 using namespace aether::style;
 Fixture f;auto base=aether_computed_style_default();aether_transform_operation operation{};auto target=transformed(operation,20);
 auto bound=f.map->current->backing()->find_owner(f.node);auto legacy=unbound_record(f.node,target);
 install_legacy(f,legacy);CHECK(!f.map->current->static_context_owners());
 // Full import as well as delta patch must reject partial owner coverage.
 BackingBuildInput imported;imported.entries.emplace(f.node,legacy);BackingRef unbound;
 ok(freeze_backing(imported,unbound));CHECK(!unbound->context_owners());
 // Repeated keys use the current staged root when maintaining unknown coverage.
 BackingPatchEntry rebound[]={{f.node,bound},{f.node,legacy},{f.node,bound}};BackingRef restored;
 ok(patch_backing(unbound,rebound,restored));CHECK(restored->context_owners());
 BackingPatchEntry removed{f.node,{}};ok(patch_backing(unbound,std::span(&removed,1),restored));
 CHECK(restored->context_owners() && restored->size()==0);
 f.prepare(true);f.build();CHECK(f.owners()==1 && f.layout->effective_fixed_container(f.node));
 aether_work_counters_reset();f.prepare(false);CHECK(!f.engine->context_cache.effective);
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
 aether_context_work_counters_v1_t counters{};ok(aether_context_work_counters_get_v1(&counters));
 CHECK(counters.backing_entries_visited>0 && counters.fallbacks==1);
#endif
 // Active-only legacy membership also uses the full helper. Finish excludes it.
 ok(aether_transition_runtime_observe(f.transitions,f.node,&base,&target,0));
 install_legacy(f,unbound_record(f.node,base));f.prepare(false);f.build();CHECK(f.owners()==1);
 CHECK(!aether_transition_runtime_tick(f.transitions,2000));f.prepare(true);f.build();CHECK(!f.owners());
 aether_backdrop_filter blur{};blur.kind=AETHER_BACKDROP_FILTER_BLUR;blur.length_amount={2,AETHER_CSS_UNIT_PX};
 auto backdrop=base;backdrop.backdrop_filters={&blur,1};install_legacy(f,unbound_record(f.node,backdrop));
 f.prepare(true);f.build();CHECK(f.owners()==1);
 aether_layout_result_t clone{};ok(aether_layout_clone_snapshot(f.layout,&clone));
 const auto snapshot=clone->effective_context_owners;auto* old_node=f.node;
 aether_doc_destroy(f.doc);f.doc=nullptr;f.node=nullptr;
 CHECK(clone->effective_fixed_container(old_node));CHECK(f.layout->effective_fixed_container(old_node));
 aether_bool_t changed{};ok(aether_layout_prepare_transition_context(f.engine,f.transitions,f.map,f.layout,&changed));
 CHECK(changed && f.engine->prepared_context_owners->owners.empty());
 CHECK(clone->effective_context_owners==snapshot && snapshot->owners.size()==1);
 aether_layout_result_destroy(clone);
}
static void persistent_diff(){
 using Index=aether::style::PersistentIndex<int>;Index index;std::map<uintptr_t,int> values;std::mt19937 rng(12345);
 for(int round=0;round<2000;++round){auto next=index;auto expected=values;for(int change=0;change<6;++change){auto key=uintptr_t(rng()%100);if(rng()%3){int value=rng()%20;next=next.set(key,value);expected[key]=value;}else{next=next.erase(key);expected.erase(key);}}
  auto actual=values;index.diff(next,[&](uintptr_t key,const int*,const int* after){if(after)actual[key]=*after;else actual.erase(key);});CHECK(actual==expected);index=std::move(next);values=std::move(expected);}
}
int main(){rounded_transition_membership();aether_work_counters_set_enabled(AETHER_TRUE);persistent_diff();cause_lifecycle();compact_provenance_and_context();generated_staging_and_contextual();destroyed_owner();unbound_legacy_fallback();std::printf("PASS D2 owners/compact/provenance %u checks\n",checks);return 0;}
