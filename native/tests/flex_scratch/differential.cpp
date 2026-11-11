#include "common.h"
#include <cstdio>
#include <random>
#include <thread>
#include <limits>
aether_flex_resolve_result_s baseline_aether_resolve_flexible_lengths(const aether_flex_resolve_input_s&);
aether_flex_resolve_result_s model_aether_resolve_flexible_lengths(const aether_flex_resolve_input_s&);
static void check(const aether_flex_resolve_input_s& in) {
 const auto input_before=in;
 const auto items_before=in.items?*in.items:std::vector<aether_flex_item_input_s>{};
 aether_flex_resolve_result_s a,b; bool a_overflow=false,b_overflow=false;
 try {a=baseline_aether_resolve_flexible_lengths(in);} catch(const std::overflow_error&) {a_overflow=true;}
 check_input(in,input_before,items_before);
 try {b=model_aether_resolve_flexible_lengths(in);} catch(const std::overflow_error&) {b_overflow=true;}
 check_input(in,input_before,items_before);
 if(a_overflow!=b_overflow) std::abort();
 if(!a_overflow)check_result(a,b);
}
static void random_cases(unsigned seed,int count) {
 std::mt19937 rng(seed);
 for(int k=0;k<count;++k){std::vector<aether_flex_item_input_s> v(rng()%101);for(auto& i:v){float base=float(rng()%300);i.basis={int32_t(rng()%3),float(rng()%300),float(rng()%300),base,bool(rng()%2)};i.flex_grow=float(rng()%9)/4;i.flex_shrink=float(rng()%9)/4;i.min_main_size=rng()%3?float(rng()%60):NAN;i.max_main_size=(rng()%3)?NAN:float(rng()%250);i.main_decoration_size=float(rng()%50);i.outer_main_size=float(int(rng()%61)-30);i.automatic_minimum={bool(rng()%2),float(rng()%90),rng()%2?NAN:float(rng()%120),rng()%2?NAN:float(rng()%120),rng()%2?NAN:float(rng()%150)};}check({&v,k%13?float(rng()%1200):NAN,float(rng()%20),bool(rng()%2)});}
}
int main(){
 std::vector<aether_flex_item_input_s> minimal;
 check({&minimal,100,0,true});
 minimal.resize(1);minimal[0].basis={AETHER_FLEX_BASIS_LENGTH,NAN,0,30,true};minimal[0].min_main_size=0;
 check({&minimal,100,0,true});minimal[0].flex_grow=1;check({&minimal,100,0,true});check({&minimal,NAN,0,true});
 random_cases(0xA37E,20000);
 puts("bitwise differential/input invariance PASS 20000 deterministic cases");
 // A frozen line on both sides of active lines, decreasing and increasing sizes.
 for(int mode=0;mode<4;++mode){
 std::vector<aether_flex_item_input_s> v;
 for(float size:{90.f,30.f,30.f,30.f,18.f,18.f,18.f,18.f,18.f,15.f,15.f,15.f,15.f,15.f,15.f,90.f}){aether_flex_item_input_s i;i.basis={AETHER_FLEX_BASIS_LENGTH,NAN,0,size,true};i.min_main_size=0;i.flex_grow=(size==90 || mode==1)?0:1;i.flex_shrink=mode==1?0:1;if(mode==2)i.max_main_size=size+1;if(mode==3)i.outer_main_size=-1;v.push_back(i);}check({&v,100,0,true});}
 size_t checks=0,throws=0;
 float vals[]={0.f,-0.f,1.f,-1.f,1e-40f,-1e-40f,1e-5f,-1e-5f,1e30f,-1e30f,std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),NAN,INFINITY,-INFINITY};
 auto run=[&](const aether_flex_resolve_input_s& in){
  int a=0,b=0;try{baseline_aether_resolve_flexible_lengths(in);}catch(const std::overflow_error&){a=1;}try{model_aether_resolve_flexible_lengths(in);}catch(const std::overflow_error&){b=1;}if(a!=b)abort();check(in);if(a)++throws;++checks;
 };
 run({nullptr,100,0,true});
 for(bool wrap:{false,true}) for(float v:vals)for(int field=0;field<15;++field)for(int target=0;target<3;++target){
  std::vector<aether_flex_item_input_s> items(21);
  for(size_t j=0;j<items.size();++j){auto&i=items[j];i.basis={AETHER_FLEX_BASIS_LENGTH,NAN,30,30,true};i.min_main_size=0;i.flex_grow=1;i.max_main_size=j%3?NAN:40;}
  auto&i=items[target*7+1];float* fields[]={&i.basis.specified_main_size,&i.basis.content_main_size,&i.basis.resolved_length,&i.min_main_size,&i.automatic_minimum.content_size_suggestion,&i.automatic_minimum.specified_size_suggestion,&i.automatic_minimum.transferred_size_suggestion,&i.automatic_minimum.max_main_size,&i.max_main_size,&i.outer_main_size,&i.flex_grow,&i.flex_shrink,&i.main_decoration_size};
  aether_flex_resolve_input_s in{&items,100,0,wrap};if(field<13)*fields[field]=v;else if(field==13)in.available_main_size=v;else in.gap=v;
  run(in);
 }
 printf("extreme differential PASS cases=%zu overflow_pairs=%zu\n",checks,throws);

 // Independent invocations from four threads share no scratch. No allocator hooks.
 std::thread workers[4];for(unsigned k=0;k<4;++k)workers[k]=std::thread([k]{random_cases(0xB100+k,1000);});
 for(auto& worker:workers)worker.join();
 puts("concurrent invocation differential PASS 4 x 1000 cases");
}
