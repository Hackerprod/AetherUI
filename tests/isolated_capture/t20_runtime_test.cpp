#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/css/measure_state.h"
#include "../../native/src/dom/dom_types.h"
#include "../../native/src/css/style_map_types.h"
#include "../../native/src/css/style_revision.h"
#include <aether/aether_contextual_style.h>
#include "../../native/src/style_runtime/typography_context.h"
#include "../../native/src/style_runtime/style_source_accounting.h"
#include "../../native/src/style_runtime/style_runtime_types.h"
#include <thread>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
using namespace aether::style;
static long fail_after=-1;
[[gnu::noinline]] void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void operator delete(void* p) noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void* p,std::size_t) noexcept{std::free(p);}
static unsigned checks=0;
static void checked(bool ok,int line){if(!ok){std::cerr<<"T2.0 failure line="<<line<<" check="<<checks+1<<'\n';std::abort();}++checks;}
#define ck(x) checked((x),__LINE__)
struct Snapshot {
 aether_style_revision_t revision{};aether_style_map_t map{};
 Snapshot()=default;Snapshot(const Snapshot&)=delete;
 Snapshot(Snapshot&& o):revision(o.revision),map(o.map){o.revision=nullptr;o.map=nullptr;}
 ~Snapshot(){aether_style_map_destroy(map);aether_style_revision_destroy(revision);}
 const FrozenNodeRecord* node(aether_node_t n)const{auto* r=map->current->backing()->find(n);ck(r&&r->typography());return r;}
 const TypographyEvidence& evidence(aether_node_t n)const{return node(n)->typography()->evidence;}
};
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};
 aether_style_environment_t env{400,300,1,16,0,0,0};
 Fixture(const std::string& body,const aether_style_runtime_options_t* options=nullptr){ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,body.c_str())==AETHER_OK);ck(aether_style_runtime_create(options,&env,nullptr,nullptr,&runtime)==AETHER_OK);}
 ~Fixture(){aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 aether_node_t id(const char* id){auto* n=aether_doc_get_element_by_id(doc,id);ck(n);return n;}
 int64_t sheet(const std::string& css){int64_t handle{};int32_t rules{};ck(aether_style_runtime_add_stylesheet(runtime,css.c_str(),2,"T18 fixture",&handle,&rules)==AETHER_OK);return handle;}
 Snapshot resolve(){aether_style_transaction_t tx{};Snapshot s;ck(aether_style_runtime_prepare_revision(runtime,doc,&env,&tx)==AETHER_OK);ck(aether_style_runtime_commit_revision(runtime,tx,&s.revision)==AETHER_OK);aether_style_transaction_destroy(tx);ck(aether_style_revision_create_map(s.revision,&s.map)==AETHER_OK);return s;}
 void style(aether_node_t n,const char* css){ck(aether_element_set_attribute(n,"style",css)==AETHER_OK);}
};
static const style_source_accounting_s& inputs(const Snapshot& s,aether_node_t n){auto& r=*s.node(n)->typography();ck(r.context_unproven&&r.font_unresolved);ck(bool(r.inputs));ck(!r.evidence.coverage_complete);return *r.inputs;}
static std::string expansion_css(size_t selectors,size_t declarations,size_t payload=1){
 std::string s;for(size_t i=0;i<selectors;++i){if(i)s+=",";s+=".x";}s+="{";for(size_t i=0;i<declarations;++i)s+="--x:"+std::string(payload,'a')+";";return s+"}";
}
static void expansion_tests(){
 size_t product=7;ck(!style_source_expansion_product(static_cast<size_t>(-1),2,product));ck(product==7);ck(style_source_expansion_product(static_cast<size_t>(-1),1,product));ck(product==static_cast<size_t>(-1));
 Fixture f("<body><div id='x'>X</div></body>");std::vector<compiled_rule_s> out;int32_t id=1;style_source_budget_s b;style_source_work_s work;
 auto overflow=expansion_css(257,257);ck(style_source_compile(overflow.c_str(),1,2,out,id,nullptr,nullptr,nullptr,&b,&work)==AETHER_ERROR_INVALID_STATE);ck(out.empty()&&id==1);ck(b.expanded_declarations==0&&b.compiled_rules==0&&b.compiled_bytes==0);ck(work.work_expanded_declaration_copies==0);
 auto exact=expansion_css(256,256);ck(style_source_compile(exact.c_str(),1,2,out,id,nullptr,nullptr,nullptr,&b,&work)==AETHER_OK);ck(out.size()==256&&b.expanded_declarations==65536&&work.work_expanded_declaration_copies==65536);
 const auto oldid=id;style_source_work_s extra;ck(style_source_compile(".x{--x:a}",2,2,out,id,nullptr,nullptr,nullptr,&b,&extra)==AETHER_ERROR_INVALID_STATE);ck(id==oldid&&out.size()==256&&extra.work_expanded_declaration_copies==0);
 style_source_budget_s rules;rules.rules_limit=256;style_source_work_s rw;ck(style_source_compile(expansion_css(257,1).c_str(),2,2,out,id,nullptr,nullptr,nullptr,&rules,&rw)==AETHER_ERROR_INVALID_STATE);ck(rw.work_expanded_declaration_copies==0);
 style_source_budget_s bytes;bytes.compiled_bytes_limit=1;style_source_work_s bw;ck(style_source_compile(".x{--x:a}",3,2,out,id,nullptr,nullptr,nullptr,&bytes,&bw)==AETHER_ERROR_INVALID_STATE);ck(bytes.compiled_bytes==0&&bw.work_expanded_declaration_copies==0);
 style_source_budget_s shorthand_budget;shorthand_budget.expanded_declarations_limit=7;style_source_work_s sw;ck(style_source_compile(".x,.y{margin:1px}",3,2,out,id,nullptr,nullptr,nullptr,&shorthand_budget,&sw)==AETHER_ERROR_INVALID_STATE);ck(sw.work_expanded_declaration_copies==0);
 style_source_budget_s measured;std::vector<compiled_rule_s> one;int32_t one_id=1;ck(style_source_compile(".x{--x:a}",4,2,one,one_id,nullptr,nullptr,nullptr,&measured)==AETHER_OK);style_source_budget_s boundary;boundary.compiled_bytes_limit=measured.compiled_bytes;one.clear();ck(style_source_compile(".x{--x:a}",4,2,one,one_id,nullptr,nullptr,nullptr,&boundary)==AETHER_OK);
 auto first=f.sheet(exact);auto old=f.resolve();int64_t rejected=-1;ck(aether_style_runtime_add_stylesheet(f.runtime,".x{--x:a}",2,"limit",&rejected,nullptr)==AETHER_ERROR_INVALID_STATE);ck(rejected==-1&&typography_inputs_current(f.runtime,old.map->current));
 f.style(f.id("x"),"--inline:a");aether_style_transaction_t tx{};ck(aether_style_runtime_prepare_revision(f.runtime,f.doc,&f.env,&tx)==AETHER_ERROR_INVALID_STATE);ck(!tx);ck(aether_style_runtime_remove_stylesheet(f.runtime,first));auto recovered=f.resolve();ck(inputs(recovered,f.id("x")).expanded_declarations==1);
 {Fixture two("<body><div id='a'>A</div><div id='b'>B</div></body>");auto before=two.resolve();std::string css;for(size_t i=0;i<8193;++i)css+="margin:1px;";two.style(two.id("a"),css.c_str());two.style(two.id("b"),css.c_str());aether_style_transaction_t pending{};ck(aether_style_runtime_prepare_revision(two.runtime,two.doc,&two.env,&pending)==AETHER_ERROR_INVALID_STATE);ck(!pending);aether_style_revision_t r{};ck(aether_style_runtime_retain_revision(two.runtime,&r)==AETHER_OK);ck(aether_style_revision_generation(r)==aether_style_revision_generation(before.revision));aether_style_revision_destroy(r);}
 bool done=false;unsigned oom=0;for(long n=0;n<1000&&!done;++n){std::vector<compiled_rule_s> result;style_source_budget_s budget;int32_t rid=7;fail_after=n;try{auto status=style_source_compile(".a,.b{--x:a;color:red}",1,2,result,rid,nullptr,nullptr,nullptr,&budget);fail_after=-1;ck(status==AETHER_OK);done=true;}catch(const std::bad_alloc&){fail_after=-1;++oom;ck(result.empty()&&rid==7);ck(!budget.compiled_rules&&!budget.expanded_declarations&&!budget.compiled_bytes);}}
 ck(done&&oom);std::cout<<"T2.0 expansion PASS copies_rejected="<<extra.work_expanded_declaration_copies<<" copies_boundary="<<work.work_expanded_declaration_copies<<" OOM="<<oom<<'\n';
}
static void compiler_budget_tests(){
 Fixture setup("<body><div>X</div></body>");
 std::string css="a{";for(size_t i=0;i<65537;++i)css+="--x:a;";css+="}";
 std::vector<compiled_rule_s> rules;int32_t next=7;style_source_accounting_s a;a.source_bytes=123;
 style_source_budget_s b;style_source_work_s w;
 ck(style_source_compile(css.c_str(),1,2,rules,next,nullptr,nullptr,&a,&b,&w)==AETHER_ERROR_INVALID_STATE);
 ck(rules.empty()&&next==7&&a.source_bytes==123);ck(b.source_bytes==0&&b.declarations==0&&b.rules==0);ck(w.work_declaration_spans==65536);
 std::vector<compiled_property_declaration_s> props;std::vector<custom_property_declaration_s> custom;std::vector<compiled_shorthand_declaration_s> shorthand;
 style_source_budget_s ib;style_source_work_s iw;const auto inline_over=css.substr(2,css.size()-3);
 ck(style_source_compile_inline(inline_over.c_str(),props,custom,shorthand,nullptr,nullptr,nullptr,nullptr,&ib,&iw)==AETHER_ERROR_INVALID_STATE);ck(props.empty()&&custom.empty()&&shorthand.empty());ck(ib.source_bytes==0&&ib.declarations==0);ck(iw.work_declaration_spans==65536);
 css.erase(css.size()-7,6); // exact 65,536 declarations
 ck(style_source_compile(css.c_str(),1,2,rules,next,nullptr,nullptr,&a,&b)==AETHER_OK);ck(rules.size()==1&&rules[0].custom_declarations.size()==65536);ck(b.declarations==65536);
 auto saved=rules.size();auto old_next=next;style_source_work_s over;
 ck(style_source_compile("b{--y:a}",2,2,rules,next,nullptr,nullptr,&a,&b,&over)==AETHER_ERROR_INVALID_STATE);ck(rules.size()==saved&&next==old_next);ck(over.work_declaration_spans==0);ck(b.declarations==65536);
 style_source_budget_s bytes;bytes.source_bytes_limit=4;style_source_work_s no_copy;
 ck(style_source_compile("a { }",3,2,rules,next,nullptr,nullptr,nullptr,&bytes,&no_copy)==AETHER_ERROR_INVALID_STATE);ck(no_copy.work_source_bytes_copied==0&&no_copy.work_rule_starts==0&&no_copy.work_declaration_spans==0);ck(bytes.source_bytes==0);
 ck(style_source_compile("a{} ",3,2,rules,next,nullptr,nullptr,nullptr,&bytes)==AETHER_OK);ck(bytes.source_bytes==4);
 style_source_work_s no_copy2;ck(style_source_compile("b{}",4,2,rules,next,nullptr,nullptr,nullptr,&bytes,&no_copy2)==AETHER_ERROR_INVALID_STATE);ck(no_copy2.work_source_bytes_copied==0);
 style_source_budget_s rule_budget;rule_budget.rules_limit=1;style_source_work_s rw;
 ck(style_source_compile("a{}b{}",5,2,rules,next,nullptr,nullptr,nullptr,&rule_budget,&rw)==AETHER_ERROR_INVALID_STATE);ck(rw.work_rule_starts==1);ck(rule_budget.rules==0);
 // OOM leaves both the shared budget and all caller outputs untouched.
 bool done=false;unsigned oom=0;
 for(long n=0;n<500&&!done;++n){style_source_budget_s budget;std::vector<compiled_rule_s> out;int32_t id=9;style_source_accounting_s evidence;evidence.source_bytes=42;fail_after=n;
  try{auto status=style_source_compile("a{--x:a;color:red}",1,2,out,id,nullptr,nullptr,&evidence,&budget);fail_after=-1;ck(status==AETHER_OK);done=true;}
  catch(const std::bad_alloc&){fail_after=-1;++oom;ck(out.empty()&&id==9&&evidence.source_bytes==42);ck(budget.source_bytes==0&&budget.declarations==0&&budget.rules==0);}
 }
 ck(done&&oom);std::cout<<"T2.0 compiler budget PASS OOM="<<oom<<" overflow_spans="<<w.work_declaration_spans<<" rejected_source_copies="<<no_copy.work_source_bytes_copied<<'\n';
}
int main(){
 expansion_tests();
 compiler_budget_tests();
 {Fixture setup("<body><div>X</div></body>");std::vector<compiled_rule_s> rules;int32_t next=1;style_source_accounting_s a;
  ck(style_source_compile(".never{font-kerning:none}",1,2,rules,next,nullptr,nullptr,&a)==AETHER_OK);ck(rules.empty());ck(a.losses);ck(a.inputs_accounted);ck(a.declarations==1);
  ck(style_source_compile("@font-face{src:url(missing.woff2)}",2,2,rules,next,nullptr,nullptr,&a)==AETHER_OK);ck(rules.empty());ck(a.external_pending);ck(!a.inputs_accounted);
 }

 {Fixture f("<body><div id='x'>X</div></body>");auto x=f.id("x");auto initial=f.resolve();ck(inputs(initial,x).inputs_accounted);ck(!inputs(initial,x).losses);ck(typography_inputs_current(f.runtime,initial.map->current));ck(bool(initial.node(x)->typography()->text_diagnostic));ck(initial.node(x)->typography()->text_diagnostic->source_bytes=="X");
  for(const char* css:{"#absent{font-variant:small-caps}","#absent{font-variant-ligatures:none}","#absent{font-kerning:none}","#absent{unknown-thing:7}","@unknown{font-kerning:none}","@media(max-width:1px){#x{font-kerning:none}}","#absent{font-weight:garbage}","???{font-size:20px}","#x,???{font-size:20px}","#x:is(.ok,? ? ?){font-size:20px}"}) {
   const auto h=f.sheet(css);ck(!typography_inputs_current(f.runtime,initial.map->current));auto changed=f.resolve();ck(inputs(changed,x).losses);ck(inputs(changed,x).inputs_accounted);ck(typography_inputs_current(f.runtime,changed.map->current));ck(aether_style_runtime_remove_stylesheet(f.runtime,h));auto clean=f.resolve();ck(!inputs(clean,x).losses);ck(inputs(changed,x).losses);ck(!typography_inputs_current(f.runtime,changed.map->current));
  }
  auto h=f.sheet("@import url('never-fetched.css');");auto pending=f.resolve();ck(inputs(pending,x).external_pending);ck(!inputs(pending,x).inputs_accounted);ck(aether_style_runtime_remove_stylesheet(f.runtime,h));
  f.style(x,"font-kerning:none");auto inline_loss=f.resolve();ck(inputs(inline_loss,x).losses);ck(inputs(inline_loss,x).declarations==1);f.style(x,"");ck(!typography_inputs_current(f.runtime,inline_loss.map->current));
 }
 {Fixture f("<body><div id='p' style='font-kerning:none'><span id='x'>X</span></div><link rel='stylesheet' href='missing.css'></body>");auto s=f.resolve();ck(inputs(s,f.id("x")).external_pending);ck(!inputs(s,f.id("x")).inputs_accounted);ck(s.node(f.id("p"))->typography()->inputs==s.node(f.id("x"))->typography()->inputs);}
 {Fixture f("<body><div id='x'>X</div></body>");std::string css=".unmatched{";for(size_t i=0;i<33000;++i)css+="unknown:z;";css+="}";f.sheet(css);auto old=f.resolve();int64_t rejected=-1;int32_t count=-1;
  ck(aether_style_runtime_add_stylesheet(f.runtime,css.c_str(),2,"limit",&rejected,&count)==AETHER_ERROR_INVALID_STATE);ck(rejected==-1&&count==-1);ck(typography_inputs_current(f.runtime,old.map->current));
  std::string inline_css;for(size_t i=0;i<33000;++i)inline_css+="unknown:z;";f.style(f.id("x"),inline_css.c_str());aether_style_transaction_t tx{};ck(aether_style_runtime_prepare_revision(f.runtime,f.doc,&f.env,&tx)==AETHER_ERROR_INVALID_STATE);ck(!tx);
  aether_style_revision_t retained{};ck(aether_style_runtime_retain_revision(f.runtime,&retained)==AETHER_OK);ck(aether_style_revision_generation(retained)==aether_style_revision_generation(old.revision));aether_style_revision_destroy(retained);
 }

 {Fixture f("<body><div id='a'>A</div><div id='b'>B</div></body>");auto old=f.resolve();std::string css;for(size_t i=0;i<33000;++i)css+="--x:a;";f.style(f.id("a"),css.c_str());f.style(f.id("b"),css.c_str());aether_style_transaction_t tx{};ck(aether_style_runtime_prepare_revision(f.runtime,f.doc,&f.env,&tx)==AETHER_ERROR_INVALID_STATE);ck(!tx);aether_style_revision_t r{};ck(aether_style_runtime_retain_revision(f.runtime,&r)==AETHER_OK);ck(aether_style_revision_generation(r)==aether_style_revision_generation(old.revision));aether_style_revision_destroy(r);}
 {style_source_accounting_s a,b;a.source_bytes=style_source_accounting_s::max_source_bytes-1;b.source_bytes=1;a.merge(b);ck(a.inputs_accounted);a.merge(b);ck(!a.inputs_accounted);ck(a.source_bytes==style_source_accounting_s::max_source_bytes);}
 unsigned failures=0;bool completed=false;
 {Fixture f("<body><div id='x'>X</div></body>");auto x=f.id("x");auto old=f.resolve();const auto generation=aether_style_revision_generation(old.revision);f.style(x,"font-kerning:none");
  aether_style_transaction_t cancelled{};ck(aether_style_runtime_prepare_revision(f.runtime,f.doc,&f.env,&cancelled)==AETHER_OK);aether_style_transaction_destroy(cancelled);aether_style_revision_t retained{};ck(aether_style_runtime_retain_revision(f.runtime,&retained)==AETHER_OK);ck(aether_style_revision_generation(retained)==generation);aether_style_revision_destroy(retained);
  for(long n=0;n<3000&&!completed;++n){aether_style_transaction_t tx{};fail_after=n;auto status=aether_style_runtime_prepare_revision(f.runtime,f.doc,&f.env,&tx);fail_after=-1;
   if(status==AETHER_OK){aether_style_revision_t fresh{};ck(aether_style_runtime_commit_revision(f.runtime,tx,&fresh)==AETHER_OK);aether_style_revision_destroy(fresh);completed=true;}
   else{ck(status==AETHER_ERROR_OUT_OF_MEMORY);ck(!tx);++failures;aether_style_revision_t r{};ck(aether_style_runtime_retain_revision(f.runtime,&r)==AETHER_OK);ck(aether_style_revision_generation(r)==generation);aether_style_revision_destroy(r);}
   aether_style_transaction_destroy(tx);ck(!inputs(old,x).losses);
  }
  auto now=f.resolve();bool worker_current=true;std::thread worker([&]{worker_current=typography_inputs_current(f.runtime,now.map->current);});worker.join();ck(!worker_current);
  aether_style_transaction_t stale{};ck(aether_style_runtime_prepare_revision(f.runtime,f.doc,&f.env,&stale)==AETHER_OK);f.style(x,"font-variant:none");aether_style_revision_t no{};ck(aether_style_runtime_commit_revision(f.runtime,stale,&no)==AETHER_ERROR_INVALID_STATE);ck(!no);aether_style_transaction_destroy(stale);
 }
 ck(completed&&failures);std::cout<<"T2.0 runtime PASS checks="<<checks<<" prepare_OOM_points="<<failures<<" context=UNPROVEN font=UNRESOLVED render=CLOSED\n";
}
