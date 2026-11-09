#include <aether/aether.h>
#include <aether/aether_work_counters.h>
#include <thread>
#include <limits>
#include "../src/diagnostics/work_counters.h"
static_assert(sizeof(aether_work_counters_v1_t) == 13 * sizeof(uint64_t));
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
void ok(aether_status_t s){if(s!=AETHER_OK){std::fprintf(stderr,"ABI error %d\n",s);std::exit(2);}}
void require(bool b,const char* what){if(!b){std::fprintf(stderr,"FAIL %s\n",what);std::exit(2);}}
aether_text_metrics_t measure(const char* t,const aether_computed_style_t*,float,void*){return {t?float(std::strlen(t))*8:0,20,16};}
aether_scroll_offset_t zero_offset(aether_node_t,void*){return {0,0};}
struct Fixture{
 aether_doc_t doc=nullptr; aether_style_runtime_t rt=nullptr; aether_style_map_t map=nullptr;
 aether_layout_engine_t engine=nullptr; aether_layout_result_t layout=nullptr;
 aether_node_t a=nullptr,b=nullptr; int elements=0;
 Fixture(int cards,bool z){
  std::string html="<html><body>";
  for(int i=0;i<cards;++i)html+="<article id='card-"+std::to_string(i)+"' class='page-card'><div><div class='page-name'>page_name</div><div class='page-file'>page.html</div></div><button class='open'>Open</button></article>";
  html+="</body></html>";ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,html.c_str()));
  aether_style_runtime_options_t opts{};aether_style_environment_t env{520,1500,1,16,0,0,0};ok(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&rt));
  std::string css="html,body{display:block;margin:0}body{width:520px;height:1500px;overflow-y:auto;background:#0b1324;font-family:Arial}div{display:block} .page-card{display:flex;justify-content:space-between;align-items:center;margin-bottom:11px;padding:16px 18px;background:rgba(19,31,57,.94);border:1px solid #263d65;border-radius:13px}.page-card:hover{background:#1a2b4d;border-color:#4a78b9}.page-name{color:#f4f8ff;font-size:16px;font-weight:700}.page-file{margin-top:5px;color:#8fa6c8;font-size:12px}.open{padding:10px 16px;background:#38bdf8;color:#06233a;border:0;border-radius:9px;font-weight:700}.open:hover{background:#7dd3fc}";
  if(z)css+=".page-card{position:relative;z-index:1}";
  int64_t handle=0;ok(aether_style_runtime_add_stylesheet(rt,css.c_str(),2,"catalog.css",&handle,nullptr));
  aether_style_update_t* updates=nullptr;elements=aether_style_runtime_resolve_subtree(rt,aether_doc_get_document_element(doc),&updates);require(elements>0,"initial resolve");aether_style_updates_free(updates,elements);
  ok(aether_style_runtime_create_resolved_style_map(rt,doc,&map));ok(aether_layout_create(measure,nullptr,&engine));ok(aether_layout_layout(engine,doc,map,520,1500,nullptr,nullptr,&layout));
  a=aether_doc_get_element_by_id(doc,"card-0");b=aether_doc_get_element_by_id(doc,"card-1");require(a&&b,"cards exist");
 }
 ~Fixture(){aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
};

aether_work_counters_v1_t read_work() {
 aether_work_counters_v1_t c{};ok(aether_work_counters_get_v1(&c));return c;
}
int main(){
 require(aether_work_counters_get_v1(nullptr)==AETHER_ERROR_NULL_ARG,"null output rejected");
 const bool supported=aether_work_counters_set_enabled(AETHER_FALSE)!=0;
 for(int n:{100,500,1000}){
  Fixture f(n,false); aether_node_t changed[]={f.a,f.b};
  aether_work_counters_reset();
  require(read_work().compute_nodes==0,"reset starts empty");
  aether_element_set_state(f.a,AETHER_STATE_HOVER);
  aether_style_update_t* out=nullptr;
  int count=aether_style_runtime_resolve_state_changes(f.rt,aether_doc_get_document_element(f.doc),changed,2,&out);
  require(count==1,"one hovered card");aether_style_updates_free(out,count);
  require(read_work().compute_nodes==0,"disabled collection remains empty");
  require(bool(aether_work_counters_set_enabled(AETHER_TRUE))==supported,"stable support");
  aether_work_counters_reset();
  // A visually empty resolution must still reveal all internal full work.
  out=nullptr;count=aether_style_runtime_resolve_state_changes(f.rt,aether_doc_get_document_element(f.doc),changed,2,&out);
  require(count==0,"no visual update");aether_style_updates_free(out,count);
  auto c=read_work();
  if(supported){
   require(c.compute_nodes==uint64_t(f.elements),"full computation count");
   require(c.state_entries_visited==uint64_t(f.elements),"full old state traversal");
   require(c.revision_records_created==uint64_t(f.elements),"full record allocation");
   require(c.revision_entries_created==uint64_t(f.elements),"full manifest construction");
   require(c.revision_prepares==1&&c.revision_publications==1&&c.state_full_fallbacks==1,"full commit counted");
  }else require(c.compute_nodes==0,"unsupported build remains empty");
  std::printf("cards=%d elements=%d updates=%d compute=%llu records=%llu entries=%llu\n",n,f.elements,count,(unsigned long long)c.compute_nodes,(unsigned long long)c.revision_records_created,(unsigned long long)c.revision_entries_created);
  aether_work_counters_reset();ok(aether_style_runtime_update_resolved_style_map(f.rt,f.map,changed,2));
  if(supported)require(read_work().map_entries_copied==uint64_t(f.elements),"patch copies N entries");
  aether_bool_t context_changed=0;aether_work_counters_reset();
  ok(aether_layout_prepare_transition_context(f.engine,nullptr,f.map,f.layout,&context_changed));
  if(supported){c=read_work();require(c.transition_context_entries_visited==uint64_t(f.elements)&&c.transition_context_builds==1,"context scans N entries");}
  auto box=aether_layout_result_find_primary_box(f.layout,f.a);require(box,"primary box");auto rect=aether_layout_box_get_border_box(box);
  aether_work_counters_reset();
  require(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.map,rect.x+3,rect.y+3,zero_offset,nullptr)==f.a,"hit unchanged");
  if(supported){c=read_work();require(c.hit_queries==1&&c.hit_boxes_visited>0&&c.paint_order_children_classified>0,"hit work counted");}
  aether_work_counters_set_enabled(AETHER_FALSE);
 }
 if(supported){
  aether_work_counters_set_enabled(AETHER_TRUE);aether_work_counters_reset();
  std::thread worker([] {require(read_work().hit_queries==0,"new thread empty");Fixture f(2,false);require(read_work().compute_nodes==0,"new thread disabled");aether_work_counters_set_enabled(AETHER_TRUE);aether_work_counters_reset();aether_node_t changed=f.a;aether_style_update_t* updates=nullptr;int n=aether_style_runtime_resolve_state_changes(f.rt,aether_doc_get_document_element(f.doc),&changed,1,&updates);aether_style_updates_free(updates,n);require(read_work().compute_nodes>0,"thread collects independently");});
  worker.join();require(read_work().compute_nodes==0,"worker does not alter caller counters");
 }
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
 aether_work_counters_set_enabled(AETHER_TRUE);aether_work_counters_reset();
 aether::diagnostics::count_work(aether::diagnostics::WorkCounter::compute_nodes,std::numeric_limits<uint64_t>::max());
 aether::diagnostics::count_work(aether::diagnostics::WorkCounter::compute_nodes);
 require(read_work().compute_nodes==std::numeric_limits<uint64_t>::max(),"counter saturates instead of wrapping");
 aether_work_counters_set_enabled(AETHER_FALSE);aether_work_counters_reset();
 aether::diagnostics::count_work(aether::diagnostics::WorkCounter::compute_nodes);
 require(read_work().compute_nodes==0,"reset does not enable collection");
#endif
 std::puts("PASS work counters and full oracle fixture");
}
