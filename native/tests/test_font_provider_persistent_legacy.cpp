#include "../src/rendering/html_ltr/font_provider.h"
#include "../src/rendering/html_ltr/owned_font_transport.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
using namespace aether::rendering;using namespace html_ltr;
static long fail_after=-1,allocations=0;
void* operator new(size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t alignment){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;++allocations;void* p=nullptr;if(posix_memalign(&p,size_t(alignment),n?n:1))throw std::bad_alloc();return p;}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}void operator delete(void* p,size_t,std::align_val_t)noexcept{std::free(p);}
static auto read(const char* p){std::ifstream f(p,std::ios::binary);return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});}
static std::string encode(uint32_t cp){if(cp<128)return std::string(1,char(cp));if(cp<2048)return {char(0xc0|(cp>>6)),char(0x80|(cp&63))};return {char(0xe0|(cp>>12)),char(0x80|((cp>>6)&63)),char(0x80|(cp&63))};}
int main(int argc,char** argv){
 assert(argc==3);auto r=read(argv[1]),b=read(argv[2]);OwnedBudget budget;const auto initial=budget.total().current;
 {auto rs=std::allocate_shared<owned_text::Source>(OwnedAllocator<owned_text::Source>(budget,OwnedCategory::inputs),budget.handle(),std::span<const uint8_t>(r));auto bs=std::allocate_shared<owned_text::Source>(OwnedAllocator<owned_text::Source>(budget,OwnedCategory::inputs),budget.handle(),std::span<const uint8_t>(b));
  auto regular=owned_text::legacy_source(rs),bold=owned_text::legacy_source(bs);const auto* ra=rs->bytes.data();const auto* ba=bs->bytes.data();const auto baseline=budget.total().current;
  {FontProvider p(regular,bold,FontProvider::PersistentInteractionTag{budget});const auto owned=budget.total().current;size_t maximum=0;
   for(size_t i=0;i<10000;++i){uint32_t cp=32+uint32_t(i%224);if(!repertoire_scalar(cp))cp=0x2022;auto scalar=encode(cp);std::string s;for(size_t j=0;j<256;++j)s+=scalar;
    {auto operation=p.operation();auto q=p.product(s,16,(i/224)%2?400:700,24);auto back=p.product(scalar,16,(i/224)%2?400:700,24);assert(q.glyphs.size()==256&&back.glyphs.size()==1);assert(p.usage().scalar_visits==514);assert(q.binding->source()->byte_view().data()==((i/224)%2?ra:ba));}
    assert(p.usage().outlines<=256&&p.usage().instances<=16);maximum=std::max(maximum,p.usage().payload);assert(budget.total().current==owned);
   }
   std::cout<<"operations=10000 logical_cache_max="<<maximum<<" owned_stable="<<owned<<'\n';
   {auto operation=p.operation();auto masked=p.product("\xe2\x80\xa2\xe2\x80\xa2",16,400,24);assert(masked.glyphs.size()==2&&masked.source.size()==6);}
  }
  assert(budget.total().current==baseline);
  // Fill and retain every outline slot; no referenced resource may be evicted.
  {FontProvider p(regular,bold,FontProvider::PersistentInteractionTag{budget});std::vector<std::shared_ptr<const tt::Outline>> pinned;std::string next;int next_weight=0;
   for(int weight:{400,700})for(uint32_t cp=32;cp<=255;++cp)if(repertoire_scalar(cp)&&next.empty()){
    auto s=encode(cp);if(pinned.size()==256){next=s;next_weight=weight;break;}
    auto operation=p.operation();auto q=p.product(s,16,weight,24);auto o=p.outline(q.binding,q.glyphs.front().id);bool duplicate=false;for(const auto& old:pinned)if(old==o)duplicate=true;if(!duplicate)pinned.push_back(o);
   }
   assert(pinned.size()==256&&!next.empty());const auto survivor=pinned.back();bool limited=false;
   try{auto operation=p.operation();(void)p.product(next,16,next_weight,24);}catch(const Error& e){limited=e.status==AETHER_ERROR_LIMIT_EXCEEDED;}assert(limited&&pinned.back()==survivor&&survivor->source());
   pinned.front().reset();fail_after=2;bool oom=false;
   try{auto operation=p.operation();(void)p.product(next,16,next_weight,24);}catch(const std::bad_alloc&){oom=true;}catch(const Error& e){assert(e.status==AETHER_ERROR_OUT_OF_MEMORY);oom=true;}fail_after=-1;assert(oom&&pinned.back()==survivor);
   {auto operation=p.operation();auto q=p.product(next,16,next_weight,24);assert(q.glyphs.size()==1);}assert(pinned.back()==survivor);
   std::cout<<"pinned=256 LIMIT then unpin/retry PASS\n";
  }
  long points=0;{FontProvider p(regular,bold,FontProvider::PersistentInteractionTag{budget});auto before=allocations;{auto operation=p.operation();(void)p.product("temporary",16,400,24);}points=allocations-before;}
  for(long i=0;i<points;++i){FontProvider p(regular,bold,FontProvider::PersistentInteractionTag{budget});fail_after=i;bool failed=false;
   try{auto operation=p.operation();(void)p.product("temporary",16,400,24);}catch(const std::bad_alloc&){failed=true;}catch(const Error& e){assert(e.status==AETHER_ERROR_OUT_OF_MEMORY);failed=true;}fail_after=-1;assert(failed);
   {auto operation=p.operation();auto q=p.product("temporary",16,400,24);assert(q.source=="temporary");}
  }
  assert(budget.total().current==baseline);std::cout<<"product_OOM="<<points<<" retry/no-poison PASS\n";
 }
 assert(budget.total().current==initial);std::cout<<"owned lifecycle PASS\n";
}
