// Minimal allocation-failure and nested-call fixture. Never run broad fuzzing here.
#include "common.h"
#include <cstdio>
#include <new>
#include <cstddef>
#include <limits>
static size_t ordinal=0,fail_at=0,live_bytes=0;static bool tracking=false;
static void (*nested_call)()=nullptr;
static size_t nested_countdown=0;
struct alignas(std::max_align_t) AllocationHeader {size_t bytes;};
void* operator new(size_t n){
 if(tracking && ++ordinal==fail_at)throw std::bad_alloc();
 if(nested_call && --nested_countdown==0){auto callback=nested_call;nested_call=nullptr;const bool was_tracking=tracking;tracking=false;callback();tracking=was_tracking;}
 if(n>std::numeric_limits<size_t>::max()-sizeof(AllocationHeader))throw std::bad_alloc();
 auto p=static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader)+(n?n:1)));
 if(!p)throw std::bad_alloc();p->bytes=n;live_bytes+=n;return p+1;
}
void operator delete(void*p) noexcept{if(p){auto h=static_cast<AllocationHeader*>(p)-1;live_bytes-=h->bytes;std::free(h);}}
void operator delete(void*p,size_t) noexcept{::operator delete(p);}
aether_flex_resolve_result_s model_aether_resolve_flexible_lengths(const aether_flex_resolve_input_s&);
static const aether_flex_resolve_input_s* nested_input;
static const aether_flex_resolve_result_s* nested_expected;
static void run_nested(){check_result(*nested_expected,model_aether_resolve_flexible_lengths(*nested_input));}
int main(){
 for(bool frozen:{false,true}){
 std::vector<aether_flex_item_input_s> v;
 for(float b:{90.f,30.f,30.f,30.f,18.f,18.f,18.f,18.f,18.f,15.f,15.f,15.f,15.f,15.f,15.f,90.f}){aether_flex_item_input_s i;i.basis={AETHER_FLEX_BASIS_LENGTH,NAN,0,b,true};i.min_main_size=0;i.flex_grow=frozen?0:1;i.flex_shrink=frozen?0:1;v.push_back(i);}
 const auto before=v;const aether_flex_resolve_input_s in{&v,100,0,true};const auto before_in=in;
 ordinal=0;fail_at=0;tracking=true;auto initial=model_aether_resolve_flexible_lengths(in);tracking=false;size_t n=ordinal;
 const size_t expected[]={1,3,5,6,1};if(initial.lines.size()!=5)std::abort();for(size_t k=0;k<5;++k)if(initial.lines[k].item_indices.size()!=expected[k])std::abort();
 for(size_t f=1;f<=n;++f){const auto before_live=live_bytes;ordinal=0;fail_at=f;tracking=true;bool caught=false;
 try{auto r=model_aether_resolve_flexible_lengths(in);}catch(const std::bad_alloc&){caught=true;}
 tracking=false;if(!caught||live_bytes!=before_live)std::abort();check_input(in,before_in,before);
 {auto r=model_aether_resolve_flexible_lengths(in);check_result(initial,r);}if(live_bytes!=before_live)std::abort();}
 printf("product OOM PASS frozen=%d: %zu one-shot failures; all input/result fields and allocation balance checked\n",frozen,n);
 // Reenter at each allocation of the outer invocation, including scratch growth.
 nested_input=&in;nested_expected=&initial;
 for(size_t f=1;f<=n;++f){
  // A lightweight countdown arms the callback at a particular allocation below.
  const auto before_live=live_bytes;nested_call=run_nested;nested_countdown=f;
  {auto r=model_aether_resolve_flexible_lengths(in);check_result(initial,r);}
  if(nested_call||live_bytes!=before_live)std::abort();
 }
 puts("nested invocation recovery/input invariance PASS");
 }
}
