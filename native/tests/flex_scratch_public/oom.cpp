// Minimal public boundary sweep; never used for timing or broad differentials.
#define FLEX_PUBLIC_OOM
#include "runtime.cpp"
#include <new>
static long ordinal=-1,calls=0;static bool injected=false;
void* operator new(size_t n){if(ordinal>=0&&calls++==ordinal){injected=true;throw std::bad_alloc();}if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void*p,const std::nothrow_t&)noexcept{std::free(p);}void operator delete[](void*p,const std::nothrow_t&)noexcept{std::free(p);}
int main(){Fixture f(2,"mixed",false);auto retained=f.layout(101);auto expected=Manifest(f,retained).v;long failures=0;
 // Bound preflight cost before sweeping. Only ordinary new/new[] are injected.
 ordinal=1000000;calls=0;auto output=retained;auto status=aether_layout_layout(f.engine,f.doc,f.map,101,180,nullptr,nullptr,&output);long total=calls;ordinal=-1;check(status==0);aether_layout_result_destroy(output);check(total>0&&total<2000);
 for(long i=0;i<total;++i){calls=0;injected=false;ordinal=i;output=retained;status=aether_layout_layout(f.engine,f.doc,f.map,101,180,nullptr,nullptr,&output);ordinal=-1;check(injected);check(status==AETHER_ERROR_OUT_OF_MEMORY);check(output==retained);check(Manifest(f,retained).v==expected);auto retry=f.layout(101);check(Manifest(f,retry).v==expected);aether_layout_result_destroy(retry);++failures;}
 aether_layout_result_destroy(retained);std::printf("public OOM PASS points=%ld retained result unchanged; recovery fieldwise equal; new/new[] only\n",failures);
}
