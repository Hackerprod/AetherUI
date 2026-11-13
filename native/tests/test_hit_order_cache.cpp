// Manual internal regression/benchmark. No public ABI or Actions dependency.
#include "../src/layout/layout_types.h"
#include "../src/layout/input_hit_test.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <type_traits>
static_assert(!std::is_copy_constructible_v<LayoutHitOrderView>);
static_assert(!std::is_copy_assignable_v<LayoutHitOrderView>);

static long fail_after=-1;
static size_t allocations=0, allocated_bytes=0;
void* operator new(std::size_t n) {
    if(fail_after==0) throw std::bad_alloc();
    if(fail_after>0) --fail_after;
    ++allocations; allocated_bytes+=n;
    if(void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
static void check(bool b,const char* s){if(!b){std::fprintf(stderr,"FAIL %s\n",s);std::exit(1);}}
static void ok(aether_status_t s){check(s==AETHER_OK,"status");}
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){return {0,1,.8f};}
struct Fixture {
 aether_doc_t doc{}; aether_style_runtime_t rt{}; aether_style_map_t map{};
 aether_layout_engine_t engine{}; aether_layout_result_t layout{};aether_status_t layout_status=AETHER_OK;
 Fixture(size_t n,bool mixed,bool scrolling=true,bool contents=false){
  std::string html="<html><body><div id='scope'>";
  if(contents)html+="<div class='wrap'>";
  for(size_t i=0;i<n;++i) html+="<i class='c"+std::to_string(i%5)+"'></i>";
  if(contents)html+="</div>";
  html+="</div></body></html>"; ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,html.c_str()));
  aether_style_runtime_options_t options{};
  aether_style_environment_t env{400,300,1,16,0,0,0};
  ok(aether_style_runtime_create(&options,&env,nullptr,nullptr,&rt));
  std::string css="html,body{display:block;visibility:visible;pointer-events:auto;margin:0}#scope{display:block;position:relative;width:300px;height:200px;overflow:scroll}i{display:block;width:40px;height:20px}";
  if(contents)css+=".wrap{display:contents}";
  if(!scrolling)css+="#scope{overflow:visible}";
  if(mixed)css+=".c0{position:relative;z-index:-2}.c1{position:relative;z-index:3}.c2{transform:translateX(0px)}.c3{visibility:hidden}.c4{pointer-events:none}";
  int64_t sheet=0;int32_t rules=0;ok(aether_style_runtime_add_stylesheet(rt,css.c_str(),2,"cache-test",&sheet,&rules));
  aether_style_update_t* updates=nullptr;
  int count=aether_style_runtime_resolve_subtree(rt,aether_doc_get_document_element(doc),&updates);
  check(count>0,"resolve");aether_style_updates_free(updates,count);
  ok(aether_style_runtime_create_resolved_style_map(rt,doc,&map));
  ok(aether_layout_create(measure,nullptr,&engine));
  layout_status=aether_layout_layout(engine,doc,map,400,300,nullptr,nullptr,&layout);
  if(contents&&layout_status!=AETHER_OK){check(layout_status==AETHER_ERROR_INVALID_STATE&&!layout,"contents rejects without publication");return;}
  ok(layout_status);
  check(layout->root->descendants_and_self()>=int(n),"fixture contains required child count");
 }
 ~Fixture(){aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
};
static void compare_order(const aether_layout_box_s* box,const StyleLookup& lookup){
 auto old=layout_children_paint_order(box,lookup,BoxStylePolicy::snapshot);
 auto now=layout_hit_order(box,lookup,BoxStylePolicy::snapshot);
 check(old.empty()==now.empty(),"DOM classification equal");
 auto a=old.rbegin();auto b=now.rbegin();
 for(;a!=old.rend();++a,++b)check(b!=now.rend()&&a->dom_index==b->dom_index,"order equal");
 check(b==now.rend(),"length equal");
 for(auto* c:box->children)compare_order(c,lookup);
}
static aether_scroll_offset_t scroll(aether_node_t,void* p){return {0,*static_cast<float*>(p)};}
static void differential(Fixture& f){
 StyleLookup lookup(f.layout->assets,{},false); compare_order(f.layout->root,lookup);
 aether_layout_result_t clone{};ok(aether_layout_clone_snapshot(f.layout,&clone));check(!clone->hit_order_cache,"clone cache absent");aether_layout_result_destroy(clone);
 for(float offset:{0.f,50.f,190.f})for(float x:{-1.f,5.f,39.f,299.f,399.f})for(float y:{-1.f,5.f,39.f,199.f,299.f}){
  auto cached=f.layout->hit_test(x,y,lookup,BoxStylePolicy::snapshot,scroll,&offset);
  auto input=layout_hit_test_input(f.layout,f.layout->root,lookup,BoxStylePolicy::snapshot,x,y,scroll,&offset);
  auto table=std::move(f.layout->hit_order_cache);
  check(cached==f.layout->hit_test(x,y,lookup,BoxStylePolicy::snapshot,scroll,&offset),"hit equal");
  auto base=layout_hit_test_input(f.layout,f.layout->root,lookup,BoxStylePolicy::snapshot,x,y,scroll,&offset);
  check(base.owner==input.owner&&base.part==input.part,"input owner/part equal");
  f.layout->hit_order_cache=std::move(table);
 }
 // Explicit identical-base override is still an override and must use old behavior.
 StyleLookup override_lookup(f.layout->assets,f.layout->assets->base,true);
 auto verify=[&](auto&& self,const aether_layout_box_s* box)->void{
  const size_t before=allocations;
  auto old=layout_children_paint_order(box,override_lookup,BoxStylePolicy::snapshot);
  const size_t expected=allocations-before;
  const size_t start=allocations;
  auto result=layout_hit_order(box,override_lookup,BoxStylePolicy::snapshot);
  check(allocations-start==expected,"override remains fallback allocations");
  check(old.empty()==result.empty(),"override DOM");
  for(auto* c:box->children)self(self,c);
  };verify(verify,f.layout->root);
 for(auto policy:{BoxStylePolicy::snapshot,BoxStylePolicy::effective_map}){
  StyleLookup explicit_empty(f.layout->assets,{},true);
  LayoutHitOrderMetrics metrics{};layout_hit_order_metrics=&metrics;
  auto excluded=layout_hit_order(f.layout->root,explicit_empty,policy);
  layout_hit_order_metrics=nullptr;
  check(metrics.cache_hits==0&&metrics.fallbacks==1,"explicit empty override excluded");
 }
 LayoutHitOrderMetrics painting{};layout_hit_order_metrics=&painting;
 auto effective=layout_hit_order(f.layout->root,lookup,BoxStylePolicy::effective_map);
 layout_hit_order_metrics=nullptr;
 check(painting.cache_hits==0&&painting.fallbacks==1,"effective map excluded");
 auto distinct=std::make_shared<const aether::layout::LayoutAssets>(*f.layout->assets);
 StyleLookup foreign(distinct,{},false);
 LayoutHitOrderMetrics identity{};layout_hit_order_metrics=&identity;
 auto foreign_order=layout_hit_order(f.layout->root,foreign,BoxStylePolicy::snapshot);
 layout_hit_order_metrics=nullptr;
 check(identity.cache_hits==0&&identity.fallbacks==1,"distinct assets identity excluded");
}
static void oom(Fixture& f){
 f.layout->hit_order_cache.reset();
 size_t before=allocations;f.layout->build_hit_order_cache();size_t total=allocations-before;
 check(bool(f.layout->hit_order_cache),"OOM baseline build");
 size_t complete=0;
 for(size_t i=0;i<total;++i){
  f.layout->hit_order_cache.reset();fail_after=long(i);f.layout->build_hit_order_cache();fail_after=-1;
  StyleLookup lookup(f.layout->assets,{},false);
  if(f.layout->hit_order_cache){++complete;compare_order(f.layout->root,lookup);}
  (void)f.layout->hit_test(5,5,lookup,BoxStylePolicy::snapshot);
 }
 f.layout->hit_order_cache.reset();f.layout->build_hit_order_cache();
 std::printf("oom_failpoints=%zu complete_under_failure=%zu\n",total,complete);
}
static void public_callbacks(){
 for(bool input_api:{false,true})for(int action=0;action<5;++action)for(bool cached:{false,true}){
  Fixture f(25,true);if(!cached)f.layout->hit_order_cache.reset();
  struct State{Fixture* f;int action;int calls=0;aether_node_t detached=nullptr;};State state{&f,action};
  auto callback=+[](aether_node_t,void* data)->aether_scroll_offset_t{
   auto& s=*static_cast<State*>(data);if(s.calls++)return {0,0};
   auto* body=aether_doc_get_body(s.f->doc);
   if(s.action==0)aether_element_set_attribute(body,"style","width:99px");
   if(s.action==1){s.detached=aether_doc_get_element_by_id(s.f->doc,"scope");ok(aether_node_remove_child(body,s.detached));}
   if(s.action==2){aether_doc_destroy(s.f->doc);s.f->doc=nullptr;}
   if(s.action==3){aether_input_hit_part_t part{};aether_node_t owner{};ok(aether_layout_result_hit_test_input(s.f->layout,nullptr,5,5,nullptr,nullptr,&part,&owner));}
   if(s.action==4){aether_layout_result_destroy(s.f->layout);s.f->layout=nullptr;}
   return {0,0};
  };
  aether_input_hit_part_t part=99;aether_node_t owner=reinterpret_cast<aether_node_t>(1);
  if(input_api){
   auto status=aether_layout_result_hit_test_input(f.layout,nullptr,5,5,callback,&state,&part,&owner);
   if(action<3)check(status==AETHER_ERROR_INVALID_STATE&&part==AETHER_INPUT_HIT_NONE&&!owner,"public input callback mutation rejected");
   else check(status==AETHER_OK&&owner,"public input callback reentry/release pinned");
  }else{
   owner=aether_layout_result_hit_test_with_scroll_offsets(f.layout,5,5,callback,&state);
   if(action<3)check(!owner,"public DOM callback mutation rejected");
   else check(owner!=nullptr,"public DOM callback reentry/release pinned");
  }
  check(state.calls>0,"public callback called");
  if(state.detached)ok(aether_node_append_child(aether_doc_get_body(f.doc),state.detached));
 }
 std::puts("PASS public callback mutation/detach/destroy/reentry/external release cached and baseline");
}
static volatile uintptr_t sink=0;
static void benchmark(Fixture& f,size_t n,bool mixed){
 using clock=std::chrono::steady_clock;

 StyleLookup lookup(f.layout->assets,{},false);
 auto* scope=f.layout->root->children.front();
 const size_t front_index=mixed?n-4:n-1;
 check(f.layout->hit_test(5,5,lookup,BoxStylePolicy::snapshot)==scope->children.front()->element,"back-order hit proven");
 check(f.layout->hit_test(5,float(front_index*20+5),lookup,BoxStylePolicy::snapshot)==scope->children[front_index]->element,"front-order hit proven");
 check(!f.layout->hit_test(5,float(n*20+100),lookup,BoxStylePolicy::snapshot),"miss proven");
 f.layout->hit_order_cache.reset();
 size_t startalloc=allocations,startbytes=allocated_bytes;
 LayoutHitOrderMetrics cold_metrics{};layout_hit_order_metrics=&cold_metrics;
 auto t=clock::now();f.layout->build_hit_order_cache();
 layout_hit_order_metrics=nullptr;
 double cold=std::chrono::duration<double,std::micro>(clock::now()-t).count();
 std::printf("n=%zu mixed=%d cold_us=%.3f cold_allocs=%zu cold_allocated_bytes=%zu\n",n,mixed,cold,allocations-startalloc,allocated_bytes-startbytes);
 std::printf("cold_classifications=%zu cold_sorts=%zu cache_entries=%zu\n",cold_metrics.classifications,cold_metrics.sorts,f.layout->hit_order_cache->entries.size());
 std::vector<double> cold_samples; cold_samples.reserve(31);
 for(int k=0;k<31;++k){f.layout->hit_order_cache.reset();auto begin=clock::now();f.layout->build_hit_order_cache();cold_samples.push_back(std::chrono::duration<double,std::micro>(clock::now()-begin).count());}
 std::sort(cold_samples.begin(),cold_samples.end());
 size_t retained=sizeof(LayoutHitOrderTable)+f.layout->hit_order_cache->entries.bucket_count()*sizeof(void*);
 for(const auto& entry:f.layout->hit_order_cache->entries)retained+=sizeof(entry)+entry.second.capacity()*sizeof(aether::stacking::stack_item_t);
 std::printf("cold_p50_us=%.3f cold_p95_us=%.3f retained_logical_bytes=%zu (excludes hash-node links and allocator overhead)\n",cold_samples[15],cold_samples[29],retained);
 for(int mode=0;mode<2;++mode){
  auto removed=mode==0?std::move(f.layout->hit_order_cache):nullptr;
  for(float y:{5.f,mixed?float((n-4)*20+5):float(n*20-15),float(n*20+100)}){
   std::vector<double> samples;samples.reserve(101);
   size_t a=allocations,b=allocated_bytes;
   LayoutHitOrderMetrics hot{};layout_hit_order_metrics=&hot;
   for(int k=0;k<101;++k){auto begin=clock::now();auto* hit=f.layout->hit_test(5,y,lookup,BoxStylePolicy::snapshot);sink=reinterpret_cast<uintptr_t>(hit);samples.push_back(std::chrono::duration<double,std::micro>(clock::now()-begin).count());}
   layout_hit_order_metrics=nullptr;
   size_t count=allocations-a,bytes=allocated_bytes-b;
   std::sort(samples.begin(),samples.end());
   std::printf("n=%zu mixed=%d mode=%s y=%.0f p50_us=%.3f p95_us=%.3f allocs=%zu allocated_bytes=%zu\n",n,mixed,mode?"cache":"baseline",y,samples[50],samples[95],count,bytes);
   std::printf("classifications=%zu sorts=%zu cache_hits=%zu fallbacks=%zu\n",hot.classifications,hot.sorts,hot.cache_hits,hot.fallbacks);
   if(mode){check(count==0,"hot hit cache allocates nothing");check(hot.classifications==0&&hot.sorts==0&&hot.fallbacks==0,"hot no classifications sorts fallback");}
  }
  if(removed)f.layout->hit_order_cache=std::move(removed);
 }
}
int main(){
 public_callbacks();
 {Fixture contents(25,true,true,true);if(contents.layout_status==AETHER_OK){differential(contents);std::puts("PASS display:contents mixed order differential");}else std::printf("NOT_RUN display:contents differential: legacy layout rejected status=%d before cache; no semantics override\n",int(contents.layout_status));}
 {Fixture small(25,true);check(bool(small.layout->hit_order_cache),"cache published");differential(small);oom(small);differential(small);}
 for(size_t n:{100u,1000u,10000u})for(bool mixed:{false,true}){Fixture f(n,mixed,false);differential(f);benchmark(f,n,mixed);}
 std::puts("hot vector copies=0 by noncopyable borrowed-view structural guarantee (not a dynamic copy counter)");
 std::puts("PASS hit cache differential, scroll, override, OOM and benchmark");
}
