#include "rendering/html_ltr/interaction_frame.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace aether::rendering::html_ltr;
static void oom(){throw std::bad_alloc{};}
template<class F> void error(int status,F f){bool caught=false;try{f();}catch(const Error& e){caught=true;assert(e.status==status);}assert(caught);}
int main(){
 const auto base=OwnedBudget::bookkeeping_bytes();
 error(AETHER_ERROR_LIMIT_EXCEEDED,[&]{OwnedBudget too_small(base-1);});
 error(AETHER_ERROR_INVALID_ARGUMENT,[]{OwnedBudget too_large(OwnedBudget::limit_bytes+1);});
 OwnedBudget b(base+32);assert(b.total().current==base);
 error(AETHER_ERROR_INVALID_ARGUMENT,[&]{b.reserve(OwnedCategory::count,1);});
 assert(b.total().current==base);
 {auto r=b.reserve(OwnedCategory::glyphs,16);assert(r.shrink(8));assert(!r.shrink(9));assert(b.total().current==base+8);auto moved=std::move(r);assert(!r.bytes());assert(moved.bytes()==8);}
 assert(b.total().current==base);
 error(AETHER_ERROR_LIMIT_EXCEEDED,[]{OwnedBudget::multiply(std::numeric_limits<std::size_t>::max(),2);});
 InteractionFrame frame;frame.prepare(b,2,2,1,1);frame.data()[0]=77;assert(b.total().current==base+16);
 bool failed=false;try{frame.prepare(b,2,2,2,2,oom);}catch(const std::bad_alloc&){failed=true;}assert(failed&&b.total().current==base+16);assert(frame.data()[0]==77&&frame.version()==1&&frame.sequence()==1);
 error(AETHER_ERROR_LIMIT_EXCEEDED,[&]{frame.prepare(b,3,2,2,2);});assert(frame.data()[0]==77&&b.total().current==base+16);
 frame.prepare(b,2,2,2,2);assert(b.total().peak==base+32&&b.total().current==base+16&&frame.version()==2);
 error(AETHER_ERROR_SUSPENDED,[&]{frame.prepare(b,0,2,3,3);});assert(frame.version()==2);
 for(auto dims:{std::pair{-1,2},std::pair{8193,1},std::pair{8192,8192}})error(AETHER_ERROR_INVALID_ARGUMENT,[&]{InteractionFrame::byte_count(dims.first,dims.second);});
 assert(InteractionFrame::byte_count(8192,2048)==64u*1024u*1024u);
 // Storage may outlive budget handle, including destruction after an allocation failure.
 OwnedBuffer<unsigned char> survivor;{OwnedBudget local;survivor.allocate(local,OwnedCategory::glyphs,128);survivor.data()[0]=42;}assert(survivor.data()[0]==42);survivor.reset();
 {OwnedBudget cycles;InteractionFrame confirmed;for(unsigned i=0;i<10000;++i){InteractionFrame candidate;candidate.prepare(cycles,8,8,i+1,i+1);confirmed.swap(candidate);}assert(cycles.usage(OwnedCategory::frame).current==256);assert(cycles.usage(OwnedCategory::frame).peak==512);}
 {OwnedBudget buffers;OwnedBuffer<unsigned> value;value.allocate(buffers,OwnedCategory::values,4);value.data()[0]=12;
  const auto charged=buffers.total().current;auto* alias=&value;value=std::move(*alias);assert(value.data()[0]==12&&buffers.total().current==charged);
  value.allocate(buffers,OwnedCategory::values,0);assert(!value.data()&&!value.capacity()&&buffers.total().current==base);}
 std::puts("owned/frame unit PASS; 10000 frame swaps only, NOT interactive cycles");
}
