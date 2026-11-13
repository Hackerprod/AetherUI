// Mechanical transport fixtures, NOT real font/shaping validation.
#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/layout/layout_types.h"
#include "../../native/src/css/style_freeze.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace aether::rendering::text;
static int calls=0,checks=0;
static void ck(bool b){assert(b);++checks;}
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){++calls;return {33,44,22};}
int main(){
 aether_layout_engine_s engine;engine.text_measurer=measure;
 aether_layout_box_s fragment;fragment.kind=LAYOUT_BOX_TEXT;fragment.text="x";
 auto raw=aether_computed_style_default(); raw.font_size=12; aether::style::FrozenStyleRef frozen; ck(aether::style::freeze_style(raw,frozen)==AETHER_OK); fragment.style=BoxStyleSnapshot(frozen);
 auto legacy=layout_fragment_metrics(&engine,fragment);ck(calls==1&&legacy.width==33&&legacy.baseline==22);
 auto font=std::make_shared<const FontInstance>(std::vector<std::uint8_t>{1},"unverified-fixture",0,768,0);
 for(float baseline:{0.f,9.f,12.f}){
  auto run=std::make_shared<const ShapedRun>(font,"x",std::vector<Glyph>{{1,0,64,0,0,0}},aether_text_metrics_t{10,12,baseline});
  fragment.native_text={run,baseline};
  auto m=layout_fragment_metrics(&engine,fragment);ck(m.width==10&&m.height==12&&m.baseline==baseline&&calls==1);
  fragment.content_box.y=-31;ck(layout_fragment_metrics(&engine,fragment).baseline==baseline);
 }
 auto reject=[&](){bool rejected=false;try{(void)layout_fragment_metrics(&engine,fragment);}catch(const std::invalid_argument&){rejected=true;}ck(rejected&&calls==1);};
 fragment.native_text.baseline=std::numeric_limits<float>::quiet_NaN();reject();
 fragment.native_text.baseline=std::numeric_limits<float>::infinity();reject();
 fragment.native_text.baseline=11;reject();fragment.native_text.baseline=12;
 fragment.text="y";reject();fragment.text="x";
 fragment.style.font_size=13;reject();fragment.style.font_size=std::numeric_limits<float>::infinity();reject();fragment.style.font_size=12;
 auto copy=fragment.native_text;fragment.native_text={};
 ck(layout_fragment_metrics(&engine,fragment).baseline==22&&calls==2);
 fragment.native_text=copy;ck(layout_fragment_metrics(&engine,fragment).baseline==12&&calls==2);
 std::cout<<"T1.2 fragment metrics PASS checks="<<checks<<" callback_calls="<<calls<<"\n";
}
