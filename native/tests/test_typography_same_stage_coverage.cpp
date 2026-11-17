#include "../src/style_runtime/style_runtime_core.cpp"
// C2 real text budget, discarded typography and contextual ownership regressions.
// No injected accounting or observer oracle; private core is included only
// for standalone linkage against static support without a runtime TU.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/dom/dom_types.h"
#include "../src/css/computed_style_handle.h"
#include "../src/css/style_map_types.h"
#include "../src/style_runtime/typography_context.h"
#include "../src/rendering/text/native_paragraph_diagnostic.h"
#include <cstdio>
#include <cstdlib>
#include "same_stage_assertions.h"
#include <string>
using namespace aether::style;
#define CHECK(x) SS_REQUIRE(x)
static void ok(aether_status_t s){CHECK(s==AETHER_OK);}
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};
 explicit Fixture(const std::string& html){ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,html.c_str()));ok(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&runtime));}
 ~Fixture(){aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}
 aether_style_revision_t resolve(){aether_style_transaction_t tx{};aether_style_revision_t r{};ok(aether_style_runtime_prepare_revision(runtime,doc,nullptr,&tx));ok(aether_style_runtime_commit_revision(runtime,tx,&r));aether_style_transaction_destroy(tx);return r;}
};
static FrozenNodeRef get(aether_style_revision_t r,aether_node_t target,uint32_t* bits=nullptr){
 for(int i=0;i<aether_style_revision_style_count(r);++i){aether_node_t n{};aether_computed_style_handle_t h{};uint32_t b{};ok(aether_style_revision_get_style(r,i,&n,&h,&b));auto v=h->record;aether_computed_style_handle_destroy(h);if(n==target){if(bits)*bits=b;return v;}}
 CHECK(false);return {};
}
static void exact_masks(const FrozenNodeRef& r,uint32_t bits,uint32_t expected){
 SS_CHECK("impact.entry",bits==expected);SS_CHECK("impact.own",r->declared_impact()==expected);SS_CHECK("impact.inherited",r->declared_inherited_impact()==expected);SS_CHECK("impact.inherited-change",r->inherited_change_impact()==expected);
}
static void late_text_budget(){
 using aether::rendering::text::ParagraphDiagnosticLimits;
 // Early contributes one byte. Late initially contributes exactly the remaining
 // capacity. A second text node moves the same tree from exact limit to +1.
 const auto limit=ParagraphDiagnosticLimits{}.source_bytes;
 Fixture f("<html><body><div id='early'>x</div><div id='late'>"+std::string(limit-1,'a')+"<span id='holder'>z</span></div></body></html>");
 auto early=f.node("early"),late=f.node("late"),holder=f.node("holder");
 auto extra=aether_node_get_child(holder,0);CHECK(extra);
 ok(aether_node_remove_child(holder,extra));
 auto initial=f.resolve();auto e0=get(initial,early);CHECK(e0->typography()->inputs->losses==0);
 // Append one text byte to late, after early was visited. All existing element
 // identities remain unchanged; the actual paragraph budget causes the loss.
 ok(aether_node_append_child(late,extra));
 auto over=f.resolve();
 for(auto n:{aether_doc_get_document_element(f.doc),aether_doc_get_body(f.doc),early,late,holder}){
  uint32_t bits{};auto r=get(over,n,&bits);exact_masks(r,bits,19);
  CHECK(r->typography()->prepared_evidence->coverage_gaps==1);
  SS_CHECK("F-gaps",r->typography()->evidence.coverage_gaps==5);
  SS_CHECK("final-budget-loss",r->typography()->inputs->losses==static_cast<uint32_t>(style_source_loss::budget));
  CHECK(!r->typography()->inputs->inputs_accounted);
 }
 CHECK(e0->typography()->evidence.coverage_gaps==1&&e0->typography()->inputs->losses==0);
 auto repeat=f.resolve();uint32_t bits{};auto er=get(repeat,early,&bits);exact_masks(er,bits,0);
 CHECK(er->typography()->evidence==get(over,early)->typography()->evidence);
 ok(aether_node_remove_child(late,extra));
 auto under=f.resolve();auto eu=get(under,early,&bits);exact_masks(eu,bits,19);
 CHECK(eu->typography()->evidence.coverage_gaps==1&&eu->typography()->inputs->losses==0);
 auto again=f.resolve();auto ea=get(again,early,&bits);exact_masks(ea,bits,0);
 // Restore ownership of detached text before document destruction.
 ok(aether_node_append_child(holder,extra));
 for(auto r:{initial,over,repeat,under,again})aether_style_revision_destroy(r);
}
static void discarded_and_context(){
 Fixture f("<html><body><div id='target'>x</div><div id='other'>y</div></body></html>");
 auto target=f.node("target");auto initial=f.resolve();
 aether_style_map_t old_map{};ok(aether_style_revision_create_map(initial,&old_map));
 auto old_version=pin_style_map(old_map);auto old_record=get(initial,target);
 auto old_context=TypographyContext::bind(old_version,old_record);CHECK(old_context.valid());
 ok(aether_element_set_attribute(target,"style","word-spacing:1px"));
 auto discarded=f.resolve();uint32_t bits{};auto record=get(discarded,target,&bits);
 CHECK((bits&19u)==19u);
 CHECK(record->typography()->prepared_evidence->coverage_gaps==3);
 // This real declaration also loses input coverage; do not mislabel F as 3.
 CHECK(record->typography()->inputs->losses!=0);
 CHECK(record->typography()->evidence.coverage_gaps==7);
 auto repeat=f.resolve();auto repeated=get(repeat,target,&bits);exact_masks(repeated,bits,0);
 CHECK(repeated->typography()->evidence==record->typography()->evidence);
 aether_style_map_t new_map{};ok(aether_style_revision_create_map(repeat,&new_map));
 auto new_version=pin_style_map(new_map);auto new_context=TypographyContext::bind(new_version,repeated);
 CHECK(new_context.valid());CHECK(old_context.valid());
 CHECK(compare_typography_context(old_context,new_context)==TypographyComparison::unproven);
 CHECK(!TypographyContext::bind(new_version,old_record).valid());
 CHECK(!typography_inputs_current(f.runtime,old_version));CHECK(typography_inputs_current(f.runtime,new_version));
 ok(aether_element_set_attribute(target,"style",""));auto restored=f.resolve();auto clean=get(restored,target,&bits);
 CHECK((bits&19u)==19u);CHECK(clean->typography()->prepared_evidence->coverage_gaps==1);
 CHECK(clean->typography()->evidence.coverage_gaps==1);
 CHECK(record->typography()->evidence.coverage_gaps==7);
 aether_style_map_destroy(old_map);aether_style_map_destroy(new_map);
 for(auto r:{initial,discarded,repeat,restored})aether_style_revision_destroy(r);
}
int main(){ss::run("coverage-text-limited",late_text_budget);ss::run("coverage-discard-context",discarded_and_context);return ss::finish();}
