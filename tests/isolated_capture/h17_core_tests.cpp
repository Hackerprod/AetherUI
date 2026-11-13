#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../../native/src/style_runtime/style_runtime_types.h"
#include "../../native/src/style_runtime/h5_parser_profile.h"
#include "../../native/src/dom/dom_types.h"
#include "../../native/src/css/computed_style_handle.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
struct Objects { aether_doc_t doc{};aether_style_runtime_t runtime{};~Objects(){aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);} };
static size_t checks=0; static long budget=-1;
void* operator new(size_t n){if(budget==0)throw std::bad_alloc();if(budget>0)--budget;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
void ck(bool b){if(!b)std::cerr<<"FAIL "<<checks<<"\n";assert(b);++checks;}
static const char* css="*{opacity:.2} :first-of-type{opacity:.8}";
static void add(aether_style_runtime_t rt){int64_t h;int32_t n;ck(aether_style_runtime_add_stylesheet(rt,css,1,"h17",&h,&n)==AETHER_OK);}
static std::unordered_map<aether_node_t,float> resolve(aether_style_runtime_t rt,aether_doc_t doc){
 aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_document_element(doc),&u);ck(n>=0);
 std::unordered_map<aether_node_t,float> result;
 for(int i=0;i<n;i++){aether_node_t node;aether_computed_style_handle_t h{};ck(aether_style_runtime_get_update(u[i],&node,&h,nullptr)==AETHER_OK);aether_computed_style_t s{};ck(aether_computed_style_copy(h,&s)==AETHER_OK);result[node]=s.opacity;aether_computed_style_free(&s);aether_computed_style_handle_destroy(h);}
 aether_style_updates_free(u,n);return result;
}
static float revision_basis(aether_style_revision_t revision,aether_node_t target) {
 for(int i=0;i<aether_style_revision_style_count(revision);++i){aether_node_t node{};aether_computed_style_handle_t style{};
  ck(aether_style_revision_get_style(revision,i,&node,&style,nullptr)==AETHER_OK);
  const float basis=style->style.flex_basis.length.value;aether_computed_style_handle_destroy(style);if(node==target)return basis;
 }ck(false);return 0;
}
static size_t runtime_oom(){
 Objects o;ck(aether_doc_create(&o.doc)==AETHER_OK);ck(aether_doc_parse_html(o.doc,"<body><div id='x' style='flex:0 0 30px'></div></body>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{400,200,1,16,0,0,0};ck(aether_style_runtime_create(&options,&env,nullptr,nullptr,&o.runtime)==AETHER_OK);
 aether_style_transaction_t tx{};ck(aether_style_runtime_prepare_revision(o.runtime,aether_doc_get_body(o.doc),&env,&tx)==AETHER_OK);
 aether_style_revision_t old{};ck(aether_style_runtime_commit_revision(o.runtime,tx,&old)==AETHER_OK);aether_style_transaction_destroy(tx);
 auto* target=aether_doc_get_element_by_id(o.doc,"x");const auto generation=aether_style_revision_generation(old);
 int64_t sh;int32_t nr;ck(aether_style_runtime_add_stylesheet(o.runtime,"div:first-of-type{flex-basis:80px!important}",1,"h17",&sh,&nr)==AETHER_OK);
 size_t failures=0;bool done=false;
 for(long i=0;i<10000;++i){tx=nullptr;budget=i;
  auto status=aether_style_runtime_prepare_revision(o.runtime,aether_doc_get_body(o.doc),&env,&tx);budget=-1;
  ck(revision_basis(old,target)==30);
  aether_style_revision_t current{};ck(aether_style_runtime_retain_revision(o.runtime,&current)==AETHER_OK);ck(aether_style_revision_generation(current)==generation);ck(revision_basis(current,target)==30);aether_style_revision_destroy(current);
  if(status==AETHER_OK){aether_style_revision_t next{};ck(aether_style_runtime_commit_revision(o.runtime,tx,&next)==AETHER_OK);ck(revision_basis(next,target)==80);aether_style_revision_destroy(next);aether_style_transaction_destroy(tx);done=true;break;}
  ck(status==AETHER_ERROR_OUT_OF_MEMORY&&tx==nullptr);++failures;
 }
 ck(done&&failures>0);ck(revision_basis(old,target)==30);aether_style_revision_destroy(old);return failures;
}

int main(){
 Objects o;ck(aether_doc_create(&o.doc)==AETHER_OK);
 ck(aether_doc_parse_html(o.doc,"<html><body><section id='p'>text<!--c--><i id='a'></i><div id='x' class='x'></div><i id='b' class='x' hidden></i><div id='y' class='x'></div></section><section id='q'><i id='d'></i></section></body></html>")==AETHER_OK);
 auto node=[&](const char* id){return aether_doc_get_element_by_id(o.doc,id);};
 for(auto pair:std::vector<std::pair<const char*,const char*>>{{"i:first-of-type","a"},{"div:first-of-type","x"},{".x:first-of-type","x"},{"*:FIRST-OF-TYPE","x"},{"div:first-of-\\74 ype","x"},{"section > div:first-of-type","x"},{"i:first-of-type + div","x"}}){
 compiled_selector_s s;ck(selector_parse(pair.first,s));ck(selector_match(s,node(pair.second)));ck((s.dependencies&SELECTOR_DEP_STRUCTURE)!=0);
 }
 compiled_selector_s s;ck(selector_parse(".x:first-of-type",s));ck(!selector_match(s,node("b")));ck(!selector_match(s,node("y")));
 compiled_selector_s root_selector;ck(selector_parse("html:first-of-type",root_selector));ck(selector_match(root_selector,aether_doc_get_document_element(o.doc)));
 compiled_selector_s invalid;ck(!selector_parse("div:first-of-type(1)",invalid));
 aether_style_environment_t env{400,200,1,16,0,0,0};ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&o.runtime)==AETHER_OK);add(o.runtime);
 auto accumulated=resolve(o.runtime,o.doc);
 auto compare=[&](){for(auto [n,v]:resolve(o.runtime,o.doc))accumulated[n]=v;aether_style_runtime_t fresh{};ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&fresh)==AETHER_OK);add(fresh);auto expected=resolve(fresh,o.doc);for(auto [n,v]:expected){if(accumulated.at(n)!=v)std::cerr<<"mutation mismatch "<<n->tag_name<<" id="<<n->attributes["id"]<<" actual="<<accumulated.at(n)<<" expected="<<v<<"\n";ck(accumulated.at(n)==v);}aether_style_runtime_destroy(fresh);};
 compare();
 ck(aether_node_append_child(node("p"),node("a"))==AETHER_OK);compare();ck(accumulated[node("b")]==.8f&&accumulated[node("a")]==.2f);
 ck(aether_node_append_child(node("q"),node("b"))==AETHER_OK);compare();ck(accumulated[node("a")]==.8f);
 auto* detached=node("d");ck(aether_node_remove_child(node("q"),detached)==AETHER_OK);compare();ck(accumulated[node("b")]==.8f);
 compiled_selector_s disconnected;ck(selector_parse("i:first-of-type",disconnected));ck(selector_match(disconnected,detached));
 ck(aether_node_append_child(node("p"),detached)==AETHER_OK);compare();
 size_t failures=0;bool done=false;
 for(long i=0;i<1000;i++){std::vector<compiled_selector_s> out;budget=i;bool ok=false;try{ok=selector_parse_list_strict_profile("i:first-of-type, div:first-of-type",out);}catch(const std::bad_alloc&){}budget=-1;if(ok){ck(out.size()==2);done=true;break;}++failures;}
 ck(done&&failures>0);
 auto runtime_failures=runtime_oom();std::cout<<"runtime OOM "<<runtime_failures<<" ";
 std::cout<<"H17 checks "<<checks<<" OOM "<<failures<<"\n";
}
