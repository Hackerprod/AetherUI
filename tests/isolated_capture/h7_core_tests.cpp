#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/style_runtime/paused_keyframes.h"
#include "../../native/src/css/computed_style_handle.h"
#include "../../native/src/css/frozen_measure_read.h"
#include <aether/aether_contextual_style.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <new>
#include <cstdlib>
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
static int diagnostics=0;
static void diagnostic(const char* code,const char*,const char*,const char*,void*){if(!std::strcmp(code,"AETHER-CSS-ANIMATION-PROFILE"))++diagnostics;}
struct Scene {
 aether_doc_t doc{};aether_node_t node{};aether_style_runtime_t runtime{};aether_style_map_t styles{};
 Scene(){assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,"<body><div id='x'></div></body>")==AETHER_OK);node=aether_doc_get_element_by_id(doc,"x");aether_style_runtime_options_t o{};aether_style_environment_t e{600,180,1,16,0,0,0};assert(aether_style_runtime_create(&o,&e,diagnostic,nullptr,&runtime)==AETHER_OK);}
 int64_t add(const std::string& css){int64_t h;int32_t n;assert(aether_style_runtime_add_stylesheet(runtime,css.c_str(),2,"H7",&h,&n)==AETHER_OK);return h;}
 aether_computed_style_handle_t resolve(){aether_style_update_t* updates{};int n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(n>=0);aether_style_updates_free(updates,n);aether_style_map_t m{};assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&m)==AETHER_OK);auto h=aether_style_map_get(m,node);aether_style_map_destroy(m);return h;}
 ~Scene(){if(runtime)aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
static float grow(aether_computed_style_handle_t h){aether_computed_style_t v{};assert(aether_computed_style_copy(h,&v)==AETHER_OK);float f=v.flex_grow;aether_computed_style_free(&v);return f;}
int main(){style_property_registry_init();size_t checks=0;
 for(auto body:{"from{gap:10px}to{gap:50px}","0%{flex-grow:1}100%{flex-grow:3}","from{order:-2}to{order:2}","from{flex-direction:row}to{flex-direction:column}","from{flex-basis:100px}to{flex-basis:200px}","from{gap:10px}from{gap:20px}to{gap:50px}","from{gap:10px;width:3px!important}to{gap:50px}"}){auto p=paused_keyframes_parse("MiXeD",body);assert(p.profile_supported&&p.name=="MiXeD");++checks;}
 for(auto body:{"from{gap:10px}","from{gap:10px!important}to{gap:50px}","from{gap:10px}to{row-gap:50px}","from{gap:10%}to{gap:50px}","from{gap:calc(1px)}to{gap:50px}","from{order:1.5}to{order:2}","from{width:1px}to{width:2px}","50%{gap:1px}to{gap:2px}","from{flex-grow:1e999}to{flex-grow:2}","from{order:2147483648}to{order:1}","from{gap:10px}to{gap:20px}"}){bool expected=std::string(body)=="from{gap:10px}to{gap:20px}";assert(paused_keyframes_parse("a",body).profile_supported==expected);++checks;}
 assert(paused_keyframes_parse(R"(\4d iXeD)","from{flex-grow:1}to{flex-grow:3}").name=="MiXeD");++checks;
 const std::string k="@keyframes Grow{from{flex-grow:1}to{flex-grow:3}}";
 for(auto delay:{"-2.5s","-4.9s","-5s","-5.1s","-7.5s","1s","-10s","-11s"}){
  Scene s;s.add(k+"#x{flex-grow:9;animation:Grow 10s linear "+delay+" paused}");auto h=s.resolve();double d=std::strtod(delay,nullptr);float want=d<=0&&-d<10?float(1+2*(-d/10)):9;assert(std::abs(grow(h)-want)<.0001);aether_computed_style_handle_destroy(h);++checks;
 }
 {Scene s;s.add(k+"#x{flex-grow:9!important;animation:Grow 10s linear -5s paused}");auto h=s.resolve();assert(grow(h)==9);aether_computed_style_handle_destroy(h);++checks;}
 {Scene s;auto first=s.add(k+"#x{flex-grow:9;animation:Grow 10s linear -5s paused}");auto old=s.resolve();assert(grow(old)==2);
  auto second=s.add("@keyframes Grow{from{flex-grow:3}to{flex-grow:5}}");auto now=s.resolve();assert(grow(now)==4&&grow(old)==2);aether_computed_style_handle_destroy(now);
  assert(aether_style_runtime_remove_stylesheet(s.runtime,second));now=s.resolve();assert(grow(now)==2);aether_computed_style_handle_destroy(now);
  s.add("#x{animation:none}");now=s.resolve();assert(grow(now)==9&&grow(old)==2);aether_computed_style_handle_destroy(now);
  aether_computed_style_handle_t clone{};assert(aether_computed_style_handle_clone(old,&clone)==AETHER_OK);aether_computed_style_handle_destroy(old);aether_style_runtime_destroy(s.runtime);s.runtime=nullptr;assert(grow(clone)==2);aether_computed_style_handle_destroy(clone);(void)first;checks+=5;}
 for(auto wrapper:{"@media print{", "@supports not (display:flex){"}){Scene s;s.add(k+wrapper+"@keyframes Grow{from{flex-grow:8}to{flex-grow:10}}}#x{animation:Grow 10s linear -5s paused}");auto h=s.resolve();assert(grow(h)==2);aether_computed_style_handle_destroy(h);++checks;}
 {Scene s;s.add(k+"#x{animation-name:Grow;animation-duration:10s;animation-delay:-5s;animation-timing-function:linear;animation-play-state:paused}");auto h=s.resolve();assert(grow(h)==2);aether_computed_style_handle_destroy(h);++checks;}
 for(auto value:{"Grow 10s linear -5s running","Grow 10s ease -5s paused","Grow 0s linear paused","unknown 10s linear paused"}){Scene s;s.add(k+"#x{animation:"+value+"}");int before=diagnostics;auto h=s.resolve();assert(diagnostics>before);aether_computed_style_handle_destroy(h);++checks;}
 {Scene s;s.add(k+"@media(min-width:700px){@keyframes Grow{from{flex-grow:3}to{flex-grow:5}}}#x{animation:Grow 10s linear -5s paused}");auto old=s.resolve();assert(grow(old)==2);
  aether_style_environment_t env{800,180,1,16,0,0,0};aether_style_update_t* updates{};
  int n=aether_style_runtime_set_environment(s.runtime,&env,aether_doc_get_body(s.doc),&updates);assert(n>=0);
  bool observed=false;for(int i=0;i<n;++i){aether_node_t node{};aether_computed_style_handle_t h{};uint32_t impact{};assert(aether_style_runtime_get_update(updates[i],&node,&h,&impact)==AETHER_OK);if(node==s.node){assert(grow(h)==4);observed=true;}aether_computed_style_handle_destroy(h);}
  assert(observed&&grow(old)==2);aether_style_updates_free(updates,n);auto now=s.resolve();assert(grow(now)==4&&grow(old)==2);aether_computed_style_handle_destroy(old);aether_computed_style_handle_destroy(now);++checks;
 }
 size_t prepare_failures=0;
 {Scene s;s.add("#x{flex-grow:9}");auto old=s.resolve();s.add(k+"#x{animation:Grow 10s linear -5s paused}");bool done=false;
  for(long attempt=0;attempt<3000;++attempt){aether_style_transaction_t transaction=nullptr;fail_after=attempt;auto status=aether_style_runtime_prepare_revision(s.runtime,aether_doc_get_body(s.doc),nullptr,&transaction);fail_after=-1;
   assert(grow(old)==9);if(status!=AETHER_OK){assert(status==AETHER_ERROR_OUT_OF_MEMORY&&transaction==nullptr);++prepare_failures;continue;}
   aether_style_revision_t revision=nullptr;assert(aether_style_runtime_commit_revision(s.runtime,transaction,&revision)==AETHER_OK);aether_style_revision_destroy(revision);aether_style_transaction_destroy(transaction);done=true;break;
  }assert(done);auto now=s.resolve();assert(grow(now)==2&&grow(old)==9);aether_computed_style_handle_destroy(now);aether_computed_style_handle_destroy(old);++checks;
 }
 // Compiler rule IDs, keyframes, selectors and media graphs publish together.
 size_t failures=0;bool done=false;
 for(long n=0;n<1500;++n){std::vector<compiled_rule_s> rules;int32_t id=17;fail_after=n;try{auto st=style_source_compile("@media(min-width:1px){@keyframes K{from{gap:10px}to{gap:50px}}a{animation:K 10s linear -5s paused}}",1,2,rules,id,nullptr,nullptr);fail_after=-1;if(!(st==AETHER_OK&&rules.size()==2&&id==19&&rules[0].keyframes)){std::cerr<<"OOM silent n="<<n<<" status="<<st<<" rules="<<rules.size()<<" id="<<id<<"\n";std::abort();}done=true;break;}catch(const std::bad_alloc&){fail_after=-1;assert(rules.empty()&&id==17);++failures;}}
 assert(done);
 {std::vector<compiled_rule_s> rules;int32_t id=INT32_MAX;auto st=style_source_compile(k.c_str(),1,2,rules,id,nullptr,nullptr);assert(st==AETHER_ERROR_INVALID_STATE&&rules.empty()&&id==INT32_MAX);++checks;}
 {std::string css="@supports not(display:flex){@keyframes a{";for(int n=0;n<33;++n)css+="(";css+="0";for(int n=0;n<33;++n)css+=")";css+="}}";std::vector<compiled_rule_s> rules;int32_t id=17;assert(style_source_compile(css.c_str(),1,2,rules,id,nullptr,nullptr)==AETHER_ERROR_INVALID_STATE&&rules.empty()&&id==17);++checks;}
 {std::string css;for(int i=0;i<6000;++i)css+=k;std::vector<compiled_rule_s> rules;int32_t id=17;assert(style_source_compile(css.c_str(),1,2,rules,id,nullptr,nullptr)==AETHER_ERROR_INVALID_STATE&&rules.empty()&&id==17);++checks;}
 std::cout<<"H7 core checks="<<checks<<" preparation OOM="<<prepare_failures<<" compiler OOM="<<failures<<"\n";
}
