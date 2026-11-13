// TEST-ONLY double: no font parsing, contextual shaping or real rasterization.
#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/layout/layout_types.h"
#include <aether/aether_contextual_style.h>
#include "../../native/src/css/style_freeze.h"
#include <cassert>
#include <iostream>
using namespace aether::rendering::text;
static int checks=0,calls=0;
void ck(bool b){assert(b);++checks;}
static aether_text_metrics_t legacy(const char*,const aether_computed_style_t*,float,void*){++calls;return {7,16,12};}
struct TestOnlyProvider final:MeasurementProvider {
 mutable int calls=0; int fault=0; aether_node_t parent{},child{}; aether_layout_engine_s* engine=nullptr;
 Measurement measure(std::string_view s,std::shared_ptr<const FontInstance> f,float)const override{
  ++calls;
  if(engine) ck(!engine->select_text_measurement({}));
  if(fault==1)throw std::bad_alloc();
  if(fault==2)return {};
  if(fault==4)f=std::make_shared<const FontInstance>(f->source,f->face_index,f->size_26_6,f->raster_flags);
  if(fault==5)s="wrong source";
  std::vector<Glyph> glyphs;
  for(size_t i=0;i<s.size();++i)if(s[i]!=' ')glyphs.push_back({1,static_cast<uint32_t>(i),64,0,0,0});
  auto run=std::make_shared<const ShapedRun>(f,std::string(s),std::move(glyphs),aether_text_metrics_t{float(s.size()),16,12});
  auto m=run->metrics;if(fault==3)m.width+=1;
  if(fault==6)ck(aether_node_remove_child(parent,child)==AETHER_OK);
  return {m,run};
 }
};
int main(){
 bool invalid_mode=false;try{MeasurementSelection invalid(static_cast<MeasurementMode>(99));}catch(const std::invalid_argument&){invalid_mode=true;}ck(invalid_mode);
 auto raw=aether_computed_style_default();raw.font_size=16;
 aether::style::FrozenStyleRef frozen;ck(aether::style::freeze_style(raw,frozen)==AETHER_OK);
 BoxStyleSnapshot style(frozen);aether_layout_engine_s e;e.text_measurer=legacy;
 ck(engine_measure_text_transport(&e,"x",&style,0).metrics.width==7&&calls==1);
 e.text_measurer=nullptr;
 ck(!engine_measure_text_transport(&e,"x",&style,0).run&&engine_measure_text_transport(&e,"x",&style,0).metrics.width==0);
 e.text_measurer=legacy;
 auto font=std::make_shared<const FontInstance>(std::vector<uint8_t>{1,2},"TEST ONLY",0,1024,0);
 auto p=std::make_shared<TestOnlyProvider>();
 auto selection=std::make_shared<const MeasurementSelection>(MeasurementMode::native,p,font);
 ck(e.select_text_measurement(selection));
 auto rejects=[&](const char* text="x"){bool bad=false;try{engine_measure_text_transport(&e,text,&style,0);}catch(const std::invalid_argument&){bad=true;}ck(bad);};
 rejects();ck(calls==1);e.text_measurer=nullptr;
 auto m=engine_measure_text_transport(&e,"x",&style,0);ck(m.run&&m.metrics.width==1);
 ck(!engine_measure_text_transport(&e,"",&style,0).run&&p->calls==1);
 auto space=engine_measure_text_transport(&e,"  ",&style,0);ck(space.run->glyphs.empty()&&space.metrics.width==2);
 style.font_size=17;rejects();style.font_size=16;
 std::string oversized(max_text_bytes+1,'x');rejects(oversized.c_str());
 ck(e.select_text_measurement(std::make_shared<const MeasurementSelection>(MeasurementMode::legacy,p,font)));rejects();
 ck(e.select_text_measurement(selection));
 for(int fault:{2,3,4,5}){p->fault=fault;rejects();}p->fault=0;
 ck(e.select_text_measurement(std::make_shared<const MeasurementSelection>(MeasurementMode::native)));rejects();
 ck(e.select_text_measurement(selection));
 // Public layout transaction: failure keeps prior output; nested setter fails.
 aether_doc_t doc{};aether_style_runtime_t rt{};aether_style_map_t map{};
 ck(aether_doc_create(&doc)==AETHER_OK);
 ck(aether_doc_parse_html(doc,"<body><div id='parent' style='display:flex'><span id='child'>x x</span></div></body>")==AETHER_OK);
 aether_style_environment_t env{400,300,1,16,0,0,0};
 ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);
 aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(doc),&u);ck(n>=0);aether_style_updates_free(u,n);
 ck(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==AETHER_OK);
 aether_layout_result_t result{};p->engine=&e;
 ck(aether_layout_layout(&e,doc,map,400,300,nullptr,nullptr,&result)==AETHER_OK);
 ck(result&&p->calls>2);
 auto previous=result;
 for(int fault:{1,2,3,4,5}){p->fault=fault;ck(aether_layout_layout(&e,doc,map,400,300,nullptr,nullptr,&result)==(fault==1?AETHER_ERROR_OUT_OF_MEMORY:AETHER_ERROR_INVALID_STATE));ck(result==previous&&!e.layout_in_progress);}
 p->fault=0;
 ck(e.select_text_measurement(std::make_shared<const MeasurementSelection>(MeasurementMode::native)));
 ck(aether_layout_layout(&e,doc,map,400,300,nullptr,nullptr,&result)==AETHER_ERROR_INVALID_STATE&&result==previous);
 ck(e.select_text_measurement(selection));p->fault=6;
 p->parent=aether_doc_get_element_by_id(doc,"parent");p->child=aether_doc_get_element_by_id(doc,"child");
 ck(aether_layout_layout(&e,doc,map,400,300,nullptr,nullptr,&result)==AETHER_ERROR_INVALID_STATE&&result==previous);
 // Config/provider destruction cannot retire retained run/font/source.
 e.select_text_measurement({});selection.reset();p.reset();font.reset();ck(m.run->font->bytes.size()==2&&m.run->source=="x");
 aether_layout_result_destroy(result);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);
 std::cout<<"T1.4 TEST-ONLY provider PASS checks="<<checks<<" real shaping NOT_RUN\n";
}
