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
int main(){
 for(auto [basis,want]:{std::pair{1000.f,24.f},{1228.f,24.f},{1280.f,50.f},{1600.f,210.f}}){
  Scene s("<body><div id='x'></div></body>","body{margin:0}div,aside{display:block}#x{height:20px;padding:28px max(24px,calc((100% - 1180px)/2))}",basis);
  auto* b=s.box("x");assert(b);assert(near(b->content_box.x-b->border_box.x,want));assert(near(b->content_box.y-b->border_box.y,28));
  auto handle=aether_style_map_get(s.styles,aether_doc_get_element_by_id(s.doc,"x"));assert(handle);
  aether_computed_style_t sentinel{},before{};std::memset(&sentinel,0x5a,sizeof sentinel);before=sentinel;
  assert(aether_computed_style_copy(handle,&sentinel)==AETHER_ERROR_INVALID_STATE);assert(std::memcmp(&before,&sentinel,sizeof sentinel)==0);
  aether_computed_style_handle_t clone{};assert(aether_computed_style_handle_clone(handle,&clone)==AETHER_OK);aether_computed_style_handle_destroy(handle);
  assert(aether_computed_style_copy(clone,&sentinel)==AETHER_ERROR_INVALID_STATE);aether_computed_style_handle_destroy(clone);
 }
 for(auto [basis,want]:{std::pair{200.f,80.f},{500.f,150.f},{800.f,180.f}}){
  Scene s("<body><div id='p'><aside id='x'></aside></div></body>","body{margin:0}div,aside{display:block}#p{display:flex;width:"+std::to_string(basis)+"px;height:60px}#x{flex:0 1 clamp(80px,30%,180px);height:20px}",1000);
  assert(s.box("x"));assert(near(s.box("x")->border_box.width,want));
 }
 {Scene s("<body><div id='p'><aside id='x'></aside></div></body>","body{margin:0}div,aside{display:block}#p{width:500px}#x{height:10px;padding:max(24px,10%)}",1600);auto*b=s.box("x");assert(near(b->content_box.x-b->border_box.x,50));assert(near(b->content_box.y-b->border_box.y,50));}
 {Scene s("<body><div id='x'></div></body>","body{margin:0}div,aside{display:block}#x{padding:clamp(180px,100px,80px);height:1px}",800);auto*b=s.box("x");assert(near(b->content_box.x-b->border_box.x,180));}
 for(const char* replace:{"padding:7px","padding:initial","padding:unset"}){Scene s("<body><div id='x'></div></body>",std::string("body{margin:0}div,aside{display:block}#x{padding:max(24px,50%);")+replace+";height:1px}",800);auto*b=s.box("x");assert(near(b->content_box.x-b->border_box.x,replace[8]=='7'?7:0));auto h=aether_style_map_get(s.styles,aether_doc_get_element_by_id(s.doc,"x"));aether_computed_style_t flat{};assert(aether_computed_style_copy(h,&flat)==AETHER_OK);aether_computed_style_free(&flat);aether_computed_style_handle_destroy(h);}
 {Scene s("<body><div id='p'><aside id='x'></aside></div></body>","body{margin:0}div,aside{display:block}#p{font-size:20px;width:400px;padding:max(3em,10%)}#x{font-size:40px;padding:inherit;height:1px}",800);auto*b=s.box("x");assert(near(b->content_box.x-b->border_box.x,60));}
 {Scene s("<body><div id='x'></div></body>","body{margin:0}div,aside{display:block}#x{padding:7px;padding:2px max(1px,garbage);height:1px}",800);auto*b=s.box("x");assert(near(b->content_box.x-b->border_box.x,7));assert(near(b->content_box.y-b->border_box.y,7));}
 std::cout<<"PASS H4 padding/flex contextual layout, nested basis, clamp order, frozen lifetime, flat reject sentinel, clone, replacement, inherit, shorthand atomic\n";
}
