#include <aether/aether.h>
#include <aether/aether_state_style.h>
#include <aether/aether_contextual_layout.h>
#include <aether/aether_context_owners_diagnostics.h>
#include "css/computed_style_handle.h"
#include "animations/transition_runtime.h"
#include "layout/layout_types.h"
#include <cstdio>
#include <cstdlib>
static void ok(aether_status_t value){if(value!=AETHER_OK){std::fprintf(stderr,"status=%d\n",value);std::exit(2);}}
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){return {0,16,12};}
int main(){
 aether_doc_t doc{};ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,"<html><body><div id='x'></div></body></html>"));
 aether_style_environment_t env{200,200,1,16,0,0,0};aether_style_runtime_t styles{};ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&styles));
 ok(aether_style_runtime_add_stylesheet(styles,"#x::before{content:'generated';transform:translateX(3px)}",0,"repro",nullptr,nullptr));
 aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(styles,doc,nullptr,&tx));
 aether_style_map_t map{};ok(aether_style_transaction_preview_legacy_map(tx,&map));
 const auto staged=map->current->backing()->context_owners()->owners.size();
 aether_style_revision_t revision{};ok(aether_style_runtime_commit_revision(styles,tx,&revision));
 aether_transition_runtime_t transitions{};ok(aether_transition_runtime_create(&transitions));aether_layout_engine_t engine{};ok(aether_layout_create(measure,nullptr,&engine));
 aether_bool_t changed{},equal{};ok(aether_layout_prepare_transition_context(engine,transitions,map,nullptr,&changed));
 aether_layout_result_t layout{};ok(aether_layout_layout(engine,doc,map,200,200,nullptr,nullptr,&layout));
 ok(aether_layout_context_validate_full(transitions,map,layout,&equal));
 aether_node_t generated{};
 for(const auto& entry:map->current->backing()->entries()) if(entry.second->generated_leaf()) generated=entry.second->generated_leaf().get();
 if(!generated) return 3;
 auto baseline=aether_computed_style_default(),target=baseline;
 aether_transform_operation operation{};operation.kind=AETHER_TRANSFORM_TRANSLATE;operation.x={20,AETHER_CSS_UNIT_PX};operation.y={0,AETHER_CSS_UNIT_PX};
 target.transforms={&operation,1};target.transition_properties=AETHER_TRANSITION_PROPERTY_TRANSFORM;target.transition_duration=1;
 ok(aether_transition_runtime_observe(transitions,generated,&baseline,&target,0));
 ok(aether_layout_prepare_transition_context(engine,transitions,map,layout,&changed));
 aether_layout_result_t active{};ok(aether_layout_layout(engine,doc,map,200,200,nullptr,nullptr,&active));aether_layout_result_destroy(layout);layout=active;
 aether_transition_runtime_tick(transitions,2000);
 ok(aether_layout_prepare_transition_context(engine,transitions,map,layout,&changed));
 aether_layout_result_t finished{};ok(aether_layout_layout(engine,doc,map,200,200,nullptr,nullptr,&finished));aether_layout_result_destroy(layout);layout=finished;
 ok(aether_layout_context_validate_full(transitions,map,layout,&equal));
 std::printf("generated staged static owners=%zu expected=1; after transform transition finish FULL oracle equal=%d expected=1\n",staged,equal);
 aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_transition_runtime_destroy(transitions);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_transaction_destroy(tx);aether_style_runtime_destroy(styles);aether_doc_destroy(doc);
 return staged==1 && equal ? 0 : 1;
}
