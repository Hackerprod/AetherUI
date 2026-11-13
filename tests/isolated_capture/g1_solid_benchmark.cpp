// Identical benchmark can compile against baseline and G1. No gradients.
#include "../../native/src/rendering/software/software_renderer.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cassert>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
int main(){
 FrameStore store;assert(store.resize({256,128,1})==FrameStatus::ok);
 p::Snapshot ss;for(int i=0;i<8;++i)ss.commands.push_back(p::Fill{{float(i*32),0,32,128},{},{.2f,.6f,.8f,.7f}});
 auto value=p::prepare(ss);assert(std::holds_alternative<p::PreparedFrame>(value));auto &f=std::get<p::PreparedFrame>(value);
 std::vector<long long> timings;
 for(int i=0;i<100;++i){auto before=std::chrono::steady_clock::now();auto r=software::render(store,f);auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-before).count();assert(r.status==software::Status::ok);if(i>=10)timings.push_back(elapsed);}
 std::sort(timings.begin(),timings.end());std::cout<<"solid_render_median_ns="<<timings[timings.size()/2]<<"\nsolid_render_p90_ns="<<timings[timings.size()*9/10]<<"\nsamples="<<timings.size()<<"\n";
}
