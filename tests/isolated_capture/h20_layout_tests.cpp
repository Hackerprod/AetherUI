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
 void new_runtime(){aether_style_environment_t env{400,300,1,16,0,0,0};ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);int64_t sh{};int32_t nr{};ck(aether_style_runtime_add_stylesheet(rt,css.c_str(),1,"h20",&sh,&nr)==AETHER_OK);}
 void resolve(){aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(doc),&u);ck(n>=0);aether_style_updates_free(u,n);aether_style_map_destroy(map);map=nullptr;ck(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==AETHER_OK);aether_layout_result_destroy(result);result=nullptr;ck(aether_layout_layout(engine,doc,map,400,300,nullptr,nullptr,&result)==AETHER_OK);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}aether_layout_box_s* box(const char* id){return result->find_primary_box(node(id));}
 void fresh_equal(){std::unordered_map<aether_node_t,aether_rectf> old;for(auto* b:result->allocated_boxes)if(b->element)old.emplace(b->element,b->border_box);aether_style_runtime_destroy(rt);rt=nullptr;new_runtime();resolve();for(auto* b:result->allocated_boxes)if(b->element){ck(old.count(b->element));auto r=old.at(b->element);ck(near(r.x,b->border_box.x)&&near(r.y,b->border_box.y)&&near(r.width,b->border_box.width)&&near(r.height,b->border_box.height));}ck(old.size()==result->live_box_count);}
 ~Scene(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
};

int main(){
 for(bool rtl:{false,true}) for(auto dir:{"row","row-reverse","column","column-reverse"}) for(auto justify:{"flex-start","flex-end","start","end","center","space-between"}) for(auto align:{"flex-start","flex-end","start","end","center"}) {
  bool row=std::string(dir).find("row")==0, rev=std::string(dir).find("reverse")!=std::string::npos;
  Scene s(std::string("#p{direction:")+(rtl?"rtl":"ltr")+";flex-direction:"+dir+";justify-content:"+justify+";align-items:"+align+"}");
  float extent=row?160:120, size=row?30:20, crossExtent=row?120:160, crossSize=row?20:30;
  float free=extent-(3*size+10), start=0, spacing=5;
  std::string j=justify,al=align;
  if(j=="flex-end")start=free;else if(j=="center")start=free/2;else if(j=="start")start=rev?free:0;else if(j=="end")start=rev?0:free;else if(j=="space-between")spacing+=free/2;
  float cross=0;if(al=="end"||al=="flex-end")cross=crossExtent-crossSize;else if(al=="center")cross=(crossExtent-crossSize)/2;
  if(!row&&rtl)cross=crossExtent-cross-crossSize;
  bool physicalReverse=rev!=(row&&rtl);int i=0;
  for(auto id:{"a","b","c"}){float main=start+i*(size+spacing);if(physicalReverse)main=extent-main-size;auto b=s.box(id);ck(near(b->border_box.x,row?main:cross));ck(near(b->border_box.y,row?cross:main));ck(aether_layout_result_hit_test(s.result,b->border_box.x+1,b->border_box.y+1)==s.node(id));++i;}
 }
 // Independent wrapping line coordinates (stretch allocates 60px per line).
 for(bool rtl:{false,true})for(auto dir:{"row","row-reverse","column","column-reverse"}){
  bool row=std::string(dir).find("row")==0,rev=std::string(dir).find("reverse")!=std::string::npos;
  Scene s(std::string("#p{direction:")+(rtl?"rtl":"ltr")+";flex-direction:"+dir+";flex-wrap:wrap;width:80px;height:50px}");
  for(int i=0;i<3;++i){auto b=s.box(i==0?"a":i==1?"b":"c");float main=(i%2)*(row?35:25);if(rev!=(row&&rtl))main=(row?80:50)-main-(row?30:20);float cross=(i/2)*(row?27.5f:42.5f);if(!row&&rtl)cross=80-cross-30;ck(near(b->border_box.x,row?main:cross));ck(near(b->border_box.y,row?cross:main));}
 }
 {Scene s("#p{direction:rtl;padding:7px 11px;border:3px solid red}.item{margin:2px 3px 4px 5px}");ck(near(s.box("a")->border_box.x,14+160-3-30));ck(near(s.box("a")->border_box.y,12));}
 {Scene s("#p{direction:rtl}#a{margin-left:auto}");ck(near(s.box("a")->border_box.x,130));ck(near(s.box("b")->border_box.x,35));ck(near(s.box("c")->border_box.x,0));}
 {Scene s("#p{direction:rtl;flex-direction:column}#a{margin-left:auto}");ck(near(s.box("a")->border_box.x,130));}
 {Scene s("#p{direction:rtl}#a{order:1}#c{order:-1}");ck(near(s.box("c")->border_box.x,130));ck(near(s.box("b")->border_box.x,95));ck(near(s.box("a")->border_box.x,60));}
 {Scene s;for(auto d:{"rtl","ltr","rtl"}){ck(aether_element_set_attribute(s.node("p"),"style",(std::string("direction:")+d).c_str())==AETHER_OK);s.resolve();s.fresh_equal();ck(near(s.box("a")->border_box.x,std::string(d)=="rtl"?130:0));}}
 {Scene s("body{direction:rtl}#p{direction:unset}");ck(near(s.box("a")->border_box.x,130));}
 {Scene s("body{direction:rtl}#p{direction:initial}");ck(near(s.box("a")->border_box.x,0));}
 {Scene s("body{direction:rtl}#p{direction:inherit}");ck(near(s.box("a")->border_box.x,130));}
 {Scene s("#p{--d:rtl;direction:var(--d)}");ck(near(s.box("a")->border_box.x,130));}

 // Negative main free space is reflected around the container, not clamped.
 for(auto dir:{"row","row-reverse"})for(auto just:{"flex-start","start","end"}){
 Scene s(std::string("#p{direction:rtl;width:70px;flex-direction:")+dir+";justify-content:"+just+"}");
 bool rev=std::string(dir)=="row-reverse";float dist=std::string(just)=="start"?(rev?-30:0):(std::string(just)=="end"?(rev?0:-30):0);float want=rev?dist:70-dist-30;ck(near(s.box("a")->border_box.x,want));}
 {Scene s("#p{direction:rtl}#a{display:flex;width:60px;height:40px;direction:ltr}#b{direction:ltr}","<body><div id='p'><div id='a' class='item'><div id='inside' style='display:block;width:10px;height:10px'></div></div><div id='b' class='item'></div></div></body>");ck(near(s.box("a")->border_box.x,100));ck(near(s.box("inside")->border_box.x,100));}
 // Prior result remains usable across every mandatory failure and optional sort fallback.
 size_t failures=0,fallbacks=0;bool completed=false;
 {Scene s("#p{direction:rtl}");auto old=s.result;s.result=nullptr;
 for(long n=0;n<2000;++n){aether_layout_result_t next{};injected=false;alloc_budget=n;auto status=aether_layout_layout(s.engine,s.doc,s.map,400,300,nullptr,nullptr,&next);alloc_budget=-1;ck(near(old->find_primary_box(s.node("a"))->border_box.x,130));
 if(status==AETHER_OK){ck(near(next->find_primary_box(s.node("a"))->border_box.x,130));aether_layout_result_destroy(next);if(!injected){completed=true;break;}++fallbacks;}
 else {ck(status==AETHER_ERROR_OUT_OF_MEMORY&&next==nullptr);++failures;}}
 aether_layout_result_destroy(old);ck(completed&&failures);}
 // Bound-domain stress exercises arithmetic rather than only lexical parsing.
 for(auto dir:{"row","row-reverse","column","column-reverse"})for(auto factor:{"65536","0.000001"}) {
  Scene s(std::string("#p{direction:rtl;width:65536px;height:65536px;gap:65536px;flex-direction:")+dir+"}.item{width:65536px;height:65536px;margin:65536px -65536px;padding:1px;flex-grow:"+factor+";flex-shrink:"+factor+"}");
  for(auto* b:s.result->allocated_boxes)for(float value:{b->border_box.x,b->border_box.y,b->border_box.width,b->border_box.height})ck(std::isfinite(value)&&std::abs(value)<2147483647.);
  s.fresh_equal();
 }
 std::cout<<"H20 layout checks="<<checks<<" OOM="<<failures<<" fallback="<<fallbacks<<"\n";
}
