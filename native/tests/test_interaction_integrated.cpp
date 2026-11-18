// Private executable only. Production DSO does not expose these hooks.
#define AETHER_INTERACTION_TESTING 1
#include "abi/interaction_test_hooks.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>
#include <thread>
#include <climits>
#include <set>
namespace t=aether::interaction_test;
static long fail_at=-1,alloc_index=0;static bool fail_triggered=false;
static void fail(){if(fail_at>=0&&alloc_index++==fail_at){fail_triggered=true;throw std::bad_alloc{};}}
void* operator new(std::size_t n){fail();if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc{};}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){fail();void*p=nullptr;if(posix_memalign(&p,std::max(std::size_t(a),sizeof(void*)),n?n:1))throw std::bad_alloc{};return p;}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}void operator delete(void*p,std::align_val_t)noexcept{std::free(p);}void operator delete[](void*p,std::align_val_t)noexcept{std::free(p);}void operator delete(void*p,std::size_t,std::align_val_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t,std::align_val_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void*p,const std::nothrow_t&)noexcept{std::free(p);}void operator delete[](void*p,const std::nothrow_t&)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void*p,std::align_val_t,const std::nothrow_t&)noexcept{std::free(p);}void operator delete[](void*p,std::align_val_t,const std::nothrow_t&)noexcept{std::free(p);}
template<class T> T init(){T v{};v.struct_size=sizeof(v);return v;}
static std::vector<unsigned char> read(const char*p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static const char*html="<html><body style=\"font-size:16px;line-height:24px\"><input type=\"text\" style=\"width:160px\"><input type=\"password\" style=\"width:160px\"><button type=\"button\" style=\"width:160px\">Go</button></body></html>";
static aether_interaction_options_v1 options;
static aether_interaction_t create(){aether_interaction_t s{};const auto status=aether_interaction_create(192,96,html,std::strlen(html),&options,&s);if(status)std::fprintf(stderr,"create code=%d\n",status);assert(status==AETHER_OK);return s;}
static auto state(aether_interaction_t s){auto v=init<aether_interaction_state_v1>();assert(aether_interaction_state(s,&v)==AETHER_OK);return v;}
static auto event(unsigned kind,unsigned key=0,const char* text=nullptr){auto e=init<aether_interaction_event_v1>();e.kind=kind;e.key=key;e.text=reinterpret_cast<const uint8_t*>(text);e.text_length=text?std::strlen(text):0;return e;}
static auto dispatch(aether_interaction_t s,aether_interaction_event_v1 e,bool changed=true,unsigned activation=0){auto before=state(s);auto r=init<aether_interaction_result_v1>();assert(aether_interaction_dispatch(s,&e,&r)==AETHER_OK);auto after=state(s);assert(r.changed==unsigned(changed)&&r.activated_control_id==activation);assert(after.state_version==before.state_version+unsigned(changed)&&r.state_version==after.state_version);assert(after.sequence==before.sequence&&after.frame_version==before.frame_version);return r;}
static void render(aether_interaction_t s){auto before=state(s);auto r=init<aether_interaction_result_v1>();auto code=aether_interaction_render(s,&r);if(code)std::fprintf(stderr,"render code=%d version=%llu\n",code,(unsigned long long)before.state_version);assert(code==AETHER_OK);auto after=state(s);bool dirty=before.frame_version!=before.state_version;assert(r.changed==unsigned(dirty)&&!r.activated_control_id);assert(after.sequence==before.sequence+unsigned(dirty)&&after.state_version==before.state_version&&after.frame_version==after.state_version);}
static void tab(aether_interaction_t s){dispatch(s,event(4,1));}
static void focus(aether_interaction_t s,unsigned id){for(unsigned n=0;state(s).focus_id!=id;++n){assert(n<4);tab(s);}}
static auto frame(aether_interaction_t s){auto i=init<aether_interaction_frame_info_v1>();assert(aether_interaction_copy_frame(s,&i,nullptr,0)==AETHER_OK);std::vector<unsigned char> p(i.byte_count);assert(aether_interaction_copy_frame(s,&i,p.data(),p.size())==AETHER_OK);return p;}
static void unchanged(aether_interaction_t s,const aether_interaction_state_v1& old,const std::vector<unsigned char>& bytes){auto now=state(s);assert(!std::memcmp(&now,&old,sizeof(old)));assert(frame(s)==bytes);}
static void resize(aether_interaction_t s,int w,int h){auto old=state(s);auto r=init<aether_interaction_result_v1>();assert(aether_interaction_resize(s,w,h,&r)==AETHER_OK);auto now=state(s);bool change=old.width!=w||old.height!=h;assert(r.changed==unsigned(change)&&!r.activated_control_id&&now.state_version==old.state_version+unsigned(change)&&now.sequence==old.sequence);}
static void curve(aether_interaction_t s,unsigned cycle){auto b=t::storage(s);std::size_t total=0;std::printf("curve cycle=%u current=%zu peak=%zu",cycle,b.current,b.peak);for(std::size_t j=0;j<b.categories.size();++j){auto c=b.categories[j];assert(c.current<=c.peak);total+=c.current;std::printf(" cat%zu=%zu/%zu",j,c.current,c.peak);}assert(total==b.current&&b.current<=b.peak&&b.peak<=b.limit);std::puts("");}
static void stress(){auto s=create();render(s);std::size_t phase_max=0;unsigned activations=0;const char*scalars[]={"a","é","ñ","ÿ"};
 for(unsigned i=0;i<10000;++i){
  if(i==5000){for(unsigned id=1;id<=2;++id){focus(s,id);std::string full(255,'x');dispatch(s,event(6,0,full.c_str()));render(s);}phase_max=0;}
  for(unsigned id=1;id<=2;++id){focus(s,id);dispatch(s,event(6,0,scalars[i%4]));render(s);dispatch(s,event(4,4));render(s);}
  if(i%100==0){focus(s,3);dispatch(s,event(4,2),true,3);++activations;dispatch(s,event(4,2),false);dispatch(s,event(5,2));render(s);resize(s,193,97);render(s);resize(s,192,96);render(s);}
  // Normalize focus and dimensions before comparing retained storage.
  focus(s,1);render(s);auto b=t::storage(s);if(i%5000<8)phase_max=std::max(phase_max,b.current);else assert(b.current<=phase_max);
  if(i%1000==0||i==4999||i==9999)curve(s,i+1);
 }
 auto final=state(s);assert(activations==100&&final.state_version==70407&&final.sequence==50303);std::printf("integrated cycles=10000 activations=%u version=%llu sequence=%llu PASS\n",activations,(unsigned long long)final.state_version,(unsigned long long)final.sequence);assert(aether_interaction_destroy(s)==AETHER_OK);
}
static void quota_and_exhaustion(){auto s=create();focus(s,1);render(s);auto old=state(s);auto bytes=frame(s);auto b=t::storage(s);auto r=init<aether_interaction_result_v1>();auto sentinel=r;auto e=event(6,0,"a");t::storage_limit(s,b.current);assert(aether_interaction_dispatch(s,&e,&r)==AETHER_ERROR_LIMIT_EXCEEDED);assert(!std::memcmp(&r,&sentinel,sizeof(r)));unchanged(s,old,bytes);assert(t::storage(s).current==b.current);t::storage_limit(s,b.limit);dispatch(s,e);old=state(s);bytes=frame(s);b=t::storage(s);t::storage_limit(s,b.current);assert(aether_interaction_render(s,&r)==AETHER_ERROR_LIMIT_EXCEEDED);unchanged(s,old,bytes);assert(t::storage(s).current==b.current);t::storage_limit(s,b.limit);render(s);
 old=state(s);bytes=frame(s);b=t::storage(s);t::storage_limit(s,b.current);assert(aether_interaction_resize(s,193,97,&r)==AETHER_ERROR_LIMIT_EXCEEDED);unchanged(s,old,bytes);assert(t::storage(s).current==b.current);t::storage_limit(s,b.limit);resize(s,193,97);render(s);
 t::state_version(s,UINT64_MAX);old=state(s);bytes=frame(s);assert(aether_interaction_dispatch(s,&e,&r)==AETHER_ERROR_EXHAUSTED);unchanged(s,old,bytes);assert(aether_interaction_resize(s,193,97,&r)==AETHER_OK&&!r.changed);t::state_version(s,old.frame_version);t::sequence(s,UINT64_MAX);dispatch(s,event(6,0,"b"));old=state(s);bytes=frame(s);assert(aether_interaction_render(s,&r)==AETHER_ERROR_EXHAUSTED);unchanged(s,old,bytes);t::sequence(s,1);render(s);assert(aether_interaction_destroy(s)==AETHER_OK);std::puts("quota rollback/retry and UINT64 exhaustion PASS");}
static void owner_thread(){auto s=create();render(s);auto old=state(s);auto bytes=frame(s);std::thread other([&]{auto r=init<aether_interaction_result_v1>();auto st=init<aether_interaction_state_v1>();auto f=init<aether_interaction_frame_info_v1>();auto c=init<aether_interaction_control_info_v1>();unsigned count=99;auto e=event(4,1);assert(aether_interaction_state(s,&st)==AETHER_ERROR_WRONG_THREAD);assert(aether_interaction_control_count(s,&count)==AETHER_ERROR_WRONG_THREAD&&count==99);assert(aether_interaction_control_info(s,0,&c)==AETHER_ERROR_WRONG_THREAD);assert(aether_interaction_dispatch(s,&e,&r)==AETHER_ERROR_WRONG_THREAD);assert(aether_interaction_resize(s,193,97,&r)==AETHER_ERROR_WRONG_THREAD);assert(aether_interaction_render(s,&r)==AETHER_ERROR_WRONG_THREAD);assert(aether_interaction_copy_frame(s,&f,nullptr,0)==AETHER_ERROR_WRONG_THREAD);assert(aether_interaction_destroy(s)==AETHER_ERROR_WRONG_THREAD);});other.join();unchanged(s,old,bytes);assert(aether_interaction_destroy(s)==AETHER_OK);std::puts("owner-thread eight public operations PASS");}
static void counts(){for(unsigned op=0;op<4;++op){aether_interaction_t s{};auto r=init<aether_interaction_result_v1>();auto e=event(6,0,"Ficticio1234567890123456789");if(op){s=create();focus(s,1);render(s);if(op==2)dispatch(s,e);}alloc_index=0;fail_at=LONG_MAX;int code=op==0?aether_interaction_create(192,96,html,std::strlen(html),&options,&s):op==1?aether_interaction_dispatch(s,&e,&r):op==2?aether_interaction_render(s,&r):aether_interaction_resize(s,193,97,&r);fail_at=-1;assert(code==AETHER_OK);std::printf("allocation count operation=%u total=%ld\n",op,alloc_index);assert(aether_interaction_destroy(s)==AETHER_OK);}}
static void oom(){
 for(unsigned op=0;op<4;++op){
  const auto setup=[&]{auto s=create();if(op){focus(s,1);render(s);if(op==2)dispatch(s,event(6,0,"Ficticio1234567890123456789"));}return s;};
  auto e=event(6,0,"Ficticio1234567890123456789");auto r=init<aether_interaction_result_v1>();aether_interaction_t s=op?setup():nullptr;
  const auto action=[&]{return op==0?aether_interaction_create(192,96,html,std::strlen(html),&options,&s):op==1?aether_interaction_dispatch(s,&e,&r):op==2?aether_interaction_render(s,&r):aether_interaction_resize(s,193,97,&r);};
  alloc_index=0;fail_at=LONG_MAX;auto code=action();fail_at=-1;assert(code==AETHER_OK);const long total=alloc_index;assert(aether_interaction_destroy(s)==AETHER_OK);s=op?setup():nullptr;
  std::set<long> points;if(op){for(long j=0;j<total;++j)points.insert(j);}else {for(long j=0;j<16;++j){points.insert(j);points.insert(total-1-j);}for(long j=0;j<=32;++j)points.insert((total-1)*j/32);}
  std::printf("OOM operation=%u total=%ld selected=%zu coverage=%s indices=",op,total,points.size(),op?"full baseline range attempted":"partial endpoints16+32quantiles");for(auto j:points)std::printf("%ld,",j);std::puts("");
  aether_interaction_state_v1 old{};std::vector<unsigned char> bytes;std::size_t current=0;if(op){old=state(s);bytes=frame(s);current=t::storage(s).current;}
  unsigned failures=0,tolerated=0,unreached=0;for(auto point:points){r=init<aether_interaction_result_v1>();auto sentinel=r;alloc_index=0;fail_triggered=false;fail_at=point;code=action();fail_at=-1;
   if(code==AETHER_OK){if(fail_triggered)++tolerated;else ++unreached;std::printf("OOM operation=%u point=%ld success triggered=%u observed=%ld\n",op,point,unsigned(fail_triggered),alloc_index);if(op){auto now=state(s);assert(now.state_version==old.state_version+(op==2?0:1));assert(now.sequence==old.sequence+(op==2?1:0));if(op!=2)assert(frame(s)==bytes);assert(!r.activated_control_id);}assert(aether_interaction_destroy(s)==AETHER_OK);s=op?setup():nullptr;if(op){old=state(s);bytes=frame(s);current=t::storage(s).current;}continue;}
   if(code!=AETHER_ERROR_OUT_OF_MEMORY){std::fprintf(stderr,"OOM operation=%u point=%ld code=%d\n",op,point,code);std::abort();}++failures;assert(fail_triggered);std::printf("OOM operation=%u point=%ld failure reached=1 observed=%ld\n",op,point,alloc_index);assert(!std::memcmp(&sentinel,&r,sizeof(r)));
   if(!op)assert(!s);else {unchanged(s,old,bytes);assert(t::storage(s).current==current);}
   if(op==1){assert(aether_interaction_destroy(s)==AETHER_OK);s=setup();old=state(s);bytes=frame(s);current=t::storage(s).current;}
  }
  assert(action()==AETHER_OK);assert(aether_interaction_destroy(s)==AETHER_OK);std::printf("OOM operation=%u failure_points=%u tolerated_success=%u unreached=%u unchanged plus final retry PASS\n",op,failures,tolerated,unreached);
 }
}

int main(int argc,char**argv){std::setvbuf(stdout,nullptr,_IONBF,0);assert(argc==3||argc==4);auto regular=read(argv[1]),bold=read(argv[2]);options=init<aether_interaction_options_v1>();options.regular_font=regular.data();options.regular_font_size=regular.size();options.bold_font=bold.data();options.bold_font_size=bold.size();if(argc==4&&!std::strcmp(argv[3],"count")){counts();return 0;}owner_thread();quota_and_exhaustion();if(argc==3||std::strcmp(argv[3],"stress"))oom();if(argc==3||std::strcmp(argv[3],"oom"))stress();std::puts("instrumented storage only; NOT total-memory/RSS proof; no G1 declaration");}
