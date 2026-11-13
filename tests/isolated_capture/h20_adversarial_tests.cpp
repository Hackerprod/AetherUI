#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/layout/layout_types.h"
#include <aether/aether_contextual_style.h>
#include "../../native/src/css/computed_style_handle.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
static long alloc_budget=-1; static bool injected=false;
void* operator new(size_t n){if(alloc_budget==0){injected=true;throw std::bad_alloc();}if(alloc_budget>0)--alloc_budget;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
static size_t checks=0;
void ck(bool b){if(!b)std::cerr<<"FAIL check "<<checks<<"\n";assert(b);++checks;}
static aether_text_metrics_t no_text(const char*,const aether_computed_style_t*,float,void*){return {};}
static bool near(float a,float b){return std::abs(a-b)<.01f;}
static const char* base="body{display:block;margin:0}div{display:block}#p{display:flex;width:160px;height:120px;gap:5px;align-items:flex-start}.item{width:30px;height:20px;flex-shrink:0;background:blue}";
static const char* html="<body><div id='p'><div id='a' class='item'></div><div id='b' class='item'></div><div id='c' class='item'></div></div></body>";
struct Scene {
 aether_doc_t doc{};aether_style_runtime_t rt{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t result{}; std::string css;
 Scene(std::string extra="",const char* h=html):css(std::string(base)+extra){ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,h)==AETHER_OK);new_runtime();ck(aether_layout_create(no_text,nullptr,&engine)==AETHER_OK);resolve();}
 void new_runtime(){aether_style_environment_t env{400,300,1,16,0,0,0};ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);int64_t sh{};int32_t nr{};ck(aether_style_runtime_add_stylesheet(rt,css.c_str(),1,"h18",&sh,&nr)==AETHER_OK);}
 void resolve(){aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(doc),&u);ck(n>=0);aether_style_updates_free(u,n);aether_style_map_destroy(map);map=nullptr;ck(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==AETHER_OK);aether_layout_result_destroy(result);result=nullptr;ck(aether_layout_layout(engine,doc,map,400,300,nullptr,nullptr,&result)==AETHER_OK);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}aether_layout_box_s* box(const char* id){return result->find_primary_box(node(id));}
 void fresh_equal(){std::unordered_map<aether_node_t,aether_rectf> old;for(auto* b:result->allocated_boxes)if(b->element)old.emplace(b->element,b->border_box);aether_style_runtime_destroy(rt);rt=nullptr;new_runtime();resolve();for(auto* b:result->allocated_boxes)if(b->element){ck(old.count(b->element));auto r=old.at(b->element);ck(near(r.x,b->border_box.x)&&near(r.y,b->border_box.y)&&near(r.width,b->border_box.width)&&near(r.height,b->border_box.height));}ck(old.size()==result->live_box_count);}
 ~Scene(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
};

// Explicit physical-side expectations, independent of flex-axis helper formulas.
int main(){
 for(auto dir:{"row","row-reverse","column","column-reverse"})for(auto side:{"left","right","top","bottom"}){
  Scene s(std::string("#p{direction:rtl;flex-direction:")+dir+"}.item{margin:0}#a{margin-"+side+":auto}");
  auto a=s.box("a");bool row=std::string(dir).find("row")==0,rev=std::string(dir).find("reverse")!=std::string::npos;float x=0,y=0;
  if(row){x=rev?0:130;if(std::string(side)=="left")x=rev?60:130;if(std::string(side)=="right")x=rev?0:70;if(std::string(side)=="top")y=100;}
  else {x=std::string(side)=="right"?0:130;y=rev?100:0;if(std::string(side)=="top")y=rev?100:50;if(std::string(side)=="bottom")y=rev?50:0;}
  if(!near(a->border_box.x,x)||!near(a->border_box.y,y)){std::cout<<"BAD "<<dir<<" "<<side<<" actual="<<a->border_box.x<<","<<a->border_box.y<<" expected="<<x<<","<<y<<"\n";return 1;}
 }
 {Scene s("#p{direction:rtl}.item{margin-left:-30px}#a{order:2}#b{order:1}#c{order:0}");for(auto id:{"a","b","c"})std::cout<<id<<":"<<s.box(id)->border_box.x<<" ";std::cout<<"\n";auto hit=aether_layout_result_hit_test(s.result,132,1);ck(hit==s.node("a"));}
 {Scene s("#p{direction:rtl}.item{margin-left:-30px}");ck(aether_layout_result_hit_test(s.result,132,1)==s.node("c"));}
 std::cout<<"adversarial pass "<<checks<<"\n";
}
