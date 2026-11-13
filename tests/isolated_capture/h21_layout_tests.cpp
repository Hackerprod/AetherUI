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
static size_t checks=0; static std::string current_case;
void ck(bool b){if(!b)std::cerr<<"FAIL check "<<checks<<" "<<current_case<<"\n";assert(b);++checks;}
static aether_text_metrics_t no_text(const char*,const aether_computed_style_t*,float,void*){return {};}
static bool near(float a,float b){return std::abs(a-b)<.01f;}
static const char* base="body{display:block;margin:0}div{display:block}#p{display:flex;width:160px;height:120px;gap:5px;align-items:flex-start}.item{width:30px;height:20px;flex-shrink:0;background:blue}";
static const char* html="<body><div id='p'><div id='a' class='item'></div><div id='b' class='item'></div><div id='c' class='item'></div></div></body>";
struct Scene {
 aether_doc_t doc{};aether_style_runtime_t rt{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t result{}; std::string css;
 Scene(std::string extra="",const char* h=html):css(std::string(base)+extra){ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,h)==AETHER_OK);new_runtime();ck(aether_layout_create(no_text,nullptr,&engine)==AETHER_OK);resolve();}
 void new_runtime(){aether_style_environment_t env{400,300,1,16,0,0,0};ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);int64_t sh{};int32_t nr{};ck(aether_style_runtime_add_stylesheet(rt,css.c_str(),1,"h21",&sh,&nr)==AETHER_OK);}
 void resolve(){aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(doc),&u);ck(n>=0);aether_style_updates_free(u,n);aether_style_map_destroy(map);map=nullptr;ck(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==AETHER_OK);aether_layout_result_destroy(result);result=nullptr;ck(aether_layout_layout(engine,doc,map,400,300,nullptr,nullptr,&result)==AETHER_OK);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}aether_layout_box_s* box(const char* id){return result->find_primary_box(node(id));}
 void fresh_equal(){std::unordered_map<aether_node_t,aether_rectf> old;for(auto* b:result->allocated_boxes)if(b->element)old.emplace(b->element,b->border_box);aether_style_runtime_destroy(rt);rt=nullptr;new_runtime();resolve();for(auto* b:result->allocated_boxes)if(b->element){ck(old.count(b->element));auto r=old.at(b->element);ck(near(r.x,b->border_box.x)&&near(r.y,b->border_box.y)&&near(r.width,b->border_box.width)&&near(r.height,b->border_box.height));}ck(old.size()==result->live_box_count);}
 ~Scene(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
};


// Expected coordinates deliberately use a declarative physical-axis table,
// never flex_axis or any other production axis/position helper.
struct ExpectedAxis { bool x; bool reverse; bool cross_reverse; };
static ExpectedAxis expected(int mode, int direction, bool rtl) {
 static constexpr ExpectedAxis ltr[3][4]={
  {{true,false,false},{true,true,false},{false,false,false},{false,true,false}},
  {{false,false,true},{false,true,true},{true,true,false},{true,false,false}},
  {{false,false,false},{false,true,false},{true,false,false},{true,true,false}}
 };
 auto a=ltr[mode][direction];
 if(rtl) { if(direction<2)a.reverse=!a.reverse;else a.cross_reverse=!a.cross_reverse; }
 return a;
}
int main(){
 const char* modes[]={"horizontal-tb","vertical-rl","vertical-lr"};
 const char* dirs[]={"row","row-reverse","column","column-reverse"};
 size_t scenes=0;
 for(int mode=0;mode<3;++mode)for(bool rtl:{false,true})for(int dir=0;dir<4;++dir)
 for(bool overflow:{false,true})for(int justify=0;justify<5;++justify)for(int align=0;align<5;++align){
  const char* positions[]={"flex-start","center","flex-end","start","end"};
  const float w=overflow?90:260,h=overflow?70:210;
  std::string css="#p{writing-mode:"+std::string(modes[mode])+";direction:"+(rtl?"rtl":"ltr")+";flex-direction:"+dirs[dir]+";width:"+std::to_string(w)+"px;height:"+std::to_string(h)+"px;row-gap:7px;column-gap:13px;padding:7px 9px 13px 11px;border-top:1px solid red;border-right:2px solid red;border-bottom:3px solid red;border-left:4px solid red;justify-content:"+positions[justify]+";align-items:"+positions[align]+"}.item{margin:2px 3px 4px 5px;padding:3px 5px 7px 11px;border-top:1px solid red;border-right:2px solid red;border-bottom:3px solid red;border-left:4px solid red}";
  current_case=std::string(modes[mode])+" "+dirs[dir]+(rtl?" RTL":" LTR")+" overflow="+std::to_string(overflow)+" justify="+positions[justify]+" align="+positions[align];
  Scene s(css);auto a=expected(mode,dir,rtl);++scenes;
  // Physical content size 30x20 + padding 16x10 + border 6x4;
  // physical margin box 60x40. Parent content origin = (15,8).
  const float outer=a.x?60:40,crossOuter=a.x?40:60;
  const float extent=a.x?w:h,crossExtent=a.x?h:w;
  const float gap=dir<2?13:7;
  const float free=extent-3*outer-2*gap;
  const bool reversedFlex=dir==1||dir==3;
  const float begin=justify==0?0:justify==1?free/2:justify==2?free:justify==3?(reversedFlex?free:0):(reversedFlex?0:free);
  const float cf=crossExtent-crossOuter;
  float cross=(align==0||align==3)?0:align==1?cf/2:cf;
  if(a.cross_reverse)cross=crossExtent-cross-crossOuter;
  int i=0;for(auto id:{"a","b","c"}){
   float main=begin+i*(outer+gap);if(a.reverse)main=extent-main-outer;
   auto b=s.box(id);ck(b!=nullptr);
   ck(near(b->border_box.x,15+(a.x?main:cross)+5));
   ck(near(b->border_box.y,8+(a.x?cross:main)+2));
   ck(near(b->border_box.width,52));ck(near(b->border_box.height,34));++i;
  }
 }
 // Basis, growing and shrinking act on the physical main dimension, while
 // row/column select gaps independently of the physical axis.
 for(int mode=0;mode<3;++mode)for(bool rtl:{false,true})for(int dir=0;dir<4;++dir)for(bool shrink:{false,true}){
  current_case=std::string(modes[mode])+" "+dirs[dir]+(rtl?" RTL":" LTR")+(shrink?" shrink":" grow");
  Scene s("#p{writing-mode:"+std::string(modes[mode])+";direction:"+(rtl?"rtl":"ltr")+";flex-direction:"+dirs[dir]+";row-gap:7px;column-gap:13px}.item{min-width:0px;min-height:0px;flex-basis:"+(shrink?"80px;flex-grow:0;flex-shrink:1":"20px;flex-grow:1;flex-shrink:0")+"}");
  auto a=expected(mode,dir,rtl);float extent=a.x?160:120,gap=dir<2?13:7,size=(extent-2*gap)/3;
  int i=0;for(auto id:{"a","b","c"}){auto b=s.box(id);ck(near(a.x?b->border_box.width:b->border_box.height,size));ck(near(a.x?b->border_box.height:b->border_box.width,a.x?20:30));float pos=i*(size+gap);if(a.reverse)pos=extent-pos-size;ck(near(a.x?b->border_box.x:b->border_box.y,pos));++i;}
 }
 // Changing an inherited mode must agree with a fresh runtime, not retain an
 // axis from the preceding snapshot.
 {Scene s;for(auto mode:modes){ck(aether_element_set_attribute(aether_doc_get_body(s.doc),"style",("writing-mode:"+std::string(mode)).c_str())==AETHER_OK);s.resolve();s.fresh_equal();auto b=s.box("b");bool vertical=std::string(mode)!="horizontal-tb";ck(near(b->border_box.x,vertical?(std::string(mode)=="vertical-rl"?130:0):35));ck(near(b->border_box.y,vertical?25:0));}}

 size_t failures=0,fallbacks=0;
 for(int mode=1;mode<3;++mode){
  Scene s("#p{writing-mode:"+std::string(modes[mode])+";direction:rtl}");
  auto old=s.result;s.result=nullptr;bool completed=false;float x=mode==1?130:0;
  for(long budget=0;budget<2000;++budget){
   aether_layout_result_t next{};injected=false;alloc_budget=budget;
   auto status=aether_layout_layout(s.engine,s.doc,s.map,400,300,nullptr,nullptr,&next);alloc_budget=-1;
   auto oldBox=old->find_primary_box(s.node("a"));ck(near(oldBox->border_box.x,x));ck(near(oldBox->border_box.y,100));
   if(status==AETHER_OK){auto b=next->find_primary_box(s.node("a"));ck(near(b->border_box.x,x)&&near(b->border_box.y,100));aether_layout_result_destroy(next);if(!injected){completed=true;break;}++fallbacks;}
   else{ck(status==AETHER_ERROR_OUT_OF_MEMORY&&next==nullptr);++failures;}
  }
  aether_layout_result_destroy(old);ck(completed);
 }
 std::cout<<"H21 layout checks="<<checks<<" coordinate_scenes="<<scenes<<" OOM="<<failures<<" fallback="<<fallbacks<<"\n";
}
