// External ABI consumer with allocation-failure interposition. No private API.
#include <aether/aether_b0_source.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include <fstream>
#include <iterator>
static long fail_at=-1,allocations=0;static bool fired=false;static long live=0;
void* operator new(std::size_t n){if(fail_at>=0&&allocations++==fail_at){fired=true;throw std::bad_alloc();}if(void*p=std::malloc(n?n:1)){++live;return p;}throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{if(p)--live;std::free(p);}void operator delete[](void*p)noexcept{if(p)--live;std::free(p);}void operator delete(void*p,std::size_t)noexcept{if(p)--live;std::free(p);}void operator delete[](void*p,std::size_t)noexcept{if(p)--live;std::free(p);}
static bool& fault_delivered=fired;
#define AETHER_TEST_ALIGNED_REJECT() (fail_at>=0&&allocations++==fail_at)
#define AETHER_TEST_ALIGNED_ALLOCATED() (++live)
#define AETHER_TEST_ALIGNED_FREED(p) ((p)?--live:live)
#include "fault_inject_aligned.h"
static std::vector<unsigned char> read(const char*p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static aether_b0_source_diagnostic_v1 diagnostic(){aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;return d;}
static std::vector<unsigned char> pixels(aether_b0_source_t s){auto d=diagnostic();assert(aether_b0_source_render(s,&d)==AETHER_OK);aether_b0_source_frame_info_v1 i{};i.struct_size=sizeof i;i.version=1;assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_OK&&i.sequence==1);std::vector<unsigned char> out(i.byte_count);assert(aether_b0_source_copy_frame(s,&i,out.data(),out.size())==AETHER_OK);return out;}
int main(int argc,char**argv){assert(argc==4);auto html=read(argv[1]),r=read(argv[2]),b=read(argv[3]);aether_b0_source_options_v1 o{};o.struct_size=sizeof o;o.version=1;o.regular_font=r.data();o.regular_font_size=r.size();o.bold_font=b.data();o.bold_font_size=b.size();
 // Exhaust every allocation reached in create; the separate private runtime
 // test additionally checks publication identities, counters and rollback.
 std::vector<unsigned char> reference;long total=0;{aether_b0_source_t s=nullptr;auto d=diagnostic();fail_at=0x7fffffff;allocations=0;assert(aether_b0_source_create(640,400,(char*)html.data(),html.size(),&o,&s,&d)==AETHER_OK);total=allocations;fail_at=-1;reference=pixels(s);assert(aether_b0_source_destroy(s)==AETHER_OK);}
 {aether_b0_source_t warm=nullptr;auto d=diagnostic();fail_at=0x7fffffff;allocations=0;assert(aether_b0_source_create(640,400,(char*)html.data(),html.size(),&o,&warm,&d)==AETHER_OK);total=allocations;fail_at=-1;assert(aether_b0_source_destroy(warm)==AETHER_OK);}
 const long baseline_live=live;long tested=0,recovered=0;for(long site=0;site<total;++site){
  aether_b0_source_t s=nullptr;auto d=diagnostic();allocations=0;fired=false;fail_at=site;int status=aether_b0_source_create(640,400,(char*)html.data(),html.size(),&o,&s,&d);fail_at=-1;
  if(fired&&status!=AETHER_OK){if(status!=AETHER_ERROR_OUT_OF_MEMORY||s)std::fprintf(stderr,"create fault site=%ld status=%d stage=%u reason=%u handle=%p\n",site,status,d.stage,d.reason,(void*)s);assert(status==AETHER_ERROR_OUT_OF_MEMORY&&!s);assert(d.status==status);++tested;}
  else{assert(status==AETHER_OK&&s);if(fired){assert(pixels(s)==reference);++recovered;}assert(aether_b0_source_destroy(s)==AETHER_OK);}
  if(live!=baseline_live)std::fprintf(stderr,"live leak site=%ld live=%ld expected=%ld\n",site,live,baseline_live);assert(live==baseline_live);
 }
 aether_b0_source_t s=nullptr;auto d=diagnostic();assert(aether_b0_source_create(640,400,(char*)html.data(),html.size(),&o,&s,&d)==AETHER_OK);
 // Exhaust every allocation reached in render; each failed first publication
 // leaves NO_FRAME and a later retry succeeds with sequence 1.
 long render_sites=0;for(long site=0;;site++){
  allocations=0;fired=false;fail_at=site;int status=aether_b0_source_render(s,&d);fail_at=-1;
  if(!fired){assert(status==AETHER_OK);break;}
  assert(status==AETHER_ERROR_OUT_OF_MEMORY);aether_b0_source_frame_info_v1 i{};i.struct_size=sizeof i;i.version=1;assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_ERROR_NO_FRAME);++render_sites;
 }
 aether_b0_source_frame_info_v1 i{};i.struct_size=sizeof i;i.version=1;assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_OK&&i.sequence==1);assert(aether_b0_source_render(s,&d)==AETHER_OK);assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_OK&&i.sequence==1);aether_b0_source_frame_info_v1 before=i;unsigned char guard=0xA5;allocations=0;fired=false;fail_at=0;const auto copy_status=aether_b0_source_copy_frame(s,&i,&guard,1);fail_at=-1;assert(fired&&copy_status==AETHER_ERROR_OUT_OF_MEMORY&&guard==0xA5&&!std::memcmp(&before,&i,sizeof i));assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_OK&&i.sequence==1);assert(aether_b0_source_destroy(s)==AETHER_OK);assert(live==baseline_live);
 std::printf("B0 OOM PASS create_oom=%ld recovered_identical=%ld attempted_sites=%ld (exhaustive C++ new), render_sites=%ld (exhaustive)\n",tested,recovered,total,render_sites);
}
