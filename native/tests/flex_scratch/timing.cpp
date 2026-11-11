// Uninstrumented timing: no global allocator replacements.
#include "flex_resolver.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <string>
aether_flex_resolve_result_s baseline_aether_resolve_flexible_lengths(const aether_flex_resolve_input_s&);
aether_flex_resolve_result_s model_aether_resolve_flexible_lengths(const aether_flex_resolve_input_s&);
using Fn=aether_flex_resolve_result_s(*)(const aether_flex_resolve_input_s&);
static bool eq(float a,float b){return std::bit_cast<uint32_t>(a)==std::bit_cast<uint32_t>(b);}
static void check(const aether_flex_resolve_input_s& in){
 auto a=baseline_aether_resolve_flexible_lengths(in),b=model_aether_resolve_flexible_lengths(in);
 if(a.items.size()!=b.items.size()||a.lines.size()!=b.lines.size()) std::abort();
 for(size_t i=0;i<a.items.size();++i){ auto x=a.items[i],y=b.items[i]; if(!eq(x.flex_base_size,y.flex_base_size)||!eq(x.hypothetical_main_size,y.hypothetical_main_size)||!eq(x.target_main_size,y.target_main_size)||x.frozen!=y.frozen||!eq(x.inner_flex_base_size,y.inner_flex_base_size)) std::abort(); }
 for(size_t i=0;i<a.lines.size();++i) if(a.lines[i].item_indices!=b.lines[i].item_indices||!eq(a.lines[i].hypothetical_outer_main_size,b.lines[i].hypothetical_outer_main_size))std::abort();
}
static volatile float sink;
static double bench(Fn f,const aether_flex_resolve_input_s& in,int reps){
 auto start=std::chrono::steady_clock::now();
 for(int i=0;i<reps;++i){ auto r=f(in); sink=r.items.empty()?0:r.items.back().target_main_size; }
 return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/reps;
}
static void run(const char* name,size_t count,float width,bool wrap,float grow){
 std::vector<aether_flex_item_input_s> items(count);
 for(auto& i:items){i.basis={AETHER_FLEX_BASIS_LENGTH,NAN,0,30,true};i.min_main_size=0;i.flex_grow=grow;}
 aether_flex_resolve_input_s in{&items,width,2,wrap};check(in);
 Fn funcs[]={baseline_aether_resolve_flexible_lengths,model_aether_resolve_flexible_lengths};
 std::vector<double> a,b;const int reps=count<32?100000:600;bench(funcs[0],in,reps/5);bench(funcs[1],in,reps/5);
 for(int r=0;r<11;++r){double x,y;if(r%2){y=bench(funcs[1],in,reps);x=bench(funcs[0],in,reps);}else{x=bench(funcs[0],in,reps);y=bench(funcs[1],in,reps);}a.push_back(x);b.push_back(y);std::printf("raw,%s,%d,%s,%d,%.9f,%.9f\n",name,r,r%2?"BA":"AB",reps,x,y);}
 std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
 std::printf("%s n=%zu lines=%zu standard_allocator median_us=%.3f/%.3f ratio=%.3f\n",name,count,funcs[0](in).lines.size(),a[5],b[5],b[5]/a[5]);
}
int main(){
 run("wrap-active",4096,100,true,1);run("wrap-frozen",4096,100,true,0);run("nowrap-active",4096,150000,false,1);run("small-active",8,500,false,1);run("small-frozen",8,500,false,0);
}
