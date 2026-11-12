#ifdef NDEBUG
#error Assertions required
#endif
#include "r8_reference.h"
#include <cassert>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <random>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
p::Radii radii(float r){return {r,r,r,r,r,r,r,r};}
p::PreparedFrame prepare(std::vector<p::Command> c){auto f=p::prepare({std::move(c)});assert(std::holds_alternative<p::PreparedFrame>(f));return std::move(std::get<p::PreparedFrame>(f));}
Frame get(FrameStore& s){std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
struct Cancel:p::Cancellation{int count{},at;explicit Cancel(int n):at(n){}bool cancelled() noexcept override{return ++count==at;}};
int main(){
 FrameStore old_store,new_store;assert(old_store.resize({32,32,1})==FrameStatus::ok);assert(new_store.resize({32,32,1})==FrameStatus::ok);
 std::mt19937 rng(0xA379);std::uint64_t tested=0,samples=0,bound=0;
 // Corpus fixed before execution: 1500 generated + 56 exact-band contacts.
 for(int i=0;i<1556;++i){
  float w=float(1+rng()%96)/4,h=float(1+rng()%96)/4;
  float x=float(int(rng()%96)-32)/4,y=float(int(rng()%96)-32)/4;
  float rad=(float(rng()%17)/32)*std::min(w,h);
  p::Rect clip{float(rng()%32)/4,float(rng()%32)/4,float(1+rng()%100)/4,float(1+rng()%100)/4};
  if(i>=1500){w=16;h=16;x=.25f;y=.5f;const float rs[]{0,.001f,.25f,.5f,1,3,8};rad=rs[(i-1500)/8];int k=(i-1500)%8;
   clip={x+rad+(k%2?0:-.125f),y+rad+(k/2%2?0:-.125f),16-2*rad+(k/4?.25f:0),16-2*rad+(k/4?.25f:0)};
  }
  auto f=prepare({p::Fill{{0,0,32,32},{},{.1f,.2f,.3f,1}},p::PushClip{clip,{}},p::Fill{{x,y,w,h},radii(rad),{.8f,.2f,.6f,.65f}},p::PopClip{}});
  auto a=r8_reference::render(old_store,f),b=s::render(new_store,f);
  assert(a.status==s::Status::ok&&b.status==s::Status::ok);
  assert(get(old_store).pixels==get(new_store).pixels);assert(b.sampled_pixels<=b.corner_pixel_bound);
  ++tested;samples+=b.sampled_pixels;bound+=b.corner_pixel_bound;
 }
 assert(new_store.resize({500,500,1})==FrameStatus::ok);assert(old_store.resize({500,500,1})==FrameStatus::ok);
 auto big=prepare({p::Fill{{0,0,500,500},radii(3),{1,0,0,1}}});
 auto b=s::render(new_store,big);assert(b.status==s::Status::ok&&b.sampled_pixels<=b.corner_pixel_bound);
 assert(r8_reference::render(old_store,big).status==s::Status::limit_exceeded);
 auto saved=get(new_store);
 // Conservative budget rejects many repeated fills, including clear/classification.
 std::vector<p::Command> many(100,p::Fill{{0,0,500,500},radii(3),{1,0,0,1}});
 assert(s::render(new_store,prepare(many)).status==s::Status::limit_exceeded);
 assert(get(new_store).pixels==saved.pixels&&get(new_store).sequence==saved.sequence);
 // New exact boundary budget: clear 250000 + each fill (500000+9216).
 many.resize(97);assert(s::render(new_store,prepare(many)).status==s::Status::ok);
 many.resize(98,p::Fill{{0,0,500,500},radii(3),{1,0,0,1}});
 assert(s::render(new_store,prepare(many)).status==s::Status::limit_exceeded);
 Cancel count(100000);assert(s::render(new_store,big,&count).status==s::Status::ok);saved=get(new_store);
 for(int n:{1,2,3,4,count.count/2,count.count-1,count.count}){Cancel c(n);assert(s::render(new_store,big,&c).status==s::Status::cancelled);assert(get(new_store).pixels==saved.pixels&&get(new_store).sequence==saved.sequence);}
 assert(new_store.resize({320,200,1})==FrameStatus::ok);assert(old_store.resize({320,200,1})==FrameStatus::ok);
 auto bench=prepare({p::Fill{{.25f,.5f,319,199},radii(3),{.8f,.2f,.6f,.65f}}});
 std::vector<double> old_times,new_times;std::uint64_t bench_samples=0;
 for(int i=0;i<23;++i){
  auto run=[&](bool old){auto start=std::chrono::steady_clock::now();auto r=old?r8_reference::render(old_store,bench):s::render(new_store,bench);assert(r.status==s::Status::ok);if(!old)bench_samples=r.sampled_pixels;return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
  double a,c;if(i%2){c=run(false);a=run(true);}else{a=run(true);c=run(false);}
  assert(get(old_store).pixels==get(new_store).pixels);
  if(i>=2){old_times.push_back(a);new_times.push_back(c);}
 }
 std::sort(old_times.begin(),old_times.end());std::sort(new_times.begin(),new_times.end());
 std::cout<<"status=PASS\ncorpus_cases="<<tested<<"\nsampled_pixels="<<samples<<"\ncorner_bound="<<bound
 <<"\nbenchmark_scope=direct 320x200 single fill, same process and build, alternating order\nwarm_pairs=21"
 <<"\nr8_median_ms="<<old_times[10]<<"\nr9_median_ms="<<new_times[10]<<"\nr8_p95_ms="<<old_times[19]<<"\nr9_p95_ms="<<new_times[19]
 <<"\nr8_membership_evaluations="<<std::uint64_t(320)*200*256<<"\nr9_membership_evaluations="<<bench_samples*256
 <<"\nlarge_500x500_budget="<<b.pixel_budget<<"\nlarge_sampled_pixels="<<b.sampled_pixels<<"\nhtml=NOT_RUN\nchromium=NOT_RUN\n";
}
