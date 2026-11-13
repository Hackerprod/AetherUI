#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../../native/src/layout/layout_internal.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
struct Failure {const char* reason;};
void require(bool ok,const char* reason){if(!ok)throw Failure{reason};}
#include "h18_profile.h"
static long budget=-1;
void* operator new(size_t n){if(budget==0)throw std::bad_alloc();if(budget>0)--budget;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
int main(){
 aether_doc_t doc{};assert(aether_doc_create(&doc)==AETHER_OK);
 assert(aether_doc_parse_html(doc,"<body><section style='display:flex'><div style='display:contents'><span style='display:contents'><div style='width:20px;height:10px'></div></span><div style='width:10px'></div></div></section></body>")==AETHER_OK);
 aether_style_environment_t env{300,200,1,16,0,0,0};aether_style_runtime_t rt{};assert(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);
 int64_t sheet;int32_t nr;assert(aether_style_runtime_add_stylesheet(rt,aether_style_runtime_defaults_css(),0,"ua",&sheet,&nr)==AETHER_OK);
 aether_style_update_t* updates{};auto count=aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);
 aether_style_map_t map{};assert(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==AETHER_OK);
 h18::before(map);size_t failures=0;bool done=false;
 for(long i=0;i<1000;++i){budget=i;try{h18::before(map);budget=-1;done=true;break;}catch(const std::bad_alloc&){budget=-1;++failures;}}
 assert(done&&failures>0);h18::before(map);
 aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);
 std::cout<<"H18 gate OOM="<<failures<<"\n";
}
