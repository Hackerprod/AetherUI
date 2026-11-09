#include <aether/aether.h>
#include <aether/aether_work_counters.h>
#include "../src/css/style_map_types.h"
#include <array>
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
 aether_node_t a=nullptr,b=nullptr; int elements=0; std::string css;
 Fixture(int cards,bool z){
  std::string html="<html><body>";
  for(int i=0;i<cards;++i)html+="<article id='card-"+std::to_string(i)+"' class='page-card'><div><div class='page-name'>page_name</div><div class='page-file'>page.html</div></div><button class='open'>Open</button></article>";
  html+="</body></html>";ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,html.c_str()));
  aether_style_runtime_options_t opts{};aether_style_environment_t env{520,1500,1,16,0,0,0};ok(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&rt));
  css="html,body{display:block;margin:0}body{width:520px;height:1500px;overflow-y:auto;background:#0b1324;font-family:Arial}div{display:block} .page-card{display:flex;justify-content:space-between;align-items:center;margin-bottom:11px;padding:16px 18px;background:rgba(19,31,57,.94);border:1px solid #263d65;border-radius:13px}.page-card:hover{background:#1a2b4d;border-color:#4a78b9}.page-name{color:#f4f8ff;font-size:16px;font-weight:700}.page-file{margin-top:5px;color:#8fa6c8;font-size:12px}.open{padding:10px 16px;background:#38bdf8;color:#06233a;border:0;border-radius:9px;font-weight:700}.open:hover{background:#7dd3fc}";
  if(z)css+=".page-card{position:relative;z-index:1}";
  int64_t handle=0;ok(aether_style_runtime_add_stylesheet(rt,css.c_str(),2,"catalog.css",&handle,nullptr));
  aether_style_update_t* updates=nullptr;elements=aether_style_runtime_resolve_subtree(rt,aether_doc_get_document_element(doc),&updates);require(elements>0,"initial resolve");aether_style_updates_free(updates,elements);
  ok(aether_style_runtime_create_resolved_style_map(rt,doc,&map));ok(aether_layout_create(measure,nullptr,&engine));ok(aether_layout_layout(engine,doc,map,520,1500,nullptr,nullptr,&layout));
  a=aether_doc_get_element_by_id(doc,"card-0");b=aether_doc_get_element_by_id(doc,"card-1");require(a&&b,"cards exist");
 }
 ~Fixture(){aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
};
template<class Fn>void bench(const char* label,int cards,bool z,int elements,int iterations,Fn fn){
 for(int i=0;i<5;++i)fn(i);
 aether_work_counters_reset();
 std::vector<double> ms;ms.reserve(iterations);const auto cpu0=std::clock();int updates=0;
 for(int i=0;i<iterations;++i){auto t=Clock::now();updates=fn(i);ms.push_back(std::chrono::duration<double,std::milli>(Clock::now()-t).count());}
 double cpu_ms=1000.0*(std::clock()-cpu0)/CLOCKS_PER_SEC/iterations;
 std::sort(ms.begin(),ms.end());double mean=0;for(double m:ms)mean+=m;mean/=ms.size();
 aether_work_counters_v1_t c{};ok(aether_work_counters_get_v1(&c));
 std::printf("%s,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",label,cards,z,elements,iterations,ms[ms.size()/2],ms[std::min(ms.size()-1,size_t(ms.size()*.95))],mean,cpu_ms,updates,(unsigned long long)c.compute_nodes,(unsigned long long)c.state_entries_visited,(unsigned long long)c.revision_records_created,(unsigned long long)c.revision_publications,(unsigned long long)c.map_entries_copied,(unsigned long long)c.hit_queries,(unsigned long long)c.hit_boxes_visited,(unsigned long long)c.paint_order_children_classified);std::fflush(stdout);
}
// Keep the baseline update_two_node_style_map case unchanged: after warmup it
// measures updates with identical records. This separate case alternates two
// genuinely different, already-resolved record sets without resolving in the
// timed interval. Internal reads below only verify ownership/output after timing.
void bench_replacing_records(Fixture& f,int cards,bool counters_supported){
 using namespace aether::style;
 struct Alternate {
  aether_style_runtime_t runtime=nullptr;
  aether_style_map_t map=nullptr;
  ~Alternate(){aether_style_map_destroy(map);aether_style_runtime_destroy(runtime);}
 } alternate;
 aether_style_runtime_options_t opts{};
 aether_style_environment_t env{520,1500,1,16,0,0,0};
 ok(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&alternate.runtime));
 const std::string css=f.css+"#card-0,#card-1{background:#ff00ff}";
 int64_t stylesheet=0;
 ok(aether_style_runtime_add_stylesheet(alternate.runtime,css.c_str(),2,"alternate-records.css",&stylesheet,nullptr));
 aether_style_update_t* updates=nullptr;
 int resolved=aether_style_runtime_resolve_subtree(alternate.runtime,aether_doc_get_document_element(f.doc),&updates);
 require(resolved==f.elements,"alternate full snapshot prepared outside benchmark");
 aether_style_updates_free(updates,resolved);
 ok(aether_style_runtime_create_resolved_style_map(alternate.runtime,f.doc,&alternate.map));
 const auto initial=pin_style_map(f.map),other=pin_style_map(alternate.map);
 require(initial&&other&&initial->document_identity()==other->document_identity(),"alternate snapshots have same document");
 const std::array<aether_node_t,2> nodes={f.a,f.b};
 std::array<std::array<FrozenNodeRef,2>,2> expected;
 for(size_t i=0;i<nodes.size();++i){
  expected[0][i]=initial->backing()->find_owner(nodes[i]);
  expected[1][i]=other->backing()->find_owner(nodes[i]);
  require(expected[0][i]&&expected[1][i]&&expected[0][i]!=expected[1][i],"two distinct record sets");
  require(expected[0][i]->node_identity()==expected[1][i]->node_identity(),"same authentic node identity");
  auto a=expected[0][i]->style_view().scalars().background_color;
  auto b=expected[1][i]->style_view().scalars().background_color;
  require(a.r!=b.r||a.g!=b.g||a.b!=b.b||a.a!=b.a,"replacement changes visible output");
 }
 int selected=0;
 auto verify=[&](uint64_t previous_copies){
  aether_work_counters_v1_t counters{};ok(aether_work_counters_get_v1(&counters));
  if(counters_supported)require(counters.map_entries_copied==previous_copies+nodes.size(),"every measured patch replaces exactly K records");
  else require(counters.map_entries_copied==0,"disabled build keeps work counters zero");
  require(counters.compute_nodes==0&&counters.revision_publications==0,"no resolve or publication inside patch benchmark");
  if(counters_supported)require(counters.revision_records_created==previous_copies+nodes.size(),"K bound records exported for legacy patch");
  const auto current=pin_style_map(f.map);
  require(current->document_identity()==initial->document_identity()&&current->input_epoch()==initial->input_epoch()&&
   current->structure_epoch()==initial->structure_epoch()&&current->style_tree_epoch()==initial->style_tree_epoch(),"legacy patch preserves provenance and epochs");
  for(size_t i=0;i<nodes.size();++i){
   const auto actual=current->backing()->find_owner(nodes[i]);const auto& wanted=expected[selected][i];
   const auto& payload=actual->payload_owner();const auto& expected_payload=wanted->payload_owner();
   const auto& document=actual->measures()->document_owner();const auto& expected_document=wanted->measures()->document_owner();
   require(payload==expected_payload&&!payload.owner_before(expected_payload)&&!expected_payload.owner_before(payload)&&
    document==expected_document&&!document.owner_before(expected_document)&&!expected_document.owner_before(document),
    "alternating legacy output has exact expected payload and document owners");
  }
 };
 aether_work_counters_reset();
 for(int i=0;i<5;++i){
  selected=1-selected;
  ok(aether_style_runtime_update_resolved_style_map(selected?alternate.runtime:f.rt,f.map,nodes.data(),int32_t(nodes.size())));
  verify(uint64_t(i)*nodes.size());
 }
 constexpr int iterations=50;
 std::vector<double> ms;ms.reserve(iterations);double cpu_ms=0;
 aether_work_counters_reset();
 for(int i=0;i<iterations;++i){
  selected=1-selected;
  const auto runtime=selected?alternate.runtime:f.rt;
  const auto cpu0=std::clock();const auto t=Clock::now();
  const auto status=aether_style_runtime_update_resolved_style_map(runtime,f.map,nodes.data(),int32_t(nodes.size()));
  const auto elapsed=Clock::now()-t;const auto cpu_elapsed=std::clock()-cpu0;
  // Assertions, reads and sampling aggregation are outside both measurements.
  ok(status);ms.push_back(std::chrono::duration<double,std::milli>(elapsed).count());
  cpu_ms+=1000.0*cpu_elapsed/CLOCKS_PER_SEC;
  verify(uint64_t(i)*nodes.size());
 }
 std::sort(ms.begin(),ms.end());double mean=0;for(double value:ms)mean+=value;mean/=iterations;
 aether_work_counters_v1_t c{};ok(aether_work_counters_get_v1(&c));
 std::printf("update_two_node_style_map_replacements,%d,0,%d,%d,%.6f,%.6f,%.6f,%.6f,2,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
  cards,f.elements,iterations,ms[ms.size()/2],ms[std::min(ms.size()-1,size_t(ms.size()*.95))],mean,cpu_ms/iterations,
  (unsigned long long)c.compute_nodes,(unsigned long long)c.state_entries_visited,(unsigned long long)c.revision_records_created,
  (unsigned long long)c.revision_publications,(unsigned long long)c.map_entries_copied,(unsigned long long)c.hit_queries,
  (unsigned long long)c.hit_boxes_visited,(unsigned long long)c.paint_order_children_classified);std::fflush(stdout);
}
int main(){
 const bool counters_supported=aether_work_counters_set_enabled(AETHER_TRUE)!=0;
 std::puts("stage,cards,z_index,elements,iterations,p50_ms,p95_ms,mean_ms,cpu_mean_ms,updates_or_hit_ok,compute_nodes_total,state_entries_total,records_total,publications_total,map_entries_copied_total,hit_queries_total,hit_boxes_total,paint_order_classifications_total");
 for(int n:{18,100,500,1000}){
  for(bool z:{false,true}){
   Fixture f(n,z);auto box=aether_layout_result_find_primary_box(f.layout,f.a);require(box,"primary box");auto rect=aether_layout_box_get_border_box(box);
   bench("hit_first_card",n,z,f.elements,300,[&](int i){auto hit=aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.map,rect.x+3+float(i%3),rect.y+3,zero_offset,nullptr);require(hit==f.a,"hit first card expected");return 1;});
   if(z)continue;
   aether_node_t changed[]={f.a,f.b};int toggle=0;
   bench("resolve_two_hover_nodes",n,z,f.elements,40,[&](int){++toggle;aether_element_set_state(f.a,(toggle%2)?AETHER_STATE_HOVER:AETHER_STATE_NONE);aether_element_set_state(f.b,(toggle%2)?AETHER_STATE_NONE:AETHER_STATE_HOVER);aether_style_update_t* out=nullptr;int count=aether_style_runtime_resolve_state_changes(f.rt,aether_doc_get_document_element(f.doc),changed,2,&out);require(count==2||toggle==1,"two updates");aether_style_updates_free(out,count);return count;});
   bench("resolve_no_visual_change",n,z,f.elements,30,[&](int){aether_style_update_t* out=nullptr;int count=aether_style_runtime_resolve_state_changes(f.rt,aether_doc_get_document_element(f.doc),changed,2,&out);require(count==0,"no visual delta");aether_style_updates_free(out,count);return count;});
   auto child=aether_node_get_child(aether_node_get_child(f.a,0),0);require(child,"unstyled child target");int child_toggle=0;
   bench("resolve_unstyled_child_hover",n,z,f.elements,30,[&](int){++child_toggle;aether_element_set_state(child,child_toggle%2?AETHER_STATE_HOVER:AETHER_STATE_NONE);aether_style_update_t* out=nullptr;int count=aether_style_runtime_resolve_state_changes(f.rt,aether_doc_get_document_element(f.doc),&child,1,&out);require(count==0,"child hover no visual delta");aether_style_updates_free(out,count);return count;});
   bench("update_two_node_style_map",n,z,f.elements,50,[&](int){ok(aether_style_runtime_update_resolved_style_map(f.rt,f.map,changed,2));return 2;});
   bench_replacing_records(f,n,counters_supported);
  }
 }
}
