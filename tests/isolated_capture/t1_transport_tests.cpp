// Dependency-free transport tests, NOT real shaping/raster acceptance.
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



#include "../../native/src/rendering/display_list_types.h"
#include <thread>
using namespace aether::rendering;
int main(){
 auto font=std::make_shared<const text::FontInstance>(std::vector<std::uint8_t>{1},"fixture",0,768,0);
 auto run=std::make_shared<const text::ShapedRun>(font,"x",std::vector<text::Glyph>{{1,0,64,0,0,0}},aether_text_metrics_t{10,12,9});

 bool rejected=false;
 try { text::ShapedRun bad(font,"x",{{1,1,0,0,0,0}},{10,12,9}); }
 catch(const std::invalid_argument&){rejected=true;} ck(rejected);
 rejected=false;
 try { text::ShapedRun bad(font,"x",{{1,0,0,0,0,0}},{10,12,13}); }
 catch(const std::invalid_argument&){rejected=true;} ck(rejected);
 alloc_budget=0; bool allocation_failed=false;
 try { auto unused=std::make_shared<const text::ShapedRun>(*run); }
 catch(const std::bad_alloc&){allocation_failed=true;}
 alloc_budget=-1;ck(allocation_failed);
 std::weak_ptr<const text::FontInstance> weak_font=font;
 std::weak_ptr<const text::ShapedRun> weak_run=run;
 Scene scene("","<body><div>x</div></body>");
 aether_layout_box_s* text_box=nullptr;
 for(auto* box:scene.result->allocated_boxes)if(box->kind==LAYOUT_BOX_TEXT)text_box=box;
 ck(text_box!=nullptr);ck(!text_box->native_text.run);
 text_box->native_text={run,9};
 text_box->border_box=text_box->content_box=text_box->padding_box=text_box->margin_box={0,0,10,12};
 aether_layout_result_t clone{};
 ck(aether_layout_clone_snapshot(scene.result,&clone)==AETHER_OK);
 bool copied=false;for(auto* box:clone->allocated_boxes)if(box->kind==LAYOUT_BOX_TEXT){
  ck(box->native_text.run==run);ck(box->native_text.baseline==9);copied=true;
 }
 ck(copied);
 aether_display_list_builder_t builder{};
 ck(aether_display_list_builder_create(nullptr,nullptr,&builder)==AETHER_OK);
 std::unique_ptr<NativePaintList> frame;
 ck(build_native_opacity(builder,scene.result,scene.map,nullptr,frame)==AETHER_OK);
 ck(frame->text_runs.size()==1);ck(frame->text_runs[0].placement.run==run);
 auto* previous=frame.get();
 text_box->native_text={};
 ck(build_native_opacity(builder,scene.result,scene.map,nullptr,frame)!=AETHER_OK);
 ck(frame.get()==previous);
 // Legacy command build continues accepting callback-produced, unshaped boxes.
 aether_display_list_t legacy{};
 ck(aether_display_list_build_with_styles(builder,scene.result,scene.map,nullptr,&legacy)==AETHER_OK);
 aether_display_list_destroy(legacy);
 text_box->native_text={run,9};
 bool saw_oom=false,completed=false;
 for(long budget=0;budget<10000;++budget){
  alloc_budget=budget;injected=false;
  const auto status=build_native_opacity(builder,scene.result,scene.map,nullptr,frame);
  alloc_budget=-1;
  if(status==AETHER_OK){completed=true;break;}
  ck(frame.get()==previous);
  if(status==AETHER_ERROR_OUT_OF_MEMORY)saw_oom=true;
 }
 ck(saw_oom&&completed);
 text_box->native_text={};run.reset();font.reset();
 aether_layout_result_destroy(clone);
 ck(!weak_run.expired()&&!weak_font.expired());
 // Transport holds no thread-affine native handles; final release off-thread.
 std::thread release([owned=std::move(frame)]() mutable {owned.reset();});release.join();
 ck(weak_run.expired()&&weak_font.expired());
 aether_display_list_builder_destroy(builder);
 std::cout<<"T1 transport checks="<<checks<<" provider=NOT_RUN raster=NOT_RUN\n";
}
