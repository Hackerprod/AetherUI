#include "../src/rendering/html_ltr/persistent_font_provider.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
using namespace aether::rendering::html_ltr;
namespace {
long fail_after=-1,allocations=0;OwnedBudget* observed=nullptr;bool track=false;size_t tracked_bytes=0,prechecks=0;
struct Block {void* base;size_t bytes;bool tracked;};
void* allocate(size_t n,size_t alignment){
 if(fail_after==0)throw std::bad_alloc();
 if(fail_after>0)--fail_after;
 ++allocations;
 if(track){assert(observed);assert(observed->total().current-OwnedBudget::bookkeeping_bytes()>=tracked_bytes+n);++prechecks;}
 alignment=std::max(alignment,alignof(Block));void* base=nullptr;const auto prefix=(sizeof(Block)+alignment-1)/alignment*alignment;
 if(posix_memalign(&base,alignment,prefix+(n?n:1)))throw std::bad_alloc();
 auto* data=static_cast<char*>(base)+prefix;auto* h=reinterpret_cast<Block*>(data)-1;*h={base,n,track};if(track)tracked_bytes+=n;return data;
}
void release(void* p){if(p){auto* h=static_cast<Block*>(p)-1;if(h->tracked)tracked_bytes-=h->bytes;std::free(h->base);}}
struct Tracking {Tracking(){track=true;}~Tracking(){track=false;}};
std::vector<uint8_t> read(const char* path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
std::string encode(uint32_t cp){if(cp<128)return std::string(1,char(cp));if(cp<2048)return {char(0xc0|(cp>>6)),char(0x80|(cp&63))};return {char(0xe0|(cp>>12)),char(0x80|((cp>>6)&63)),char(0x80|(cp&63))};}
auto create(OwnedBudget& b,const std::vector<uint8_t>& r,const std::vector<uint8_t>& bold){Tracking t;return PersistentFontProvider::create(b,r.data(),r.size(),bold.data(),bold.size());}
}
void* operator new(size_t n){return allocate(n,alignof(std::max_align_t));}void* operator new[](size_t n){return allocate(n,alignof(std::max_align_t));}
void* operator new(size_t n,std::align_val_t a){return allocate(n,size_t(a));}void* operator new[](size_t n,std::align_val_t a){return allocate(n,size_t(a));}
void operator delete(void* p)noexcept{release(p);}void operator delete[](void* p)noexcept{release(p);}void operator delete(void* p,size_t)noexcept{release(p);}void operator delete[](void* p,size_t)noexcept{release(p);}
void operator delete(void* p,std::align_val_t)noexcept{release(p);}void operator delete[](void* p,std::align_val_t)noexcept{release(p);}void operator delete(void* p,size_t,std::align_val_t)noexcept{release(p);}void operator delete[](void* p,size_t,std::align_val_t)noexcept{release(p);}
int main(int argc,char** argv){
 assert(argc==3);auto regular=read(argv[1]),bold=read(argv[2]);OwnedBudget budget;observed=&budget;const auto empty=budget.total().current;std::array<size_t,448> phase{};size_t maximum=0;
 {auto p=create(budget,regular,bold);std::shared_ptr<const OwnedFontProduct> kept;std::weak_ptr<const owned_text::Run> weak;
  for(size_t i=0;i<10000;++i){uint32_t cp=32+uint32_t(i%224);if(!repertoire_scalar(cp))cp=0x2022;auto scalar=encode(cp);std::string s;for(size_t j=0;j<256;++j)s+=scalar;
   {Tracking t;auto op=p->operation();auto q=p->product(s,16,(i/224)%2?400:700,24);assert(q->run->glyphs.size()==256);if(!i){kept=q;weak=q->run;}auto back=p->product(scalar,16,(i/224)%2?400:700,24);assert(p->work().scalar_visits==514);}
   maximum=std::max(maximum,budget.total().current);if(i>=896&&i<1344)phase[i%448]=budget.total().current;if(i>=1344)assert(phase[i%448]==budget.total().current);
  }
  std::cout<<"cycles=10000 final="<<budget.total().current<<" max="<<maximum<<" peak="<<budget.total().peak<<'\n';p.reset();assert(kept->run->source.size()==256);kept.reset();assert(weak.expired());assert(budget.total().current>empty);weak.reset();assert(budget.total().current==empty);
 }
 const long start=allocations;{auto p=create(budget,regular,bold);}const long creation_points=allocations-start;size_t faults=0;
 for(long i=0;i<creation_points;++i){fail_after=i;try{auto p=create(budget,regular,bold);assert(false);}catch(const std::bad_alloc&){++faults;}catch(const Error& e){assert(e.status==AETHER_ERROR_OUT_OF_MEMORY);++faults;}fail_after=-1;assert(budget.total().current==empty&&tracked_bytes==0);}
 long product_points=0;{auto p=create(budget,regular,bold);long n=allocations;{Tracking t;auto op=p->operation();auto q=p->product("temporary",16,400,24);}product_points=allocations-n;}
 for(long i=0;i<product_points;++i){auto p=create(budget,regular,bold);fail_after=i;try{Tracking t;auto op=p->operation();auto q=p->product("temporary",16,400,24);assert(false);}catch(const std::bad_alloc&){++faults;}catch(const Error& e){assert(e.status==AETHER_ERROR_OUT_OF_MEMORY);++faults;}fail_after=-1;{Tracking t;auto op=p->operation();auto q=p->product("temporary",16,400,24);assert(q->run->source=="temporary");}p.reset();assert(budget.total().current==empty&&tracked_bytes==0);}
 // Same shared shaping arithmetic and decoder results as the legacy provider.
 {auto p=create(budget,regular,bold);for(int weight:{400,700}){FontProvider legacy(regular,bold,2);auto op=p->operation();for(uint32_t cp=32;cp<=255;++cp)if(repertoire_scalar(cp)){
   const auto s=encode(cp);auto old=legacy.product(s,16,weight,24);std::shared_ptr<const OwnedFontProduct> owned;{Tracking t;owned=p->product(s,16,weight,24);}const auto& r=*owned->run;
   assert(std::string_view(r.source)==s&&r.glyphs.size()==old.glyphs.size()&&std::memcmp(&r.metrics,&old.metrics,sizeof(r.metrics))==0);
   for(size_t i=0;i<r.glyphs.size();++i)assert(std::memcmp(&r.glyphs[i],&old.glyphs[i],sizeof(r.glyphs[i]))==0);
   assert(std::equal(owned->prefixes.begin(),owned->prefixes.end(),old.prefixes.begin(),old.prefixes.end()));
  }}}
 assert(budget.total().current==empty&&tracked_bytes==0);
 for(uint32_t cp=1;cp<=0x10ffff;++cp)assert(repertoire_scalar(cp)==((cp>=32&&cp<=126)||(cp>=160&&cp<=255&&cp!=173)));
 std::cout<<"allocation_faults="<<faults<<" creation_points="<<creation_points<<" product_points="<<product_points<<" prechecks="<<prechecks<<" live="<<tracked_bytes<<" ledger="<<budget.total().current<<" PASS\n";
}
