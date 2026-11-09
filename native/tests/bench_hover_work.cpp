#include <aether/aether.h>
#include <aether/aether_work_counters.h>
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
int main(){
 require(aether_work_counters_set_enabled(AETHER_TRUE),"diagnostic build required");
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
  }
 }
}
