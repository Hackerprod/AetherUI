#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/layout/layout_types.h"
#include <aether/aether_contextual_style.h>
#include "../../native/src/css/frozen_measure_read.h"
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
static aether_text_metrics_t no_text(const char*,const aether_computed_style_t*,float,void*){return {};}
static bool near(float a,float b){return std::abs(a-b)<.01f;}
struct Scene {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t styles{};aether_layout_engine_t engine{};aether_layout_result_t result{};
 Scene(const char* html,const std::string& css,float width,float height=400){
  assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,html)==AETHER_OK);
  aether_style_runtime_options_t options{};aether_style_environment_t env{width,height,1,16,0,0,0};
  assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
  int64_t sheet{};int32_t rules{};assert(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"h4",&sheet,&rules)==AETHER_OK);
  aether_style_update_t* updates{};int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);
  assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);
  assert(aether_layout_create(no_text,nullptr,&engine)==AETHER_OK);
  // Frozen styles must retain contextual owners independently of runtime.
  aether_style_runtime_destroy(runtime);runtime=nullptr;
  assert(aether_layout_layout(engine,doc,styles,width,height,nullptr,nullptr,&result)==AETHER_OK);
 }
 aether_layout_box_s* box(const char* id){auto n=aether_doc_get_element_by_id(doc,id);for(auto* b:result->allocated_boxes)if(b->element==n)return b;return nullptr;}
 ~Scene(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_doc_destroy(doc);}
};
int main(){size_t checks=0;
 const char* html="<body><div id='outer'><div id='p'><div id='a'></div><div id='b'></div><div id='c'></div><div id='d'></div></div></div></body>";
 const std::string base="body{margin:0}div{display:block}#outer{width:500px;height:400px}#p{display:flex;width:360px;flex-wrap:wrap;align-content:flex-start;align-items:flex-start}#a,#b,#c,#d{flex:none;width:110px;height:20px}";
 auto same=[&](const std::string& css,const std::string& control){
  Scene a(html,base+css,800),b(html,base+control,800);
  for(auto id:{"p","a","b","c","d"}){auto x=a.box(id)->border_box,y=b.box(id)->border_box;
   if(!near(x.x,y.x)||!near(x.y,y.y)||!near(x.width,y.width)||!near(x.height,y.height)){
    std::cerr<<"Mismatch "<<id<<" "<<css<<" vs "<<control<<" got "<<x.x<<","<<x.y<<","<<x.width<<","<<x.height<<" expected "<<y.x<<","<<y.y<<","<<y.width<<","<<y.height<<"\n";std::abort();}
  }++checks;
 };
 for(auto dir:{"row","row-reverse","column","column-reverse"})for(auto wrap:{"wrap","nowrap"})for(bool fixed:{false,true}){
  std::string prefix=std::string("#p{flex-direction:")+dir+";flex-wrap:"+wrap+";"+(fixed?"height:100px;":"")+"}";
  same(prefix+"#p{gap:calc(10px + 2%)}",prefix+"#p{gap:"+(fixed?std::string("12px"):std::string("10px"))+" 17.2px}");
  same(prefix+"#p{gap:calc(3px + 5%) calc(10px + 2%)}",prefix+"#p{gap:"+(fixed?std::string("8px"):std::string("3px"))+" 17.2px}");
 }
 for(auto size:{"", "min-height:200px;", "height:auto;max-height:200px;"})same(std::string("#p{")+size+"gap:calc(10px + 2%)}",std::string("#p{")+size+"gap:10px 17.2px}");
 same("#p{height:50%;gap:calc(10px + 2%)}","#p{height:50%;gap:14px 17.2px}");
 same("#outer{height:auto}#p{height:50%;gap:calc(10px + 2%)}","#outer{height:auto}#p{height:50%;gap:10px 17.2px}");
 same("#p{height:100px;padding:10px;border:5px solid red;gap:calc(10px + 2%)}","#p{height:100px;padding:10px;border:5px solid red;gap:12px 17.2px}");
 same("#p{height:130px;width:390px;box-sizing:border-box;padding:10px;border:5px solid red;gap:calc(10px + 2%)}","#p{height:130px;width:390px;box-sizing:border-box;padding:10px;border:5px solid red;gap:12px 17.2px}");
 for(auto width:{"min-content","max-content"}){
  // Intrinsic contributions use the absolute term. Final horizontal gap is
  // resolved against the resulting content width (percentage may overflow).
  const float w=std::string(width)=="min-content"?110.f:470.f;
  same(std::string("#p{width:")+width+";gap:calc(10px + 2%)}", "#p{width:"+std::to_string(w)+"px;gap:10px "+std::to_string(10+w*.02f)+"px}");
 }
 for(auto value:{"calc(10px + 2%)", "CALC(10px + 2%)", "calc(10px /*x*/ + 2%)", "calc((20px + 4%) / 2)", "calc(5px * 2 + 2%)"}) same(std::string("#p{gap:")+value+"}","#p{gap:10px 17.2px}");
 same(R"(#p{g\61 p:c\61 lc(10px + 2%)})","#p{gap:10px 17.2px}");
 same(R"(#p{gap:calc(10p\78  + 2%)})","#p{gap:10px 17.2px}");
 same("#p{gap:calc(-10px + 2%)}","#p{gap:0}");
 same("#p{--g:calc(10px + 2%);gap:var(--g)}","#p{gap:10px 17.2px}");
 same("#p{--g:calc(10px + 2%);gap:var(--g);column-gap:7px}","#p{gap:10px 7px}");
 for(auto bad:{"-1px", "calc(10px + 2%) garbage", "calc(10px + 2%) -1px", "calc(10px + 2%) 3px 4px", "calc(1px / 0)", "calc(1px+2%)", "calc(1e999px + 2%)"})same(std::string("#p{gap:3px 7px;gap:")+bad+"}","#p{gap:3px 7px}");
 same(R"(#p{gap:3px 7px;gap:calc(1\65 3px + 2%)})","#p{gap:3px 7px}");
 for(auto kw:{"initial","unset","revert"})same(std::string("#p{gap:calc(10px + 2%);gap:")+kw+"}","#p{gap:0}");
 same("#outer{gap:calc(10px + 2%)}#p{gap:inherit}","#p{gap:10px 17.2px}");
 same("#p{gap:calc(10px + 2%)!important;gap:1px}","#p{gap:10px 17.2px}");
 same("#p{gap:calc(10px + 2%);column-gap:calc(1px + 1%)}","#p{gap:10px 4.6px}");
 same("#p{width:237.2px;gap:calc(10px + 2%)}","#p{width:237.2px;gap:10px 14.744px}");
 same("#p{width:234.744px;gap:calc(10px + 2%)}","#p{width:234.744px;gap:10px 14.69488px}");
 same("#a,#b,#c,#d{margin:2px 3px}#p{gap:calc(10px + 2%)}","#a,#b,#c,#d{margin:2px 3px}#p{gap:10px 17.2px}");
 std::cout<<"H9 native geometry checks="<<checks<<"\n";
}
