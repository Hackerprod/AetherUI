#pragma once
// Test-only definitions: include in exactly one TU of a product link-unit test.
// Requires --wrap for all five C allocation families and static C++ runtime.
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>

namespace fault_alloc {
enum Origin : unsigned { c, scalar, array, scalar_nothrow, array_nothrow,
    scalar_aligned, array_aligned, scalar_aligned_nothrow, array_aligned_nothrow, origin_count };
enum Family : unsigned { malloc_family, calloc_family, realloc_family,
    aligned_family, posix_family, family_count };
inline constexpr uint64_t disabled = UINT64_MAX;
struct Counts {
    uint64_t requests=0, successes=0, null_results=0, injected_failures=0;
    uint64_t origins[origin_count]{}, families[family_count]{};
    uint64_t failed_ordinal=disabled;
    Origin failed_origin=c;
    Family failed_family=malloc_family;
    bool invalid=false;
};
inline thread_local Counts counts{};
inline thread_local uint64_t fail_ordinal=disabled;
inline thread_local unsigned depth=0, wrapper_depth=0;
inline thread_local Origin origin=c;
inline void increment(uint64_t& value) noexcept {
    if(value==UINT64_MAX) counts.invalid=true; else ++value;
}
inline void reset(uint64_t ordinal=disabled) noexcept {
    if(depth || wrapper_depth) { counts.invalid=true; return; }
    counts={}; fail_ordinal=ordinal; origin=c;
}
inline void begin() noexcept { if(depth==UINT32_MAX) counts.invalid=true; else ++depth; }
inline void end() noexcept { if(!depth) counts.invalid=true; else --depth; }
struct Scope {
    Scope() noexcept { begin(); }
    ~Scope() { end(); }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
inline bool valid() noexcept { return !counts.invalid && !depth && !wrapper_depth; }
struct Tag {
    Origin old;
    explicit Tag(Origin value) noexcept : old(origin) { origin=value; }
    ~Tag() { origin=old; }
};
struct Wrapper {
    bool active, fail=false;
    explicit Wrapper(Family family) noexcept : active(depth!=0) {
        if(wrapper_depth || wrapper_depth==UINT32_MAX) counts.invalid=true;
        if(wrapper_depth!=UINT32_MAX) ++wrapper_depth;
        if(!active) return;
        const auto ordinal=counts.requests;
        increment(counts.requests); increment(counts.origins[origin]); increment(counts.families[family]);
        if(!counts.invalid && ordinal==fail_ordinal && !counts.injected_failures) {
            fail=true; increment(counts.injected_failures);
            counts.failed_ordinal=ordinal; counts.failed_origin=origin; counts.failed_family=family;
        }
    }
    void result(bool success) noexcept {
        if(active) increment(success ? counts.successes : counts.null_results);
    }
    ~Wrapper() { if(wrapper_depth) --wrapper_depth; else counts.invalid=true; }
};
}
extern "C" {
void* __real_malloc(size_t); void* __real_calloc(size_t,size_t); void* __real_realloc(void*,size_t);
void* __real_aligned_alloc(size_t,size_t); int __real_posix_memalign(void**,size_t,size_t);
void* __wrap_malloc(size_t n) {
    fault_alloc::Wrapper w(fault_alloc::malloc_family);
    void* p=nullptr; if(w.fail) errno=ENOMEM; else p=__real_malloc(n);
    w.result(p!=nullptr); return p;
}
void* __wrap_calloc(size_t n,size_t s) {
    fault_alloc::Wrapper w(fault_alloc::calloc_family);
    void* p=nullptr; if(w.fail) errno=ENOMEM; else p=__real_calloc(n,s);
    w.result(p!=nullptr); return p;
}
void* __wrap_realloc(void* p,size_t n) {
    fault_alloc::Wrapper w(fault_alloc::realloc_family);
    void* q=nullptr; if(w.fail) errno=ENOMEM; else q=__real_realloc(p,n);
    w.result(q!=nullptr); return q;
}
void* __wrap_aligned_alloc(size_t a,size_t n) {
    fault_alloc::Wrapper w(fault_alloc::aligned_family);
    void* p=nullptr; if(w.fail) errno=ENOMEM; else p=__real_aligned_alloc(a,n);
    w.result(p!=nullptr); return p;
}
int __wrap_posix_memalign(void** p,size_t a,size_t n) {
    fault_alloc::Wrapper w(fault_alloc::posix_family);
    const int status=w.fail ? ENOMEM : __real_posix_memalign(p,a,n);
    w.result(status==0); return status;
}
}
namespace fault_alloc {
inline void* allocate(size_t size,Origin selected,size_t alignment=0) {
    for(;;) {
        void* p=nullptr;
        { // Exception allocation and handlers are NOT attributed to this new.
            Tag tag(selected);
            if(alignment) {
                if(__wrap_posix_memalign(&p,std::max(alignment,sizeof(void*)),size?size:1)!=0) p=nullptr;
            } else p=__wrap_malloc(size?size:1);
        }
        if(p) return p;
        auto handler=std::get_new_handler();
        if(!handler) throw std::bad_alloc();
        handler();
    }
}
}
void* operator new(size_t n){return fault_alloc::allocate(n,fault_alloc::scalar);}
void* operator new[](size_t n){return fault_alloc::allocate(n,fault_alloc::array);}
void* operator new(size_t n,const std::nothrow_t&) noexcept {try{return fault_alloc::allocate(n,fault_alloc::scalar_nothrow);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {try{return fault_alloc::allocate(n,fault_alloc::array_nothrow);}catch(...){return nullptr;}}
void* operator new(size_t n,std::align_val_t a){return fault_alloc::allocate(n,fault_alloc::scalar_aligned,size_t(a));}
void* operator new[](size_t n,std::align_val_t a){return fault_alloc::allocate(n,fault_alloc::array_aligned,size_t(a));}
void* operator new(size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return fault_alloc::allocate(n,fault_alloc::scalar_aligned_nothrow,size_t(a));}catch(...){return nullptr;}}
void* operator new[](size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return fault_alloc::allocate(n,fault_alloc::array_aligned_nothrow,size_t(a));}catch(...){return nullptr;}}
void operator delete(void* p) noexcept {std::free(p);} void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);} void operator delete[](void* p,size_t) noexcept {std::free(p);}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);} void operator delete[](void* p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete(void* p,std::align_val_t) noexcept {std::free(p);} void operator delete[](void* p,std::align_val_t) noexcept {std::free(p);}
void operator delete(void* p,size_t,std::align_val_t) noexcept {std::free(p);} void operator delete[](void* p,size_t,std::align_val_t) noexcept {std::free(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept {std::free(p);} void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept {std::free(p);}

namespace fault_alloc {
inline void check(bool value,const char* message) {
    if(!value) { std::fprintf(stderr,"FAIL allocator control: %s\n",message); std::abort(); }
}
inline void* volatile escaped=nullptr;
__attribute__((noinline)) inline void* positive(unsigned family,size_t n) {
    switch(family) {
    case 0:return std::malloc(n); case 1:return std::calloc(1,n);
    case 2:return std::realloc(nullptr,n); case 3:return std::aligned_alloc(64,((n+63)/64)*64);
    case 4:{void* p=nullptr;return ::posix_memalign(&p,64,n)==0?p:nullptr;}
    case 5:return ::operator new(n); case 6:return ::operator new[](n);
    case 7:return ::operator new(n,std::nothrow); case 8:return ::operator new[](n,std::nothrow);
    case 9:return ::operator new(n,std::align_val_t(64)); case 10:return ::operator new[](n,std::align_val_t(64));
    case 11:return ::operator new(n,std::align_val_t(64),std::nothrow);
    default:return ::operator new[](n,std::align_val_t(64),std::nothrow);
    }
}
inline void positive_controls() {
    check(std::get_new_handler()==nullptr,"test requires default null new_handler; never replaces it");
    constexpr size_t size=257;
    for(unsigned family=0;family<13;++family) {
        const auto selected=family<5?c:static_cast<Origin>(family-4);
        reset(); { Scope scope; escaped=positive(family,size); }
        check(valid() && escaped && counts.requests==1 && counts.successes==1 && counts.origins[selected]==1,
            "success counted exactly once");
        std::free(escaped); escaped=nullptr;
        bool threw=false;
        reset(0);
        { Scope scope; try { escaped=positive(family,size); } catch(const std::bad_alloc&) { threw=true; } }
        const bool throwing=family==5 || family==6 || family==9 || family==10;
        check(valid() && !escaped && threw==throwing && counts.injected_failures==1 &&
            counts.failed_ordinal==0 && counts.failed_origin==selected && counts.null_results==1,
            "selected failure and required throw/null behavior");
        if(family>=5) check(counts.origins[selected]==1,"new primitive not double-counted with C wrapper");
        else check(counts.requests==1,"C failure exactly one request");
        check(counts.successes+counts.null_results==counts.requests,"every request has one outcome");
        std::printf("allocator family=%u success_requests=1 failed_requests=%llu injected=1\n",family,
            static_cast<unsigned long long>(counts.requests));
    }
    reset(0);
    auto* block=static_cast<unsigned char*>(std::malloc(size)); check(block!=nullptr,"realloc fixture");
    std::memset(block,0x5a,size); void* replacement=nullptr;
    { Scope scope; replacement=std::realloc(block,size*2); }
    check(valid() && !replacement && counts.requests==1 && counts.injected_failures==1,"realloc injected");
    for(size_t i=0;i<size;++i) check(block[i]==0x5a,"failed realloc retains content");
    std::free(block);
    int sentinel=0; void* output=&sentinel; int result=0; errno=EDOM; reset(0);
    { Scope scope; result=::posix_memalign(&output,64,size); }
    check(valid() && result==ENOMEM && output==&sentinel && errno==EDOM && counts.requests==1,
        "posix failure preserves output and errno");
    reset(0); escaped=positive(0,size); check(counts.requests==0 && counts.injected_failures==0,"disabled outside scope"); std::free(escaped);
    reset(0); { Scope outer; { Scope inner; escaped=positive(0,size); } }
    check(valid() && !escaped && counts.requests==1 && counts.injected_failures==1,"nested once");
    reset(0); { Scope scope; escaped=positive(0,size); check(!escaped,"first selected fails"); escaped=positive(0,size); }
    check(valid() && escaped && counts.requests==2 && counts.injected_failures==1,"fault once"); std::free(escaped);
    reset(); { Scope scope; wrapper_depth=1; escaped=positive(0,size); wrapper_depth=0; }
    check(!valid(),"recursion invalidates measurement"); std::free(escaped);
    reset(); { Scope scope; counts.requests=UINT64_MAX; escaped=positive(0,size); }
    check(!valid(),"counter overflow invalidates measurement"); std::free(escaped);
    reset(); end(); check(!valid(),"underflow invalidates measurement"); reset();
    std::thread thread([] { check(!depth && !counts.requests,"TLS starts independent"); reset(0);
        { Scope scope; escaped=nullptr; void* p=positive(0,size); check(!p,"worker fault"); }
        check(valid() && counts.injected_failures==1,"worker ledger valid"); });
    thread.join(); check(counts.requests==0,"worker ledger cannot contaminate parent");
    reset(); { Scope scope; } check(valid() && !counts.requests,"empty scope");
}
}
