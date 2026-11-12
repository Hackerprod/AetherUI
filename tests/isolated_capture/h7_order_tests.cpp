#ifdef NDEBUG
#error Assertions required
#endif
#include <aether/aether_contextual_style.h>
#include "../../native/src/layout/layout_types.h"
#include "../../native/src/css/computed_style_handle.h"
#include "../../native/src/css/frozen_measure_read.h"
#include "../../native/src/rendering/software/software_renderer.h"
#include "../../native/src/rendering/preparation/native_command_source.h"
#include "../../native/src/rendering/display_list_types.h"
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <fstream>
#include "../software_render_smoke/png_writer.h"
static std::string output_dir;
static aether_text_metrics_t no_text(const char*,const aether_computed_style_t*,float,void*){return {};}
struct Scene {
 uint32_t last_a_impact = 0;
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t styles{};aether_layout_engine_t engine{};aether_layout_result_t result{};
 Scene(const std::string& extra,const char* html="<body><div id='p'><aside id='a'></aside><aside id='b'></aside><aside id='c'></aside></div></body>"){
  assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,html)==AETHER_OK);
  aether_style_runtime_options_t opt{};aether_style_environment_t env{300,100,1,16,0,0,0};
  assert(aether_style_runtime_create(&opt,&env,nullptr,nullptr,&runtime)==AETHER_OK);
  int64_t sh{};int32_t rules{};const auto css="body{margin:0}#p{display:flex;width:90px;height:60px;align-items:start}aside{display:block;width:30px;height:20px;flex-shrink:0}#a{background:red}#b{background:green}#c{background:blue}"+extra;
  assert(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"order",&sh,&rules)==AETHER_OK);resolve();
  assert(aether_layout_create(no_text,nullptr,&engine)==AETHER_OK);layout();
 }
 void resolve(){aether_style_update_t* updates{};int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);last_a_impact=0;for(int i=0;i<count;++i){aether_node_t n{};aether_computed_style_handle_t h{};uint32_t impact{};assert(aether_style_runtime_get_update(updates[i],&n,&h,&impact)==AETHER_OK);if(n==node("a"))last_a_impact=impact;aether_computed_style_handle_destroy(h);}aether_style_updates_free(updates,count);aether_style_map_destroy(styles);styles=nullptr;assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);}
 void layout(){assert(aether_layout_layout(engine,doc,styles,300,100,nullptr,nullptr,&result)==AETHER_OK);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}
 aether_layout_box_s* box(const char* id){return aether_layout_result_find_primary_box(result,node(id));}
 int32_t order(const char* id){auto h=aether_style_map_get(styles,node(id));assert(h);auto n=h->measures->scalars().flex_order;aether_computed_style_handle_destroy(h);return n;}
 ~Scene(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
static void pixels(Scene& s,int x,int y,unsigned blue,unsigned green,unsigned red){
 aether_display_list_builder_t builder{};assert(aether_display_list_builder_create(no_text,nullptr,&builder)==AETHER_OK);
 std::unique_ptr<aether::rendering::NativePaintList> list;assert(aether::rendering::build_native_opacity(builder,s.result,s.styles,nullptr,list)==AETHER_OK);
 namespace p=aether::rendering::preparation;auto snap=p::snapshot_native_geometry(*list,p::ValidationDomain::ResolvedGeometryOnly);assert(std::holds_alternative<p::Snapshot>(snap));auto frame=p::prepare(std::get<p::Snapshot>(snap));assert(std::holds_alternative<p::PreparedFrame>(frame));
 aether::rendering::FrameStore store;assert(store.resize({300,100,1})==aether::rendering::FrameStatus::ok);assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(frame)).status==aether::rendering::software::Status::ok);
 std::optional<aether::rendering::Frame> out;assert(store.copy_confirmed(out)==aether::rendering::FrameStatus::ok&&out);auto at=y*out->stride+x*4;assert(out->pixels[at]==blue&&out->pixels[at+1]==green&&out->pixels[at+2]==red&&out->pixels[at+3]==255);
 if(!output_dir.empty()){static int serial=0;auto bytes=smoke_png::png(smoke_png::rgba(*out),300,100);std::ofstream f(output_dir+"/order-overlap-"+std::to_string(++serial)+".png",std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();assert(bool(f));}
 aether_display_list_builder_destroy(builder);
}
int main(int argc,char** argv){
 if(argc>1)output_dir=argv[1];
 {Scene s("#a{order:2}#b{order:-1}#c{order:-1}");assert(s.order("a")==2&&s.order("b")==-1);assert(s.box("b")->border_box.x==0&&s.box("c")->border_box.x==30&&s.box("a")->border_box.x==60);assert(s.node("p")->children[0]==s.node("a")&&s.node("p")->children[1]==s.node("b"));
  auto h=aether_style_map_get(s.styles,s.node("a"));aether_computed_style_handle_t clone{};assert(aether_computed_style_handle_clone(h,&clone)==AETHER_OK);aether_computed_style_handle_destroy(h);aether_computed_style_t sentinel{},before{};std::memset(&sentinel,0x5a,sizeof sentinel);before=sentinel;assert(aether_computed_style_copy(clone,&sentinel)==AETHER_ERROR_INVALID_STATE&&std::memcmp(&before,&sentinel,sizeof sentinel)==0);
  auto old=s.result;s.result=nullptr;assert(aether_element_set_attribute(s.node("a"),"style","order:initial")==AETHER_OK);s.resolve();assert(s.last_a_impact==7);s.layout();assert(s.order("a")==0);assert(aether_layout_result_find_primary_box(old,s.node("a"))->border_box.x==60);assert(clone->measures->scalars().flex_order==2);aether_style_runtime_destroy(s.runtime);s.runtime=nullptr;assert(clone->measures->scalars().flex_order==2);aether_computed_style_handle_destroy(clone);aether_layout_result_destroy(old);
 }
 {Scene s("#p{flex-direction:row-reverse}#a{order:2}#b{order:-1}#c{order:-1}");assert(s.box("b")->border_box.x==60&&s.box("c")->border_box.x==30&&s.box("a")->border_box.x==0);}
 {Scene s("#p{width:60px;flex-wrap:wrap}#a{order:2}#b{order:-1}#c{order:-1}");assert(s.box("b")->border_box.x==0&&s.box("c")->border_box.x==30&&s.box("a")->border_box.y>s.box("b")->border_box.y);}
 {Scene s("#a{order:2147483647}#b{order:-2147483648}#c{order:+0}");assert(s.order("a")==INT32_MAX&&s.order("b")==INT32_MIN&&s.order("c")==0);}
 for(const char* bad:{"2147483648","-2147483649","99999999999999999999999999","1.5","1e2","1 junk"}){Scene s(std::string("#a{order:2;order:")+bad+"}");assert(s.order("a")==2);}
 for(const char* value:{"initial","unset"}){Scene s(std::string("#p{order:4}#a{order:2;order:")+value+"}");assert(s.order("a")==0&&s.order("b")==0);}
 {Scene s("#p{order:4}#a{order:inherit}");assert(s.order("a")==4&&s.order("b")==0);}
 // CSS endpoints and initial local time are sampled before layout. Ties
 // round toward positive infinity, including the negative half-integers.
 for(auto test:{std::pair{"-1.25s",-1},std::pair{"-3.75s",0},std::pair{"-6.25s",1}}){
  Scene s(std::string("@keyframes ord{from{order:-2}to{order:2}}#a{animation:ord 10s linear ")+test.first+" paused}");
  if(s.order("a")!=test.second)std::cerr<<"delay "<<test.first<<" got order "<<s.order("a")<<" expected "<<test.second<<"\n";
  assert(s.order("a")==test.second);
  if(test.second<0)assert(s.box("a")->border_box.x==0);
  else if(test.second==0)assert(s.box("a")->border_box.x==0); // ties retain DOM
  else assert(s.box("a")->border_box.x==60);
 }
 // Order changes the painting and hit sequence too, not just positions.
 {Scene s("#a{order:2;margin-left:-30px}#b{order:1}#c{display:none}");assert(s.box("a")->border_box.x==0&&s.box("b")->border_box.x==0);assert(aether_layout_result_hit_test(s.result,10,10)==s.node("a"));pixels(s,10,10,0,0,255);}
 {Scene s("#p{flex-direction:row-reverse}#a{order:2;margin-right:-30px}#b{order:1}#c{display:none}");assert(s.box("a")->border_box.x==s.box("b")->border_box.x);const int x=int(s.box("a")->border_box.x)+10;assert(aether_layout_result_hit_test(s.result,float(x),10)==s.node("a"));pixels(s,x,10,0,0,255);}
 std::cout<<"PASS H7 order negative/ties/int32 bounds/strict parser/reverse/wrap/DOM intact/base reset/frozen lifetime/clone/flat reject/overlap pixels+hit\n";
}
