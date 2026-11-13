// T1.6 prerequisite regression, NOT a shaping or admitted-layout test.
#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/layout/layout_types.h"
#include <aether/aether_contextual_style.h>
#include <cassert>
#include <iostream>
using namespace aether::rendering::text;
struct NeverCalled final : MeasurementProvider {
 mutable unsigned calls=0;
 Measurement measure(std::string_view, std::shared_ptr<const FontInstance>, float) const override {
  ++calls; throw std::runtime_error("unproven context reached provider");
 }
};
static aether_text_metrics_t legacy(const char*, const aether_computed_style_t*, float, void*) { return {8,16,12}; }
int main() {
 unsigned checks=0;
 auto ck=[&](bool value){assert(value);++checks;};
 auto provider=std::make_shared<NeverCalled>();
 // Deliberately invalid font bytes still satisfy transport. They must never
 // become evidence that a resolved CSS family/face has been selected.
 auto font=std::make_shared<const FontInstance>(std::vector<uint8_t>{1,2},"claimed family",999,1024,0);
 auto selection=std::make_shared<const MeasurementSelection>(MeasurementMode::native,provider,font);
 const char* cases[]={"AV","A<!--boundary-->V","To","T<!--boundary-->o","ffi","f<!--boundary-->f<!--boundary-->i"," A <!--boundary-->  V ","   ","A\tV","A<br>V","<span>A</span>V","<span style='font-weight:bold'>A</span>V"};
 for(const char* content:cases) for(float width:{1.f,400.f}) {
  aether_doc_t doc{}; aether_style_runtime_t rt{}; aether_style_map_t map{};
  ck(aether_doc_create(&doc)==AETHER_OK);
  std::string html="<body><div style='white-space:nowrap;direction:ltr;writing-mode:horizontal-tb;font-size:16px'>";
  html+=content;html+="</div></body>";
  ck(aether_doc_parse_html(doc,html.c_str())==AETHER_OK);
  aether_style_environment_t env{400,300,1,16,0,0,0};
  ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);
  aether_style_update_t* updates{};
  int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(doc),&updates);ck(n>=0);
  aether_style_updates_free(updates,n);
  ck(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==AETHER_OK);
  aether_layout_engine_s engine;engine.text_measurer=legacy;
  aether_layout_result_t previous{};
  ck(aether_layout_layout(&engine,doc,map,width,300,nullptr,nullptr,&previous)==AETHER_OK);
  ck(previous!=nullptr);
  engine.text_measurer=nullptr;ck(engine.select_text_measurement(selection));
  auto output=previous;
  auto status=aether_layout_layout(&engine,doc,map,width,300,nullptr,nullptr,&output);
  // All-space input may have no text requiring measurement; otherwise no
  // candidate can be accepted merely because nowrap/direction were explicit.
  if(std::string(content)=="   " && status==AETHER_OK) {
   ck(provider->calls==0);if(output!=previous)aether_layout_result_destroy(output);
  } else {ck(status==AETHER_ERROR_INVALID_STATE);ck(output==previous);}
  ck(provider->calls==0);ck(!engine.layout_in_progress);
  aether_layout_result_destroy(previous);aether_style_map_destroy(map);
  aether_style_runtime_destroy(rt);aether_doc_destroy(doc);
 }
 std::cout<<"T1.6 prerequisite closure PASS checks="<<checks<<" provider_calls="<<provider->calls<<"; context-free native selection remains CLOSED\n";
}
