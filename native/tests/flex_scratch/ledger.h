#include <memory>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
inline size_t scratch_live=0,scratch_peak=0,scratch_allocs=0,scratch_retained=0;
template<class T> struct LedgerAllocator {
 using value_type=T;
 LedgerAllocator()=default;
 template<class U> LedgerAllocator(const LedgerAllocator<U>&){}
 T* allocate(size_t n){ auto p=std::allocator<T>{}.allocate(n);scratch_live+=n*sizeof(T);scratch_peak=std::max(scratch_peak,scratch_live);++scratch_allocs;scratch_retained=std::max(scratch_retained,n*sizeof(T));return p; }
 void deallocate(T*p,size_t n){scratch_live-=n*sizeof(T);std::allocator<T>{}.deallocate(p,n);}
 template<class U> bool operator==(const LedgerAllocator<U>&)const{return true;}
};
