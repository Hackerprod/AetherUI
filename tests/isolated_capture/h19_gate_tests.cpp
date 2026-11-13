#ifdef NDEBUG
#error Assertions required
#endif
#include "h19_profile.h"
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../../native/src/dom/dom_types.h"
#include <cassert>
#include <cstdlib>
#include <new>
#include <iostream>
#include <string>
static long budget=-1;
void* operator new(size_t n){if(budget==0)throw std::bad_alloc();if(budget>0)--budget;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
int main(){
 aether_doc_t doc{};assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,"<body><section id='host' class='f'><div></div></section></body>")==AETHER_OK);
 aether_style_runtime_t rt{};aether_style_environment_t env{240,120,1,16,0,0,0};assert(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);int64_t h;int32_t rules;
 assert(aether_style_runtime_add_stylesheet(rt,".f{display:flex}",1,"test",&h,&rules)==AETHER_OK);aether_style_update_t* u=nullptr;int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(doc),&u);assert(n>=0);aether_style_updates_free(u,n);aether_style_map_t map{};assert(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==AETHER_OK);
 size_t checks=0;for(auto s:{".f::first-letter",".f:first-letter","body>.f::first-letter",".missing::first-letter"}){h19::Gate g;assert(g.add(s)&&g.needed()&&g.check(doc,map));++checks;}
 for(auto s:{".f:hover::first-letter",":is(.f)::first-letter",".f~.f::first-letter",".f::first-letter,.f > div::first-letter"}){h19::Gate g;assert(!g.add(s)||!g.check(doc,map));++checks;}
 h19::Gate retained;assert(retained.add(".f::first-letter"));
 auto refresh=[&](){aether_style_transaction_t tx{};assert(aether_style_runtime_prepare_revision(rt,aether_doc_get_body(doc),&env,&tx)==AETHER_OK);aether_style_revision_t rev{};assert(aether_style_runtime_commit_revision(rt,tx,&rev)==AETHER_OK);aether_style_transaction_destroy(tx);aether_style_revision_destroy(rev);aether_style_map_destroy(map);map=nullptr;assert(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==AETHER_OK);};
 auto host=aether_doc_get_element_by_id(doc,"host");
 for(auto display:{"block","flex","block"}){assert(aether_element_set_attribute(host,"style",(std::string("display:")+display).c_str())==AETHER_OK);refresh();assert(retained.check(doc,map)==(std::string(display)=="flex"));++checks;}
 assert(aether_element_set_attribute(host,"class","other")==AETHER_OK);refresh();assert(retained.check(doc,map));++checks;
 assert(aether_element_set_attribute(host,"class","f")==AETHER_OK);refresh();assert(!retained.check(doc,map));++checks;
 assert(aether_element_set_attribute(host,"style","display:flex")==AETHER_OK);refresh();assert(retained.check(doc,map));++checks;
 size_t oom=0;bool done=false;for(long i=0;i<2000;++i){bool published=false;budget=i;try{h19::Gate g;bool added=g.add("body > .f::first-letter");bool valid=added&&g.check(doc,map);budget=-1;assert(valid);published=valid;done=true;break;}catch(const std::bad_alloc&){budget=-1;assert(!published);++oom;}}
 assert(done&&oom>0);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);std::cout<<"H19 gate="<<checks<<" OOM="<<oom<<"\n";
}
