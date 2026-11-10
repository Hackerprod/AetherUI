#include "../src/layout/layout_internal.h"
#define AETHER_INLINE_UNITS_FIXTURE_ONLY
#include "test_inline_units.cpp"
#undef AETHER_INLINE_UNITS_FIXTURE_ONLY
static float intrinsic(Fixture& f,bool height,aether_intrinsic_size_mode_e mode,float width){
 aether_layout_result_s provisional;provisional.build_overlay=std::make_unique<aether::layout::MutableLayoutOverlay>();
 const aether::layout::StyleLookup lookup(f.map->current,provisional.build_overlay.get(),f.result->assets->default_style);
 auto n=node(f,"host");const BoxStyleSnapshot style(lookup.lookup_node_owner(n));
 f.engine->measure_styles=&lookup;f.engine->measure_document=f.doc->identity;f.meter.calls.clear();
 float v=height?measure_intrinsic_height(f.engine,&lookup,&provisional,n,style,{},{},width):
 measure_intrinsic_width(f.engine,&lookup,n,style,{},{},width,&provisional,mode);
 f.engine->measure_styles=nullptr;f.engine->measure_document.reset();
 check(provisional.primary_boxes.empty(),"intrinsic creates no primary boxes");check(provisional.paint_order.empty(),"intrinsic creates no paint");
 return v;
}
int main(){
 using M=aether_intrinsic_size_mode_e;
 struct Case{const char*body;float normal_min,normal_max,pre_min,pre_max;};
 for(const char*mode:{"normal","pre-line","nowrap","pre"})for(auto c:std::vector<Case>{
  {"<span>aa</span><span>bb</span>",40,40,40,40},
  {"<span>aa</span> <span>bb</span>",20,50,50,50},
  {"<span>aa</span><!--comment--><span></span><span><span>bb</span></span>",40,40,40,40},
  {"  <span>aa</span><span>  </span><span>bb</span>  ",20,50,100,100},
  {"<span> </span><span> </span>",0,0,20,20},
  {"<span>aa</span><br><span>bb</span>",20,20,20,20}
 }){
  scenario=std::string("intrinsic ")+mode+"/"+c.body;Fixture f(c.body,mode_css(mode));
  const bool pre=std::string(mode)=="pre",nowrap=std::string(mode)=="nowrap";
  for(float width:{1.f,30.f,500.f}){
   near(intrinsic(f,false,M::min_content,width),pre?c.pre_min:nowrap?c.normal_max:c.normal_min,"min-content independent available width");
   near(intrinsic(f,false,M::max_content,width),pre?c.pre_max:c.normal_max,"max-content independent available width");
  }
 }
 for(const char*mode:{"normal","pre-line","nowrap","pre"}){
  scenario=std::string("intrinsic split CRLF ")+mode;
  Fixture f("<span>aa\r</span><span></span><span>\nbb</span>",mode_css(mode,100));
  bool forced=std::string(mode)=="pre"||std::string(mode)=="pre-line";
  near(intrinsic(f,false,M::min_content,1),forced?20:std::string(mode)=="nowrap"?50:20,"CRLF min");
  near(intrinsic(f,false,M::max_content,1),forced?20:50,"CRLF max");
  near(intrinsic(f,true,M::max_content,100),forced?40:20,"height CRLF once");
  Fixture joined("<span>abc</span><span>def</span>",mode_css(mode));
  near(intrinsic(joined,true,M::max_content,50),20,"intrinsic overflow word stays intact");
 }
 std::printf("inline-units-intrinsic: %d checks, %d failures\n",checks,failures);return failures?1:0;
}

