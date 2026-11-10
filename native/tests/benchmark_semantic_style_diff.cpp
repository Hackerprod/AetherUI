// Standalone Linux/glibc benchmark. Link against an optimized libaether_core.
// stdout: raw CSV, 30 samples per fixture/phase following 10 warmup samples.
// malloc-family requested bytes (not resident bytes), including C++ new/PMR.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <cerrno>
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t,size_t);
extern "C" void* __libc_realloc(void*,size_t);
extern "C" void* __libc_memalign(size_t,size_t);
static thread_local bool recording=false;
static thread_local uint64_t calls=0, bytes=0;
static void count(size_t n) { if(recording){++calls;bytes+=n;} }
extern "C" void* malloc(size_t n) noexcept {count(n);return __libc_malloc(n);}
extern "C" void* calloc(size_t n,size_t s) noexcept {count(n*s);return __libc_calloc(n,s);}
extern "C" void* realloc(void* p,size_t n) noexcept {count(n);return __libc_realloc(p,n);}
extern "C" void* aligned_alloc(size_t a,size_t n) noexcept {count(n);return __libc_memalign(a,n);}
extern "C" void* memalign(size_t a,size_t n) noexcept {count(n);return __libc_memalign(a,n);}
extern "C" int posix_memalign(void** p,size_t a,size_t n) noexcept {if(a<sizeof(void*)||(a&(a-1)))return EINVAL;count(n);void* q=__libc_memalign(a,n);if(!q)return ENOMEM;*p=q;return 0;}
static void check(bool ok){if(!ok){std::fputs("benchmark API failure\n",stderr);std::abort();}}
using Clock=std::chrono::steady_clock;
struct Reading {double us;uint64_t calls,bytes;};
template<class F> Reading measure(F&& f){calls=bytes=0;auto a=Clock::now();recording=true;f();recording=false;auto b=Clock::now();return{std::chrono::duration<double,std::micro>(b-a).count(),calls,bytes};}
static const char* rich="background-image:linear-gradient(30deg,red,blue),linear-gradient(90deg,white,black);background-position:10% 20%,30% 40%;background-size:50% 60%,70% 80%;box-shadow:1px 2px 3px 4px red,2px 3px 4px 5px blue;text-shadow:1px 2px 3px red,2px 3px 4px blue;backdrop-filter:blur(3px) blur(2px);transform:translate(2px,3px) rotate(10deg) scale(1.1);border:2px solid red;border-radius:5px;";
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_node_t body{};
 Fixture(int n,bool isrich){std::string html="<body class='a'>";for(int i=0;i<n;++i)html+="<div class='item'></div>";html+="</body>";
 check(aether_doc_create(&doc)==AETHER_OK);check(aether_doc_parse_html(doc,html.c_str())==AETHER_OK);body=aether_doc_get_body(doc);
 aether_style_environment_t env{800,600,1,16,0,0,0};check(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK);
 std::string css=".item{display:block;height:20px;";if(isrich)css+=rich;css+="}.a .item{width:41px;opacity:0.8;cursor:pointer}.isolated .item{width:41px;opacity:0.6;cursor:pointer}.mixed .item{opacity:0.6;cursor:crosshair}";
 int64_t handle=0;check(aether_style_runtime_add_stylesheet(runtime,css.c_str(),2,"benchmark",&handle,nullptr)==AETHER_OK);
 }
 ~Fixture(){aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 void state(const char* s){check(aether_element_set_attribute(body,"class",s)==AETHER_OK);}
 void resolve(){aether_style_update_t* updates=nullptr;auto n=aether_style_runtime_resolve_subtree(runtime,body,&updates);check(n>=0);aether_style_updates_free(updates,n);}
 void contextual(){aether_style_transaction_t tx=nullptr;aether_style_revision_t rev=nullptr;check(aether_style_runtime_prepare_revision(runtime,body,nullptr,&tx)==AETHER_OK);check(aether_style_runtime_commit_revision(runtime,tx,&rev)==AETHER_OK);aether_style_transaction_destroy(tx);aether_style_revision_destroy(rev);}
};
static void emit(const char* variant,int n,const char* scenario,const char* phase,int sample,Reading r){if(sample>=0)std::printf("%s,%d,%s,%s,%d,%.6f,%llu,%llu\n",variant,n,scenario,phase,sample,r.us,(unsigned long long)r.calls,(unsigned long long)r.bytes);}
static void validate_rich_fixture(){
 Fixture f(1,true);aether_style_transaction_t tx=nullptr;aether_style_revision_t rev=nullptr;
 check(aether_style_runtime_prepare_revision(f.runtime,f.body,nullptr,&tx)==AETHER_OK);
 check(aether_style_runtime_commit_revision(f.runtime,tx,&rev)==AETHER_OK);
 bool found=false;
 for(int i=0;i<aether_style_revision_style_count(rev);++i){aether_computed_style_handle_t h=nullptr;check(aether_style_revision_get_style(rev,i,nullptr,&h,nullptr)==AETHER_OK);aether_computed_style_t s{};check(aether_computed_style_copy(h,&s)==AETHER_OK);
 if(s.background_layers.count==2){check(s.box_shadows.count==2&&s.text_shadows.count==2&&s.transforms.count==3&&s.backdrop_filters.count==2);found=true;}
 aether_computed_style_free(&s);aether_computed_style_handle_destroy(h);}
 check(found);aether_style_transaction_destroy(tx);aether_style_revision_destroy(rev);
}
int main(){validate_rich_fixture();std::puts("fixture,nodes,scenario,phase,sample,us,alloc_calls,alloc_bytes");for(bool isrich:{false,true})for(int n:{1,1000}){
 const char* variant=isrich?"rich":"simple";
 for(int i=-10;i<30;++i){Fixture* f=nullptr;auto r=measure([&]{f=new Fixture(n,isrich);});emit(variant,n,"initialization","init",i,r);delete f;}
 for(const char* scenario:{"noop","isolated","mixed"})for(bool contextual:{false,true}){
 Fixture f(n,isrich);if(contextual)f.contextual();else f.resolve();
 for(int i=-10;i<30;++i){f.state("a");if(contextual)f.contextual();else f.resolve();f.state(std::string(scenario)=="noop"?"a":scenario);
 if(!contextual){auto r=measure([&]{f.resolve();});emit(variant,n,scenario,"resolve_legacy",i,r);}
 else {aether_style_transaction_t tx=nullptr;aether_style_revision_t rev=nullptr;
 auto prep=measure([&]{check(aether_style_runtime_prepare_revision(f.runtime,f.body,nullptr,&tx)==AETHER_OK);});
 auto commit=measure([&]{check(aether_style_runtime_commit_revision(f.runtime,tx,&rev)==AETHER_OK);});
 emit(variant,n,scenario,"prepare",i,prep);emit(variant,n,scenario,"commit",i,commit);
 aether_style_transaction_destroy(tx);aether_style_revision_destroy(rev);}
 }
 }
}}
