// Narrow runtime coverage derived from the C3 hooks LH projection scenario.
// No break-policy CSS, flex extensions, renderer or documentary consumer.
#define AETHER_SAME_STAGE_TEST_HOOKS 1
#include "../src/style_runtime/style_runtime_core.cpp"
#include "frozen_measure_test_support.h"
#include <cstdio>
#include <cstdlib>
using namespace aether::style;
static void need(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL minimal: %s\n",m);std::exit(1);}}
static void ok(aether_status_t s){need(s==AETHER_OK,"status");}
struct F {
 aether_doc_t doc{};aether_style_runtime_t rt{};aether_style_environment_t env{800,600,1,16,0,0,0};
 SameStageTestHooks hook{};int fault=0;
 F(){ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,"<html><body><div id='x'></div><div id='other'></div></body></html>"));ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt));set("font-size:16px;line-height:2");ok(aether_element_set_attribute(node("other"),"style","font-size:16px;line-height:2"));hook.context=this;hook.accounting=[](void* p,style_source_accounting_s& a){auto& f=*static_cast<F*>(p);if(f.fault==1)a.loss(style_source_loss::unknown_declaration);if(f.fault==2)a.inputs_accounted=false;if(f.fault==3)a.external_pending=true;if(f.fault==4)a.attribution.unattributed=1;};rt->same_stage_test_hooks=&hook;resolve();}
 ~F(){aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
 aether_node_t node(const char* id="x"){return aether_doc_get_element_by_id(doc,id);}
 void set(const char*s){ok(aether_element_set_attribute(node(),"style",s));}
 void sheet(const char*s){int64_t h{};ok(aether_style_runtime_add_stylesheet(rt,s,1,"minimal",&h,nullptr));}
 aether_style_transaction_t prepare(){aether_style_transaction_t t{};ok(aether_style_runtime_prepare_revision(rt,aether_doc_get_document_element(doc),&env,&t));return t;}
 void commit(aether_style_transaction_t t){aether_style_revision_t r{};ok(aether_style_runtime_commit_revision(rt,t,&r));aether_style_revision_destroy(r);aether_style_transaction_destroy(t);}
 void resolve(){commit(prepare());}
 void expect(unsigned wanted){auto t=prepare();bool found=false;for(const auto& e:t->revision->entries())if(e.node()==node()){found=true;const auto&r=e.record();need(e.impact()==wanted,"published impact");need(r->declared_impact()==wanted,"declared own");need(r->declared_inherited_impact()==wanted,"declared inherited");need(r->inherited_change_impact()==wanted,"inherited changed");}need(found,"target present");commit(t);}
};
static FrozenNodeRef replace(const FrozenNodeRef& original,FrozenMeasuresRef measures,TypographyRecordRef typography){NodeRecordInput in;in.origin=original->origin();in.node=original->node();in.style=original->style();in.typography=typography;in.measures=measures;in.custom_properties=original->custom_properties();in.color_references=original->color_references();in.node_identity=original->node_identity();in.node_lifetime=original->node_lifetime();in.document_identity=original->document_identity();in.document_lifetime=original->document_lifetime();FrozenNodeRef result;ok(freeze_node_record(in,result));return result;}
static void channels(){F f;f.expect(0);f.set("font-size:16px;line-height:32px");f.expect(81);f.expect(0);f.set("font-size:16px;line-height:32.0px");f.expect(0);f.set("font-size:16px;line-height:2");f.expect(81);f.expect(0);
 auto original=f.rt->states.at(f.node());auto altered=frozen_measure_test::snapshot(*original->measures());need(measure_property_dependencies(altered,"line-height")==0,"initial zero LH flags");measure_set_property_dependencies(altered,"line-height",MEASURE_SELF_FONT);measure_recompute_dependencies(altered);FrozenMeasuresRef frozen;ok(freeze_runtime_measures(altered,original->measures()->document_owner(),frozen));f.rt->states[f.node()]=replace(original,frozen,original->typography());f.expect(19);f.expect(0);need(original->measures()->dependency("line-height")==0,"retained flags unchanged");
 auto missing=std::make_shared<TypographyRecord>(*f.rt->states.at(f.node())->typography());missing->prepared_evidence.reset();original=f.rt->states.at(f.node());f.rt->states[f.node()]=replace(original,original->measures(),missing);f.expect(19);f.expect(0);
 f.rt->states[f.node()]=f.rt->states.at(f.node("other"));f.expect(19);f.expect(0);
 for(int fault:{1,2,3,4}){f.fault=fault;f.expect(19);f.expect(0);f.fault=0;f.expect(19);f.expect(0);}
 std::puts("PASS runtime four channels: LH81/equivalent0/projection19/repeat0/missing proof/identity/accounting");}
static void accounting(){
 struct C{const char* css;int certified;bool direct;};
 for(const auto c:{C{"#x{width:bad}",true,true},C{"#x{width:var(--absent)}",-1,false},C{"#x{height:bad}",false,false},C{"#x{width:bad;height:bad}",false,true},C{"@media(min-width:10px){#x{color:red}}",false,false},C{"@supports(display:block){#x{color:red}}",false,false}}){F f;f.sheet(c.css);f.resolve();const auto a=f.rt->states.at(f.node())->typography()->inputs;need(a!=nullptr,"accounting retained");if(c.certified>=0)need(a->visual_losses_certified()==bool(c.certified),"selective certification");need((a->attribution.direct_width_rejections>0)==c.direct,"direct width attribution");if(c.certified==0)need(a->losses||!a->inputs_accounted,"actual conservative loss");need(bool(f.rt->states.at(f.node())->typography()->evidence.coverage_gaps&4)==bool(a->losses||!a->inputs_accounted),"genuine final gap");f.expect(0);}
 {F f;f.sheet("#x{width:bad}");f.sheet("#x{height:bad}");f.resolve();auto a=f.rt->states.at(f.node())->typography()->inputs;need(a->attribution.direct_width_rejections>0&&!a->visual_losses_certified(),"mixed sheets conservative");f.expect(0);}
 std::puts("PASS accounting selective/direct/var/other/mixed/media/supports/mixed sheets");}
int main(){channels();accounting();}
