#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/layout/initial_canvas.h"
#include "../../native/src/rendering/software/software_renderer.h"
#include "../../native/src/rendering/preparation/native_command_source.h"
#include "../../native/src/rendering/display_list_types.h"
#include <cassert>
#include <iostream>
aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void* state){++*static_cast<int*>(state); return {37.f,19.f,14.f};}
int main(){
 for(auto value:{"", "-1", "2147483648", "999999999999999999999999"}) {std::string s=value;assert(initial_canvas_dimension(&s,300)==300);}
 for(auto value:{"100", " +100junk", "100.9"}){std::string s=value;assert(initial_canvas_dimension(&s,300)==100);}
 std::string zero="0";assert(initial_canvas_dimension(&zero,300)==0);assert(initial_canvas_dimension(nullptr,300)==300);
 auto sz=initial_canvas_content_size({100,50},40,NAN,true,10,8);assert(sz.width==30&&sz.height==15);
 sz=initial_canvas_content_size({100,50},2,NAN,true,10,8);assert(sz.width==0&&sz.height==0);
 bool threw=false;try{initial_canvas_content_size({0,50},20,NAN,false,0,0);}catch(const std::invalid_argument&){threw=true;}assert(threw);
 aether_doc_t doc{};aether_style_map_t styles{};aether_style_runtime_t runtime{};aether_layout_engine_t engine{},other{};
 assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,"<body><div><canvas id='c' width='100' height='50'>fallback<span>NOT MEASURED</span></canvas><aside></aside></div></body>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{500,200,1,16,0,0,0};
 assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);int64_t sheet{};int32_t rules{};
 assert(aether_style_runtime_add_stylesheet(runtime,"body{margin:0}div{display:flex;width:420px;height:100px;align-items:center;gap:10px}canvas{border:2px solid red}aside{width:20px;height:20px}",0,"test",&sheet,&rules)==AETHER_OK);
 aether_style_update_t* updates{};int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);
 int calls=0;assert(aether_layout_create(measure,&calls,&engine)==AETHER_OK);assert(aether_layout_create(measure,&calls,&other)==AETHER_OK);
 const auto run=[&](aether_layout_engine_t e){aether_layout_result_t r{};assert(aether_layout_layout(e,doc,styles,500,200,nullptr,nullptr,&r)==AETHER_OK);return r;};
 const auto box=[&](aether_layout_result_t r){for(auto* b:r->allocated_boxes)if(b->element==aether_doc_get_element_by_id(doc,"c"))return b;return static_cast<aether_layout_box_s*>(nullptr);};
 auto baseline=run(engine);const auto baseline_rect=box(baseline)->border_box;assert(calls>0);calls=0;
 set_initial_canvas_mode(engine,true);auto on=run(engine);auto* c=box(on);assert(c&&c->is_initial_canvas&&!c->is_atomic_control&&c->children.empty());assert(calls==0);assert(c->content_box.width==100&&c->content_box.height==50);assert(c->border_box.width==104&&c->border_box.height==54);assert(engine->initial_canvas_entries.size()==1);
 auto independent=run(other);assert(!box(independent)->is_initial_canvas);assert(other->initial_canvas_entries.empty());
 set_initial_canvas_mode(engine,false);auto off=run(engine);assert(!box(off)->is_initial_canvas&&engine->initial_canvas_entries.empty());assert(calls>0);
 const auto off_rect=box(off)->border_box;assert(off_rect.x==baseline_rect.x&&off_rect.y==baseline_rect.y&&off_rect.width==baseline_rect.width&&off_rect.height==baseline_rect.height);assert(c->is_initial_canvas&&c->content_box.width==100&&c->children.empty());
 aether_layout_result_destroy(baseline);aether_layout_result_destroy(off);aether_layout_result_destroy(independent);aether_layout_destroy(engine);aether_layout_destroy(other);assert(c->is_initial_canvas&&c->content_box.height==50);
 aether_display_list_builder_t builder{};assert(aether_display_list_builder_create(measure,&calls,&builder)==AETHER_OK);
 std::unique_ptr<aether::rendering::NativePaintList> list;
 assert(aether::rendering::build_native_opacity(builder,on,styles,nullptr,list)==AETHER_OK);
 namespace p=aether::rendering::preparation;
 auto snap=p::snapshot_native_geometry(*list,p::ValidationDomain::ResolvedGeometryOnly);assert(std::holds_alternative<p::Snapshot>(snap));auto frame=p::prepare(std::get<p::Snapshot>(snap));assert(std::holds_alternative<p::PreparedFrame>(frame));
 aether_display_list_builder_destroy(builder);aether_layout_result_destroy(on);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
 aether::rendering::FrameStore store;assert(store.resize({500,200,1})==aether::rendering::FrameStatus::ok);
 assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(frame)).status==aether::rendering::software::Status::ok);
 // Registry cost follows element count, not intrinsic area; no canvas buffers exist.
 for(int n:{1,16,256,257}) {
  aether_doc_t d{};assert(aether_doc_create(&d)==AETHER_OK);std::string html="<body>";
  for(int i=0;i<n;++i)html+="<canvas width='8192' height='8192'></canvas>";
  html+="</body>";assert(aether_doc_parse_html(d,html.c_str())==AETHER_OK);
  aether_layout_engine_s e;set_initial_canvas_mode(&e,true);bool limit=false;
  try{prepare_initial_canvas(&e,d);}catch(const std::length_error&){limit=true;}
  assert(limit==(n==257));if(!limit)assert(e.initial_canvas_entries.size()==static_cast<size_t>(n));
  aether_doc_destroy(d);
 }
 std::cout<<"PASS dimensions ratio decoration-floor fallback-no-measure per-engine on-off lifetime\n";
}
