#ifdef NDEBUG
#error Assertions required
#endif
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../../native/src/style_runtime/style_runtime_types.h"
#include "../../native/src/style_runtime/h5_parser_profile.h"
#include "../../native/src/dom/dom_types.h"
#include "../../native/src/css/computed_style_handle.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
static size_t checks=0;static long budget=-1;
void* operator new(size_t n){if(budget==0)throw std::bad_alloc();if(budget>0)--budget;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
static void ck(bool b){if(!b)std::cerr<<"FAIL "<<checks<<"\n";assert(b);++checks;}
struct Scene {
 aether_doc_t doc{};aether_style_runtime_t rt{};aether_style_revision_t rev{};
 aether_style_environment_t env{400,200,1,16,0,0,0};
 Scene(){ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,"<body><div id='p' class='host'><span id='x'></span></div></body>")==AETHER_OK);ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);}
 ~Scene(){aether_style_revision_destroy(rev);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
 int64_t add(const char* css){int64_t h{};int32_t n{};ck(aether_style_runtime_add_stylesheet(rt,css,1,"h19",&h,&n)==AETHER_OK);return h;}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}
 void resolve(){aether_style_transaction_t tx{};ck(aether_style_runtime_prepare_revision(rt,aether_doc_get_body(doc),&env,&tx)==AETHER_OK);aether_style_revision_t next{};ck(aether_style_runtime_commit_revision(rt,tx,&next)==AETHER_OK);aether_style_transaction_destroy(tx);aether_style_revision_destroy(rev);rev=next;}
 float opacity(aether_style_revision_t r,aether_node_t target){for(int i=0;i<aether_style_revision_style_count(r);++i){aether_node_t n{};aether_computed_style_handle_t h{};ck(aether_style_revision_get_style(r,i,&n,&h,nullptr)==AETHER_OK);float f=h->style.opacity;aether_computed_style_handle_destroy(h);if(n==target)return f;}ck(false);return -1;}
 void intact(){ck(opacity(rev,node("p"))==.8f);ck(opacity(rev,node("x"))==.6f);ck(node("p")->children.size()==1);ck(!node("p")->children[0]->is_generated);ck(node("p")->generated_storage.empty());}
};
int main(){
 static_assert(static_cast<unsigned>(aether_pseudo_element_t::before)==1&&static_cast<unsigned>(aether_pseudo_element_t::after)==2);
 for(auto text:{"div::first-letter","div:first-letter","div::FIRST-LETTER",R"(div::first-\6c etter)","section > div::first-letter","div.x::first-letter"}){compiled_selector_s s;ck(selector_parse(text,s));ck(s.pseudo_element==aether_pseudo_element_t::first_letter);ck(s.specificity.type_count==((std::string(text).find("section")!=std::string::npos)?3:2));}
 for(auto text:{"div::first-letter.x","div::first-letter::before","div::first-letter:hover","div::first-letter()","div:first-letter(1)","div:has(::first-letter)","div:is(::first-letter)","div:not(:first-letter)","div:where(.x,::first-letter)"}){std::vector<compiled_selector_s> s;ck(!selector_parse_list_strict_profile(text,s));}
 {std::vector<compiled_selector_s> s;ck(selector_parse_list_strict_profile("div::first-letter,span:first-letter",s));ck(s.size()==2);ck(s[0].specificity==s[1].specificity);}
 for(auto pseudo:{"::first-letter",":first-letter",R"(::first-\6c etter)"}){
  Scene s;s.add("#p{display:flex;opacity:.8}#x{opacity:.6}");auto h=s.add((std::string("#p")+pseudo+"{opacity:.1!important;content:'X';font-size:40px;float:left} #x"+pseudo+"{opacity:.2!important}").c_str());s.resolve();s.intact();
  for(auto display:{"block","inline-flex","flex"}){ck(aether_element_set_attribute(s.node("p"),"style",(std::string("display:")+display).c_str())==AETHER_OK);s.resolve();s.intact();}
  ck(aether_element_set_attribute(s.node("p"),"class","different")==AETHER_OK);s.resolve();s.intact();ck(aether_style_runtime_remove_stylesheet(s.rt,h)!=0);s.resolve();s.intact();
 }
 {Scene s;s.add("#p{opacity:.8}#x{opacity:.6}@media(min-width:1px){#p::first-letter{opacity:0!important}}@supports(display:flex){#p:first-letter{content:'Y'}}");s.resolve();s.intact();}
 size_t parser_oom=0;bool parser_done=false;
 for(long i=0;i<2000;++i){std::vector<compiled_selector_s> s;budget=i;bool ok=false;try{ok=selector_parse_list_strict_profile("section > .host::first-letter,span:first-letter",s);}catch(const std::bad_alloc&){}budget=-1;if(ok){ck(s.size()==2);parser_done=true;break;}++parser_oom;}ck(parser_done&&parser_oom);
 size_t runtime_oom=0;bool runtime_done=false;
 {Scene s;s.add("#p{display:flex;opacity:.8}#x{opacity:.6}");s.resolve();auto generation=aether_style_revision_generation(s.rev);s.add("#p::first-letter{opacity:0!important;content:'Z'}");
 for(long i=0;i<10000;++i){aether_style_transaction_t tx{};budget=i;auto status=aether_style_runtime_prepare_revision(s.rt,aether_doc_get_body(s.doc),&s.env,&tx);budget=-1;s.intact();aether_style_revision_t retained{};ck(aether_style_runtime_retain_revision(s.rt,&retained)==AETHER_OK);ck(aether_style_revision_generation(retained)==generation);aether_style_revision_destroy(retained);
 if(status==AETHER_OK){aether_style_revision_t next{};ck(aether_style_runtime_commit_revision(s.rt,tx,&next)==AETHER_OK);aether_style_transaction_destroy(tx);ck(s.opacity(next,s.node("p"))==.8f);aether_style_revision_destroy(next);runtime_done=true;break;}ck(status==AETHER_ERROR_OUT_OF_MEMORY&&tx==nullptr);++runtime_oom;
 }ck(runtime_done&&runtime_oom);s.resolve();s.intact();}
 std::cout<<"H19 checks="<<checks<<" parser_OOM="<<parser_oom<<" runtime_OOM="<<runtime_oom<<"\n";
}
