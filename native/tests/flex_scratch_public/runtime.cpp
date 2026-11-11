// Public-only fixture. Timing builds use the ordinary allocator; OOM is separate.
#include <aether/aether.h>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#if defined(__linux__)
#include <malloc.h>
#endif
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 33)
#define FLEX_HAVE_MALLINFO2 1
#endif
#endif
#ifndef FLEX_HAVE_MALLINFO2
#define FLEX_HAVE_MALLINFO2 0
#endif
#include <atomic>
#include <thread>
static void check(bool x) { if(!x){std::fputs("public flex check failed\n",stderr);std::abort();} }
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){return {0,0,0};}
struct Fixture {
 aether_doc_t doc{}; aether_style_runtime_t rt{}; aether_style_map_t map{}; aether_layout_engine_t engine{};
 aether_display_list_builder_t painter{}; std::vector<aether_node_t> nodes;
 bool column; size_t count; std::string mode;
 Fixture(size_t n,const std::string& m,bool col):column(col),count(n),mode(m){
  check(aether_doc_create(&doc)==0);
  std::string html="<html style='margin:0'><body style='margin:0'><div id='p' style='display:flex;flex-direction:"+std::string(col?"column":"row")+";flex-wrap:"+(m=="nowrap"||m=="shrink"?"nowrap":"wrap")+";width:100%;height:"+(col?"101px":"180px")+";align-content:flex-start;align-items:flex-start;gap:1px;overflow:hidden'>";
  for(size_t i=0;i<n;++i){
   double base=m=="increasing" ? 90.0/(1+(i%16)):30.0;
   html+="<div id='i"+std::to_string(i)+"' style='flex-basis:"+std::to_string(base)+"px;flex-grow:"+(m=="frozen"||(m=="mixed"&&i%3==0)?"0":"1")+";flex-shrink:"+(m=="frozen"?"0":"1")+";min-width:0;min-height:0;width:12px;height:12px;background:#123456'></div>";
  }
  html+="</div></body></html>";check(aether_doc_parse_html(doc,html.c_str())==0);
  aether_style_environment_t env{101,180,1,16,0,0,0}; check(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==0);
  refresh();check(aether_layout_create(measure,nullptr,&engine)==0);check(aether_display_list_builder_create(measure,nullptr,&painter)==0);
  nodes.push_back(aether_doc_get_element_by_id(doc,"p"));for(size_t i=0;i<n;++i)nodes.push_back(aether_doc_get_element_by_id(doc,("i"+std::to_string(i)).c_str()));
 }
 void refresh(){aether_style_map_destroy(map);map=nullptr;aether_style_update_t* u{};auto n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_document_element(doc),&u);check(n>=0);aether_style_updates_free(u,n);check(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==0);}
 ~Fixture(){aether_display_list_builder_destroy(painter);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
 aether_layout_result_t layout(float width){aether_layout_result_t r{};check(aether_layout_layout(engine,doc,map,width,180,nullptr,nullptr,&r)==0);check(r);return r;}
 int key(aether_node_t p){if(!p)return -1;for(size_t i=0;i<nodes.size();++i)if(nodes[i]==p)return int(i);if(p==aether_doc_get_body(doc))return -2;if(p==aether_doc_get_document_element(doc))return -3;std::abort();}
 int64_t identity(int64_t raw){for(auto node:nodes)if(aether_node_get_style_node_id(node)==raw)return key(node);for(auto node:{aether_doc_get_body(doc),aether_doc_get_document_element(doc)})if(aether_node_get_style_node_id(node)==raw)return key(node);std::fprintf(stderr,"unknown paint owner id\n");std::abort();}
 void mutate(){check(aether_element_set_attribute(nodes[1],"style","flex:2 1 17px;min-width:0;min-height:0;width:12px;height:12px;background:#123456")==0);check(aether_node_append_child(nodes[0],nodes[1])==0);refresh();}
};
struct Manifest {
 std::vector<uint64_t> v;
 void u(uint64_t x){v.push_back(x);} void f(float x){u(std::bit_cast<uint32_t>(x));}
 void rect(aether_rectf r){f(r.x);f(r.y);f(r.width);f(r.height);}
 void radii(aether_corner_radii r){f(r.top_left_x);f(r.top_left_y);f(r.top_right_x);f(r.top_right_y);f(r.bottom_right_x);f(r.bottom_right_y);f(r.bottom_left_x);f(r.bottom_left_y);}
 void color(aether_color_rgba c){f(c.r);f(c.g);f(c.b);f(c.a);}
 void box(Fixture& q,aether_layout_box_t b){
  check(b);u(q.key(aether_layout_box_get_element(b)));u(aether_layout_box_get_kind(b));u(aether_layout_box_is_atomic(b));
  f(aether_layout_box_get_x(b));f(aether_layout_box_get_y(b));f(aether_layout_box_get_width(b));f(aether_layout_box_get_height(b));
  rect(aether_layout_box_get_margin_box(b));rect(aether_layout_box_get_border_box(b));rect(aether_layout_box_get_padding_box(b));rect(aether_layout_box_get_content_box(b));
  auto s=aether_layout_box_get_scroll_metrics(b);f(s.scrollable_overflow_x);f(s.scrollable_overflow_y);f(s.scroll_offset_x);f(s.scroll_offset_y);
  auto g=aether_layout_box_get_scrollbar_geometry(b);u(g.horizontal_visible);u(g.vertical_visible);rect(g.horizontal_track);rect(g.horizontal_thumb);rect(g.vertical_track);rect(g.vertical_thumb);rect(g.corner);
  char* t=aether_layout_box_get_text(b);u(t?std::string(t).size():0);if(t){for(char c:std::string(t))u((unsigned char)c);aether_string_free(t);}
  int n=aether_layout_box_get_child_count(b);u(n);for(int i=0;i<n;++i)box(q,aether_layout_box_get_child(b,i));
 }
 Manifest(Fixture& q,aether_layout_result_t r){
  box(q,aether_layout_result_get_root(r));u(aether_layout_result_get_viewport_overflow_x(r));u(aether_layout_result_get_viewport_overflow_y(r));
  auto n=aether_layout_result_get_paint_order_count(r);u(n);for(int i=0;i<n;++i){auto b=aether_layout_result_get_paint_order_box(r,i);u(q.key(aether_layout_box_get_element(b)));rect(aether_layout_box_get_border_box(b));}
  for(auto node:q.nodes){auto b=aether_layout_result_find_primary_box(r,node);check(b);auto z=aether_layout_box_get_border_box(b);rect(z);for(auto p: {aether_pointf{z.x,z.y},aether_pointf{z.x+z.width/2,z.y+z.height/2},aether_pointf{z.x+z.width,z.y+z.height}})u(q.key(aether_layout_result_hit_test(r,p.x,p.y)));}
  aether_display_list_t list{};check(aether_display_list_build(q.painter,r,nullptr,&list)==0);auto count=aether_display_list_get_count(list);u(count);
  for(int i=0;i<count;++i){aether_paint_command_t c{};check(aether_display_list_get_command(list,i,&c)==0);u(c.tag);switch(c.tag){
   case AETHER_PAINT_FILL_ROUNDED_RECT:rect(c.data.fill_rounded_rect.rect);radii(c.data.fill_rounded_rect.radii);color(c.data.fill_rounded_rect.color);break;
   case AETHER_PAINT_PUSH_CLIP:rect(c.data.push_clip.rect);radii(c.data.push_clip.radii);break;
   case AETHER_PAINT_BEGIN_PAINT_CHUNK:case AETHER_PAINT_END_PAINT_CHUNK:u(q.identity(c.data.paint_chunk.style_node_id));break;
   case AETHER_PAINT_PUSH_SCROLL_OFFSET:u(q.identity(c.data.push_scroll_offset.scroll_container_id));break;
   case AETHER_PAINT_DRAW_SCROLLBAR_CHROME:u(q.identity(c.data.draw_scrollbar_chrome.scroll_container_id));break;
   case AETHER_PAINT_POP_CLIP:case AETHER_PAINT_POP_SCROLL_OFFSET:break;
   default:std::fprintf(stderr,"unsupported command %d\n",c.tag);std::abort();
  }aether_paint_command_free(&c);}aether_display_list_destroy(list);
 }
 void print(){for(auto x:v)std::printf("%llx,",(unsigned long long)x);std::puts("");}
};
#ifndef FLEX_PUBLIC_OOM
int main(int argc,char**argv){check(argc==5);std::string action=argv[1],mode=argv[2];size_t n=std::stoul(argv[3]);bool col=std::string(argv[4])=="column";Fixture f(n,mode=="mutated"?"mixed":mode,col);if(mode=="mutated")f.mutate();
 if(action=="manifest") {for(int stage=0;stage<3;++stage){if(stage==2)f.mutate();auto r=f.layout(stage==0?101:113);Manifest(f,r).print();aether_layout_result_destroy(r);}return 0;}
 check(action=="timing"||action=="memory");const int loops=n<100?20:n<1000?5:2;uint64_t checksum=0;
 for(int i=0;i<3;++i){auto r=f.layout(101+i%2);aether_layout_result_destroy(r);}
 if(action=="memory") {
#if FLEX_HAVE_MALLINFO2
  // Separate perturbed run. Sampling is a lower bound, not an exact heap peak.
  std::atomic<bool> done{false}; std::atomic<size_t> heap_peak{0};
  std::thread sampler([&]{while(!done.load()){auto m=mallinfo2();size_t used=m.uordblks+m.hblkhd;auto old=heap_peak.load();while(used>old&&!heap_peak.compare_exchange_weak(old,used)){}std::this_thread::sleep_for(std::chrono::microseconds(100));}});
  for(int i=0;i<30;++i){auto r=f.layout(101+i%2);auto m=mallinfo2();auto used=m.uordblks+m.hblkhd;auto old=heap_peak.load();while(used>old&&!heap_peak.compare_exchange_weak(old,used)){}aether_layout_result_destroy(r);}
  done.store(true);sampler.join();auto m=mallinfo2();rusage usage{};getrusage(RUSAGE_SELF,&usage);
  std::printf("heap_sampled_peak_bytes=%zu heap_final_bytes=%zu rss_high_kib=%ld\n",heap_peak.load(),m.uordblks+m.hblkhd,usage.ru_maxrss);return 0;
#else
  std::puts("memory unsupported: mallinfo2 requires glibc >= 2.33");return 77;
#endif
 }
 for(int sample=0;sample<7;++sample){double ns=0;for(int j=0;j<loops;++j){auto start=std::chrono::steady_clock::now();auto r=f.layout(101+j%2);auto stop=std::chrono::steady_clock::now();ns+=std::chrono::duration<double,std::nano>(stop-start).count();checksum+=aether_layout_result_get_paint_order_count(r);aether_layout_result_destroy(r);}std::printf("%d,%.0f\n",sample,ns/loops);}
 std::fprintf(stderr,"checksum=%llu\n",(unsigned long long)checksum);
}
#endif
