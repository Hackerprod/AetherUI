#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/css/frozen_measure_read.h"
#include "../../native/src/css/computed_style_handle.h"
#include <aether/aether_contextual_style.h>
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <cmath>
static long budget=-1;
static bool delivered=false;
void* operator new(size_t n) {
    if(budget>=0 && budget--==0){delivered=true;throw std::bad_alloc();}
    if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void*p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete[](void*p,const std::nothrow_t&)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t alignment) {
    if(budget>=0 && budget--==0){delivered=true;throw std::bad_alloc();}
    void*p=nullptr;size_t a=static_cast<size_t>(alignment);if(a<sizeof(void*))a=sizeof(void*);
    if(posix_memalign(&p,a,n?n:1))throw std::bad_alloc();return p;
}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void*p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void*p,size_t,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void*p,size_t,std::align_val_t)noexcept{std::free(p);}
using namespace aether::style;
static void check(aether_computed_style_handle_t handle) {
 assert(handle&&handle->measures);
 assert(std::abs(aether_css_length_resolve(handle->measures->scalars().row_gap,360,16,16,800,600)-17.2f)<.01f);
 assert(std::abs(aether_css_length_resolve(handle->measures->scalars().column_gap,360,16,16,800,600)-17.2f)<.01f);
}
static void literal_revision(aether_style_revision_t revision,aether_node_t target) {
    bool found=false;
    for(int i=0;i<aether_style_revision_style_count(revision);++i){
        aether_node_t node{};aether_computed_style_handle_t style{};
        assert(aether_style_revision_get_style(revision,i,&node,&style,nullptr)==AETHER_OK);
        if(node==target){assert(style->measures->scalars().row_gap.value==7);assert(style->measures->scalars().column_gap.value==7);found=true;}
        aether_computed_style_handle_destroy(style);
    }
    assert(found);
}
static size_t runtime_sweep(){
    aether_doc_t doc{};assert(aether_doc_create(&doc)==AETHER_OK);
    assert(aether_doc_parse_html(doc,"<body><div id='target' style='gap:7px'></div></body>")==AETHER_OK);
    aether_style_runtime_options_t options{};aether_style_environment_t env{800,600,1,16,0,0,0};
    aether_style_runtime_t runtime{};assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
    aether_style_transaction_t first{};assert(aether_style_runtime_prepare_revision(runtime,aether_doc_get_body(doc),&env,&first)==AETHER_OK);
    aether_style_revision_t old{};assert(aether_style_runtime_commit_revision(runtime,first,&old)==AETHER_OK);aether_style_transaction_destroy(first);
    auto target=aether_doc_get_element_by_id(doc,"target");literal_revision(old,target);
    const auto generation=aether_style_revision_generation(old);
    assert(aether_element_set_attribute(target,"style",R"(gap:c\61 lc(10p\78  + 2%))")==AETHER_OK);
    size_t failures=0;bool done=false;
    for(long i=0;i<10000;++i){
        aether_style_transaction_t tx=reinterpret_cast<aether_style_transaction_t>(1);delivered=false;budget=i;
        const auto status=aether_style_runtime_prepare_revision(runtime,aether_doc_get_body(doc),&env,&tx);budget=-1;
        // A retained map/revision and the runtime publication must both remain old.
        literal_revision(old,target);
        aether_style_revision_t current{};assert(aether_style_runtime_retain_revision(runtime,&current)==AETHER_OK);
        assert(aether_style_revision_generation(current)==generation);literal_revision(current,target);aether_style_revision_destroy(current);
        if(status==AETHER_OK){
            assert(!delivered&&tx);
            aether_style_revision_t committed{};
            // Publication is allocation-free for this no-diagnostic fixture.
            delivered=false;budget=0;
            const auto committed_status=aether_style_runtime_commit_revision(runtime,tx,&committed);budget=-1;
            assert(committed_status==AETHER_OK&&!delivered);
            assert(aether_style_revision_generation(committed)==generation+1);
            bool found=false;
            for(int j=0;j<aether_style_revision_style_count(committed);++j){
                aether_node_t node{};aether_computed_style_handle_t style{};
                assert(aether_style_revision_get_style(committed,j,&node,&style,nullptr)==AETHER_OK);
                if(node==target){check(style);found=true;}aether_computed_style_handle_destroy(style);
            }
            assert(found);literal_revision(old,target);
            aether_style_revision_destroy(committed);aether_style_transaction_destroy(tx);done=true;break;
        }
        assert(status==AETHER_ERROR_OUT_OF_MEMORY&&delivered&&!tx);++failures;
    }
    assert(done&&failures>100);aether_style_revision_destroy(old);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
    return failures;
}
int main(){std::printf("H9 runtime atomic OOM=%zu\n",runtime_sweep());}
