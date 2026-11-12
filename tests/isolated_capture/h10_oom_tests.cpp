#include "../../native/src/layout/layout_internal.h"
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include <cassert>
#include <iostream>
#include <cstring>
static long budget=-1;
static bool delivered=false;
void* operator new(size_t n) {
    if(budget>=0 && budget--==0){delivered=true;throw std::bad_alloc();}
    if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void*p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete[](void*p,const std::nothrow_t&)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t alignment) {
    if(budget>=0 && budget--==0){delivered=true;throw std::bad_alloc();}
    void*p=nullptr;size_t a=static_cast<size_t>(alignment);if(a<sizeof(void*))a=sizeof(void*);
    if(posix_memalign(&p,a,n?n:1))throw std::bad_alloc();return p;
}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void*p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void*p,size_t,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void*p,size_t,std::align_val_t)noexcept{std::free(p);}

static aether_text_metrics_t no_text(const char*,const aether_computed_style_t*,float,void*){return {};}
struct Scene {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t styles{};aether_layout_engine_t engine{};aether_layout_result_t result{};
 Scene(const std::string& css){assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,"<body><div id='p'><div id='a'><div id='b'></div><div id='c'></div></div></div></body>")==AETHER_OK);aether_style_runtime_options_t o{};aether_style_environment_t e{300,160,1,16,0,0,0};assert(aether_style_runtime_create(&o,&e,nullptr,nullptr,&runtime)==AETHER_OK);int64_t sh;int32_t n;auto all=std::string("body{display:block;margin:0}#p{display:block;width:200px}#a{display:flex;flex-wrap:wrap}#b{width:40px;height:20px;flex:none}#c{width:60px;height:20px;flex:none}")+css;assert(aether_style_runtime_add_stylesheet(runtime,all.c_str(),0,"H8",&sh,&n)==AETHER_OK);resolve();assert(aether_layout_create(no_text,nullptr,&engine)==AETHER_OK);}
 void resolve(){aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&u);assert(n>=0);aether_style_updates_free(u,n);aether_style_map_destroy(styles);assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);}
 aether_status_t layout(){aether_bool_t changed{};const auto prepared=aether_layout_prepare_transition_context(engine,nullptr,styles,nullptr,&changed);if(prepared!=AETHER_OK)return prepared;return aether_layout_layout(engine,doc,styles,300,160,nullptr,nullptr,&result);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}
 float width(const char* id="a"){auto* b=aether_layout_result_find_primary_box(result,node(id));if(!b){std::cerr<<"missing node "<<id<<" node="<<node(id)<<"\n";for(auto* q:result->allocated_boxes) std::cerr<<"box "<<q->element<<" kind "<<q->kind<<"\n";}assert(b);return b->border_box.width;}
 ~Scene(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
int main(){size_t oom=0,fallback=0;

 Scene s("#a{display:block;transform:translateZ(0);width:80px}#b,#c{position:fixed;left:5px;top:5px}");bool done=false;
 for(long n=0;n<5000;++n){delivered=false;budget=n;auto status=s.layout();budget=-1;
  if(status!=AETHER_OK){assert(status==AETHER_ERROR_OUT_OF_MEMORY&&s.result==nullptr);++oom;continue;}
  assert(s.width()==80);aether_layout_result_destroy(s.result);s.result=nullptr;
  if(delivered){++fallback;continue;}done=true;break;
 }assert(done);std::cout<<"H10 layout OOM="<<oom<<" fallback="<<fallback<<"\n";
}
