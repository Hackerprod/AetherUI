#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/rendering/software/software_renderer.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
static long budget=-1;
void* operator new(std::size_t n){if(budget==0)throw std::bad_alloc();if(budget>0)--budget;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
using namespace aether::rendering;
int main(){
 p::LinearGradientFill g;g.rect=g.gradient_rect={0,0,16,12};g.direction_x=1;g.stops={{0,{1,0,0,1}},{1,{0,0,1,1}}};
 p::Snapshot ss{{g,g}};bool done=false;long failures=0;
 for(long n=0;n<100;++n){budget=n;auto r=p::prepare(ss);budget=-1;if(std::holds_alternative<p::PreparedFrame>(r)){done=true;break;}assert(std::get<p::Error>(r).code==p::ErrorCode::OUT_OF_MEMORY);++failures;}
 assert(done&&failures>=3);auto r=p::prepare(ss);auto& prepared=std::get<p::PreparedFrame>(r);
 FrameStore store;assert(store.resize({16,12,1})==FrameStatus::ok);assert(s::render(store,prepared).status==s::Status::ok);
 std::optional<Frame> old;assert(store.copy_confirmed(old)==FrameStatus::ok);done=false;long render_failures=0;
 for(long n=0;n<100;++n){budget=n;auto result=s::render(store,prepared);budget=-1;if(result.status==s::Status::ok){done=true;break;}assert(result.status==s::Status::allocation_failed);std::optional<Frame> now;assert(store.copy_confirmed(now)==FrameStatus::ok);assert(now->sequence==old->sequence&&now->pixels==old->pixels);++render_failures;}
 assert(done&&render_failures>=2);std::cout<<"G1 injected OOM prepare_sites="<<failures<<" render_sites="<<render_failures<<" atomic=PASS\n";
}
