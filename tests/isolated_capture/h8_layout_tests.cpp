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
 aether_status_t layout(){return aether_layout_layout(engine,doc,styles,300,160,nullptr,nullptr,&result);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}
 float width(const char* id="a"){auto* b=aether_layout_result_find_primary_box(result,node(id));if(!b){std::cerr<<"missing node "<<id<<" node="<<node(id)<<"\n";for(auto* q:result->allocated_boxes) std::cerr<<"box "<<q->element<<" kind "<<q->kind<<"\n";}assert(b);return b->border_box.width;}
 ~Scene(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
int main(){using K=aether::css::intrinsic_size_keyword;using P=intrinsic_keyword_phase_e;size_t checks=0;
 for(bool definite:{false,true})for(auto phase:{P::used,P::min_contribution,P::max_contribution})for(float a:{20.f,80.f,200.f}){assert(resolve_intrinsic_keyword(K::min_content,"width",{60,100},a,10,definite,phase)==60);assert(resolve_intrinsic_keyword(K::max_content,"width",{60,100},a,10,definite,phase)==100);checks+=2;}
 assert(std::isnan(resolve_intrinsic_keyword(K::fit_content,"width",{60,100},80,0,false,P::used)));assert(resolve_intrinsic_keyword(K::fit_content,"min-width",{60,100},80,0,false,P::used)==60);assert(resolve_intrinsic_keyword(K::fit_content,"max-width",{60,100},80,0,false,P::used)==100);checks+=3;
 for(auto [css,w]:std::initializer_list<std::pair<const char*,float>>{{"#a{width:min-content}",60},{"#a{width:max-content}",100},{"#a{width:fit-content}",100},{"#p{width:80px}#a{width:fit-content}",80},{"#p{width:20px}#a{width:fit-content}",60},{"#a{width:max-content;padding:10px;border:2px solid red}",124},{"#a{width:max-content;padding:10px;border:2px solid red;box-sizing:border-box}",124},{"#a{width:10px;min-width:min-content}",60},{"#a{width:200px;max-width:max-content}",100},{"#a{width:20px;min-width:max-content;max-width:min-content}",100},{"#p{display:flex}#a{flex:0 0 min-content}",60},{"#p{display:flex}#a{flex:0 0 max-content}",100},{"#p{display:flex;width:80px}#a{flex:0 0 fit-content}",80},{"#p{display:flex;flex-direction:row-reverse}#a{flex:0 0 max-content}",100},{"#a{--s:max-content;width:var(--s)}",100}}){Scene s(css);assert(s.layout()==AETHER_OK);if(s.width()!=w)std::cerr<<css<<" got "<<s.width()<<" want "<<w<<"\n";assert(s.width()==w);++checks;}
 for(auto direction:{"column","column-reverse"}){Scene s(std::string("#p{display:flex;flex-direction:")+direction+"}#a{flex-basis:min-content}");assert(s.layout()==AETHER_ERROR_INVALID_STATE&&s.result==nullptr);++checks;}
 {Scene s("#a{width:min-content}");assert(s.layout()==AETHER_OK&&s.width()==60);auto old=s.result;s.result=nullptr;assert(aether_element_set_attribute(s.node("a"),"style","width:77px")==AETHER_OK);s.resolve();assert(s.layout()==AETHER_OK&&s.width()==77);assert(aether_layout_result_find_primary_box(old,s.node("a"))->border_box.width==60);aether_layout_result_destroy(old);aether_layout_result_destroy(s.result);s.result=nullptr;assert(aether_element_set_attribute(s.node("a"),"style","width:max-content")==AETHER_OK);s.resolve();assert(s.layout()==AETHER_OK&&s.width()==100);++checks;}
 // Descendant min/max keywords must not enter raw public-length resolution.
 for(auto [css,expected]:std::initializer_list<std::pair<const char*,float>>{
  {"#a{width:max-content}#b{min-width:min-content}",100},
  {"#a{width:max-content}#b{max-width:max-content}",60},
  {"#a{width:min-content}#b{min-width:fit-content}",60}
 }){Scene s(css);assert(s.layout()==AETHER_OK);if(s.width()!=expected)std::cerr<<css<<" got "<<s.width()<<" want "<<expected<<"\n";assert(s.width()==expected);++checks;}
 // OOF keywords are not auto/stretch, and fit-content uses the inset span
 // while percentage padding/margins retain the original containing-block basis.
 for(auto position:{"absolute","fixed"}) {
  const int inset=std::string(position)=="fixed"?110:60;
  for(auto [keyword,expected]:std::initializer_list<std::pair<const char*,float>>{{"min-content",60},{"max-content",100},{"fit-content",80}}){
   Scene s(std::string("#p{position:relative}#a{position:")+position+";left:"+std::to_string(inset)+"px;right:"+std::to_string(inset)+"px;width:"+keyword+"}");
   assert(s.layout()==AETHER_OK);assert(s.width()==expected);++checks;
  }
 }
 for(auto [css,expected]:std::initializer_list<std::pair<const char*,float>>{
  {"#p{position:relative}#a{position:absolute;left:0;right:0;width:min-content}",60},
  {"#p{position:relative}#a{position:absolute;left:120px;width:fit-content}",80},
  {"#p{position:relative}#a{position:absolute;right:120px;width:fit-content}",80},
  {"#p{position:relative}#a{position:absolute;left:150px;right:150px;width:fit-content}",60},
  {"#p{position:relative}#a{position:absolute;left:-10px;right:120px;width:fit-content}",90},
  {"#p{position:relative}#a{position:absolute;left:60px;right:60px;width:fit-content;margin-left:-10px}",90},
  {"#p{position:relative}#a{position:absolute;left:60px;right:60px;width:fit-content;margin-left:10px}",70},
  {"#p{position:relative}#a{position:absolute;left:40px;right:40px;width:fit-content;padding:10%}",120},
  {"#p{position:relative}#a{position:absolute;left:40px;right:40px;width:fit-content;padding:10%;box-sizing:border-box}",120},
  {"#p{position:relative}#a{position:absolute;left:60px;right:60px;width:10px;min-width:fit-content}",80},
  {"#p{position:relative}#a{position:absolute;left:60px;right:60px;width:200px;max-width:fit-content}",80},
  {"#p{position:relative}#a{position:absolute;left:60px;right:60px;width:auto}",80},
  {"#p{position:relative}#a{position:absolute;left:60px;right:60px;width:100px}",100},
  {"#p{position:relative;width:80px}#a{position:absolute;width:fit-content}",80},
  {"#p{position:relative;display:flex;width:80px;justify-content:center}#a{position:absolute;width:fit-content}",80},
  {"#p{position:relative;display:flex;width:80px;justify-content:flex-end}#a{position:absolute;width:fit-content}",80},
  {"#p{position:relative;display:flex;width:80px;flex-direction:row-reverse}#a{position:absolute;width:fit-content}",80}
 }){Scene s(css);assert(s.layout()==AETHER_OK);if(s.width()!=expected)std::cerr<<css<<" got "<<s.width()<<" want "<<expected<<"\n";assert(s.width()==expected);++checks;}
 {Scene s("#p{position:relative}#a{position:absolute;left:60px;right:60px;width:min-content;margin-left:auto;margin-right:auto}");
  assert(s.layout()==AETHER_OK&&s.width()==60);assert(aether_layout_result_find_primary_box(s.result,s.node("a"))->border_box.x==70);++checks;
 }
 size_t oom=0, fallbacks=0;bool complete=false;
 for(const char* css:{"#p{display:flex;width:80px}#a{flex:0 0 fit-content;min-width:min-content;max-width:max-content}",
                     "#p{position:relative}#a{position:absolute;left:60px;right:60px;width:fit-content;min-width:min-content;max-width:max-content}"}) {
  Scene s(css);complete=false;
  for(long n=0;n<5000;++n){delivered=false;budget=n;auto status=s.layout();budget=-1;
   if(status!=AETHER_OK){assert(status==AETHER_ERROR_OUT_OF_MEMORY&&s.result==nullptr);++oom;continue;}
   assert(s.width()==80);if(delivered){++fallbacks;aether_layout_result_destroy(s.result);s.result=nullptr;continue;}complete=true;break;
  }assert(complete);
 }
 std::cout<<"H8 layout OOM="<<oom<<" optional-allocation-fallback="<<fallbacks<<"\n";
 std::cout<<"H8 layout checks="<<checks<<"\n";
}
