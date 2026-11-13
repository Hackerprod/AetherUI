#ifndef AETHER_EFFECT_CAPTURE
#define AETHER_EFFECT_CAPTURE 1
#endif
#include "../src/style_runtime/style_runtime_core.cpp"
#include <cstdio>
#include <cstdlib>
static long fail_after=-1;
void* operator new(size_t n){if(fail_after>=0&&fail_after--==0)throw std::bad_alloc();if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);} void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);} void operator delete[](void* p,size_t) noexcept{std::free(p);}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d %s\n",__LINE__,#x);std::abort();}}while(0)
static void resolve(aether_style_runtime_t r,aether_doc_t d){aether_style_transaction_t t{};CHECK(aether_style_runtime_prepare_revision(r,d,nullptr,&t)==AETHER_OK);aether_style_revision_t rev{};CHECK(aether_style_runtime_commit_revision(r,t,&rev)==AETHER_OK);aether_style_transaction_destroy(t);aether_style_revision_destroy(rev);}
struct Listener{aether_style_runtime_t r{};aether_doc_t d{};size_t calls=0;bool reenter=false,entered=false,destroy=false;};
static void diagnostic(const char*,const char*,const char*,const char*,void* p){auto& l=*static_cast<Listener*>(p);++l.calls;if(l.reenter){l.reenter=false;l.entered=true;resolve(l.r,l.d);}if(l.destroy){l.destroy=false;aether_style_runtime_destroy(l.r);l.r=nullptr;}}
static std::string callback_trace;
static void trace(char c){callback_trace.push_back(c);}
static property_css_wide_e wide(const std::string& s){trace('w');return s=="inherit"?property_css_wide_e::inherit:property_css_wide_e::none;}
static void inherit_explicit(aether_computed_style_t*,ComputedStyleReadView){trace('i');}
static void callback_families(){
 style_property_registry_init();auto* registry=style_property_registry_get_global();std::vector<std::pair<size_t,style_property_definition_s>> saved;
 const char* names[]={"width","height","opacity","border-top-width","margin-left","margin-right","flex-grow"};
 for(int i=0;i<7;++i){auto* p=const_cast<style_property_definition_s*>(registry->try_get(names[i]));CHECK(p);saved.emplace_back(p->id,*p);
  p->parse=[](const char* in,std::string& out){trace('p');std::string copy=in;out=copy;return copy!="bad";};
  p->parse_failure=[](const char*){trace('f');return property_apply_result_s::failure("TEST","bad");};p->css_wide=wide;
  p->inherit_explicit=nullptr;p->inherit_context={};p->apply={};p->apply_checked={};p->apply_with_context={};p->apply_with_context_checked={};
  if(i==0||i==6)p->apply=[](aether_computed_style_t*,const std::string&){trace('a');};
  if(i==1)p->apply_checked=[](aether_computed_style_t*,const std::string&){trace('b');return property_apply_result_s{};};
  if(i==2)p->apply_with_context=[](property_apply_context_s&,const std::string&){trace('c');};
  if(i==3)p->apply_with_context_checked=[](property_apply_context_s&,const std::string&){trace('d');return property_apply_result_s{};};
  if(i==4)p->inherit_explicit=inherit_explicit;
  if(i==5)p->inherit_context=[](property_apply_context_s&,ComputedStyleReadView,const MeasuresReadView*){trace('j');};
 }
 auto* shorthand=const_cast<style_shorthand_definition_s*>(registry->try_get_shorthand("margin"));CHECK(shorthand);auto saved_shorthand=*shorthand;
 shorthand->expand=[](const char*,style_shorthand_writer_s& writer){trace('s');writer.set("margin-left","inherit");writer.set("margin-right","inherit");writer.set("margin-top","inherit");writer.set("margin-bottom","inherit");return true;};
 Listener l;CHECK(aether_doc_create(&l.d)==AETHER_OK);
 CHECK(aether_doc_parse_html(l.d,"<html><body><div style='--v:ok;--bad:bad;width:var(--v);height:ok;opacity:ok;border-top-width:ok;margin:inherit;flex-grow:var(--bad)'></div></body></html>")==AETHER_OK);
 CHECK(aether_style_runtime_create(nullptr,nullptr,diagnostic,&l,&l.r)==AETHER_OK);
 style_value_audit_s audit; audit.scope=[](void*,const FrozenCustomPropertyScope*){trace('q');};audit.value=[](void*,aether_node_t,const std::string&,const std::string&,const std::string*){trace('v');};style_runtime_set_value_audit(l.r,&audit);
 callback_trace.clear();resolve(l.r,l.d);
 for(char c:std::string("pwfabcdijsqv")){if(callback_trace.find(c)==std::string::npos)std::fprintf(stderr,"missing %c trace=%s\n",c,callback_trace.c_str());CHECK(callback_trace.find(c)!=std::string::npos);}
 std::printf("CALLBACK_TRACE %s\n",callback_trace.c_str());
 aether_style_runtime_destroy(l.r);aether_doc_destroy(l.d);
 for(auto& e:saved)registry->definitions[e.first]=std::move(e.second);*shorthand=std::move(saved_shorthand);
}
#if AETHER_EFFECT_CAPTURE
static void owned_diagnostics(){
 Listener l;
 std::shared_ptr<const CapturedDiagnostics> retained;
 {stylesheet_diagnostics_s batch(diagnostic,&l);
  std::string code="owned code";stylesheet_diagnostics_s::collect(code.c_str(),nullptr,"","same",&batch);
  stylesheet_diagnostics_s::collect(code.c_str(),nullptr,"","same",&batch);code="overwritten";
  batch.freeze();retained=batch.frozen;CHECK(retained&&retained->size()==2);batch.replay();CHECK(l.calls==2);
 }
 CHECK(!(*retained)[0].property&&(*retained)[0].value&&(*retained)[0].value->empty());
 CHECK(*(*retained)[0].code=="owned code"&&*(*retained)[1].message=="same");
 stylesheet_diagnostics_s absent(nullptr,nullptr);CHECK(!absent.collector());absent.freeze();CHECK(!absent.frozen);
}
static auto budget_fields(const style_source_budget_s& b){return std::array<size_t,12>{b.source_bytes,b.declarations,b.rules,b.compiled_rules,b.expanded_declarations,b.compiled_bytes,b.source_bytes_limit,b.declarations_limit,b.rules_limit,b.compiled_rules_limit,b.expanded_declarations_limit,b.compiled_bytes_limit};}
static void premerge_saturation(){
 // Private synthetic candidate: saturate reporting aggregate independently of
 // operational budget to prove capture never subtracts clamped aggregates.
 aether_doc_t d{};aether_style_runtime_t r{};CHECK(aether_doc_create(&d)==AETHER_OK);
 CHECK(aether_doc_parse_html(d,"<div id='x' style='color:red;unknown-property:1'></div>")==AETHER_OK);
 CHECK(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&r)==AETHER_OK);
 r->effect_builder=std::make_unique<CapturedEffects>();r->document_measures=std::make_shared<document_measure_context_s>();
 r->document_measures->initial_font_size=16;r->document_measures->root_font_size=16;r->document_measures->identity=aether_node_identity(d);
 r->source_accounting.source_bytes=style_source_accounting_s::max_source_bytes;r->source_accounting.declarations=style_source_accounting_s::max_declarations;
 r->source_budget.source_bytes_limit=999;r->source_budget.declarations_limit=111;r->source_budget.rules_limit=222;r->source_budget.compiled_rules_limit=333;r->source_budget.expanded_declarations_limit=444;r->source_budget.compiled_bytes_limit=99999;
 auto before=r->source_budget;selector_match_context_s selector;uint32_t impact{},inherited{};
 compute_node(r,aether_doc_get_element_by_id(d,"x"),nullptr,0,impact,inherited,selector);
 auto& e=r->effect_builder->events.at(0);CHECK(e.inline_present&&e.completed&&e.inline_local.source_bytes>0&&e.inline_local.declarations==2);
 CHECK(e.accounting_before.source_bytes==e.accounting_after.source_bytes&&e.accounting_before.declarations==e.accounting_after.declarations);
 CHECK(e.accounting_after.losses&static_cast<uint32_t>(style_source_loss::budget));CHECK(!e.accounting_after.inputs_accounted);
 CHECK(budget_fields(e.budget_before)==budget_fields(before));CHECK(budget_fields(e.inline_budget_before)==budget_fields(before));
 CHECK(budget_fields(e.budget_after)==budget_fields(r->source_budget));CHECK(budget_fields(e.inline_budget_after)==budget_fields(r->source_budget));
 aether_style_runtime_destroy(r);aether_doc_destroy(d);
}
static void text_limit_boundary(){
 const size_t limit=aether::rendering::text::ParagraphDiagnosticLimits{}.source_bytes;
 for(size_t bytes:{limit,limit+1}) {
  aether_doc_t d{};aether_style_runtime_t r{};CHECK(aether_doc_create(&d)==AETHER_OK);
  auto html=std::string("<html><body><div>")+std::string(bytes,'x')+"</div></body></html>";
  CHECK(aether_doc_parse_html(d,html.c_str())==AETHER_OK);CHECK(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&r)==AETHER_OK);
  resolve(r,d);size_t events=0;for(const auto& e:r->effects->events)if(e.kind==CapturedEffectKind::text_limited){++events;CHECK(e.completed);CHECK(e.accounting_after.losses&static_cast<uint32_t>(style_source_loss::budget));CHECK(!e.accounting_after.inputs_accounted);}
  CHECK(events==(bytes>limit?1:0));aether_style_runtime_destroy(r);aether_doc_destroy(d);
 }
}
#endif
int main(){
 callback_families();
#if !AETHER_EFFECT_CAPTURE
 std::puts("OFF compile: capture storage absent (runtime core compiles)");return 0;
#else
 owned_diagnostics();premerge_saturation();text_limit_boundary();Listener l;
 CHECK(aether_doc_create(&l.d)==AETHER_OK);
 CHECK(aether_doc_parse_html(l.d,"<html><body><link><style></style><div id='x' style='color:red;unknown-property:1; width:calc(1px +)'>text</div><div id='hidden' style='display:none'></div></body></html>")==AETHER_OK);
 auto x=aether_doc_get_element_by_id(l.d,"x");auto identity=x->identity;
 CHECK(aether_style_runtime_create(nullptr,nullptr,diagnostic,&l,&l.r)==AETHER_OK);
 CHECK(aether_style_runtime_add_stylesheet(l.r,"#x::before{width:var(--missing)}#x::after{content:'kept';color:blue}#hidden::before{content:none;width:var(--missing)}",2,"effects",nullptr,nullptr)==AETHER_OK);
 resolve(l.r,l.d);CHECK(l.calls>0);auto retained=l.r->effects;CHECK(retained&&retained->capture_finished&&!retained->effects_complete);CHECK(retained->diagnostics&&!retained->diagnostics->empty());
 CHECK(retained->published_generation==l.r->generation&&retained->source_generation+1==retained->published_generation);
 CHECK(retained->document==l.d->identity&&retained->root==l.d->identity);CHECK(retained->input_epoch==l.d->identity->mutation_epoch&&retained->sheet_epoch==l.r->sheet_epoch&&retained->structure_epoch==l.d->identity->structure_epoch&&retained->style_tree_epoch==l.d->identity->style_tree_epoch);
 size_t compute=0,before=0,after=0,links=0,styles=0,sheets=0;bool inline_found=false,hidden=false;
 for(size_t i=0;i<retained->events.size();++i){auto& e=retained->events[i];CHECK(e.sequence==i&&e.completed);CHECK(e.unknown_callbacks==all_callback_families_unknown);CHECK(e.diagnostic_end>=e.diagnostic_begin);CHECK(e.diagnostic_end<=retained->diagnostics->size());
  if(e.kind==CapturedEffectKind::compute_attempt){++compute;CHECK(e.host);if(e.host==identity){if(e.pseudo==aether_pseudo_element_t::before){++before;CHECK(!e.generated_content_present);}if(e.pseudo==aether_pseudo_element_t::after){++after;CHECK(e.generated_content_present);}if(e.inline_present){inline_found=true;CHECK(e.inline_local.source_bytes>0&&e.inline_local.declarations>0);CHECK(e.inline_budget_after.source_bytes>=e.inline_budget_before.source_bytes);CHECK(e.inline_local.losses!=0);}}if(e.host==aether_doc_get_element_by_id(l.d,"hidden")->identity)hidden=true;}
  if(e.kind==CapturedEffectKind::link)++links;if(e.kind==CapturedEffectKind::style)++styles;if(e.kind==CapturedEffectKind::sheet_aggregate)++sheets;
 }
 CHECK(before==1&&after==1&&links==1&&styles==1&&sheets==1&&inline_found&&hidden);
 CHECK(compute>l.r->states.size());CHECK(x->generated_storage.size()==1);CHECK(x->generated_storage[0]->pseudo_element==aether_pseudo_element_t::after);
 auto oldgen=l.r->generation;auto calls=l.calls;int failed=0;bool reached=false;
 for(int i=0;i<12000;++i){aether_style_transaction_t t{};fail_after=i;auto status=aether_style_runtime_prepare_revision(l.r,l.d,nullptr,&t);fail_after=-1;CHECK(l.r->generation==oldgen&&l.r->effects==retained&&l.calls==calls);
  if(status==AETHER_OK){CHECK(t);aether_style_transaction_destroy(t);reached=true;break;}CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!t);++failed;
 }CHECK(reached&&failed>20);
 l.reenter=true;resolve(l.r,l.d);CHECK(l.entered&&l.r->generation==oldgen+2);CHECK(retained->published_generation==oldgen&&l.r->effects->published_generation==oldgen+2);CHECK(retained->diagnostics&&!retained->diagnostics->empty());
 aether_style_runtime_set_diagnostics_callback(l.r,nullptr,nullptr);resolve(l.r,l.d);CHECK(!l.r->effects->diagnostics);for(auto& e:l.r->effects->events)CHECK(!e.diagnostics_captured&&e.diagnostic_begin==0&&e.diagnostic_end==0);
 aether_style_runtime_set_diagnostics_callback(l.r,diagnostic,&l);resolve(l.r,l.d);CHECK(l.r->effects->diagnostics&&!l.r->effects->diagnostics->empty());
 // Replay may destroy the runtime; transaction-owned immutable log survives.
 l.destroy=true;resolve(l.r,l.d);CHECK(!l.r);aether_doc_destroy(l.d);CHECK(!identity->alive);CHECK(retained->events.size()>compute&&retained->diagnostics->size()>0);
 std::printf("PASS effects capture ownership/pseudo discard/inline/global/null callback/reentry/destroy; ordinary OOM points=%d\n",failed);
#endif
}
