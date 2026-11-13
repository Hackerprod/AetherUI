// Diagnostic requested-allocation accounting, not allocator RSS accounting.
#include <cstdlib>
#include <new>
#include <cstddef>
#include <cstdint>
static size_t allocation_calls=0, allocated_bytes=0, live_bytes=0, peak_bytes=0;
struct alignas(std::max_align_t) AllocationHeader { size_t bytes; void* raw; };
static void* allocate(size_t n,size_t align) {
 auto* raw=std::malloc(sizeof(AllocationHeader)+(n?n:1)+align-1);
 if(!raw)throw std::bad_alloc();
 auto address=(reinterpret_cast<uintptr_t>(raw)+sizeof(AllocationHeader)+align-1)&~(align-1);
 auto* h=reinterpret_cast<AllocationHeader*>(address)-1;
 h->bytes=n;h->raw=raw;++allocation_calls;allocated_bytes+=n;
 live_bytes+=n;if(live_bytes>peak_bytes)peak_bytes=live_bytes;return h+1;
}
void* operator new(size_t n){return allocate(n,alignof(std::max_align_t));}
void* operator new[](size_t n){return ::operator new(n);}
void* operator new(size_t n,std::align_val_t a){return allocate(n,static_cast<size_t>(a));}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p)noexcept {if(p){auto* h=static_cast<AllocationHeader*>(p)-1;live_bytes-=h->bytes;std::free(h->raw);}}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
void operator delete(void* p,std::align_val_t)noexcept{::operator delete(p);}
void operator delete[](void* p,std::align_val_t)noexcept{::operator delete(p);}
void operator delete(void* p,size_t,std::align_val_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t,std::align_val_t)noexcept{::operator delete(p);}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main original_baseline_main
#include "test_native_hover_baseline.cpp"
#undef main
#pragma GCC diagnostic pop
int main(int argc,char** argv){
 int n=argc>1?std::atoi(argv[1]):18; Fixture f(n,0);
 std::cout<<"phase,cards,mode,allocation_calls,requested_bytes,live_delta,peak_above_start,wall_ns,compute_original\n";
 for(int phase=0;phase<3;++phase){
  // Keep the published revision alive while the following publication is built.
  auto previous_revision=f.rt->revision;
  if(phase==2)f.state(1);
  auto calls=allocation_calls, bytes=allocated_bytes, live=live_bytes;peak_bytes=live;
  aether::hover_probe::reset();auto start=std::chrono::steady_clock::now();
  f.resolve(phase==0);
  auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
  auto c=allocation_calls-calls,b=allocated_bytes-bytes,p=peak_bytes-live;
  auto delta=static_cast<long long>(live_bytes)-static_cast<long long>(live);
  auto probe=aether::hover_probe::snapshot();
  std::cout<<phase<<','<<n<<','<<AETHER_HOVER_PROBE<<','<<c<<','<<b<<','<<delta<<','<<p<<','<<elapsed<<','<<probe.compute_original<<'\n';
 }
}
