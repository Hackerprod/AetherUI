#include "ledger.h"
#include "common.h"
#include <cstdio>
aether_flex_resolve_result_s ledger_baseline_aether_resolve_flexible_lengths(const aether_flex_resolve_input_s&);
aether_flex_resolve_result_s ledger_model_aether_resolve_flexible_lengths(const aether_flex_resolve_input_s&);
struct Stats {size_t allocations,peak,retained;};
static void run(const char* name,const std::vector<aether_flex_item_input_s>& items,float width,bool frozen,bool equal) {
 Stats stats[2];aether_flex_resolve_result_s outputs[2];
 for(int mode=0;mode<2;++mode){scratch_live=scratch_peak=scratch_allocs=scratch_retained=0;
 outputs[mode]=(mode?ledger_model_aether_resolve_flexible_lengths:ledger_baseline_aether_resolve_flexible_lengths)({&items,width,0,true});
 if(scratch_live)std::abort();stats[mode]={scratch_allocs,scratch_peak,scratch_retained};}
 check_result(outputs[0],outputs[1]);size_t m=0;for(auto& l:outputs[1].lines)m=std::max(m,l.item_indices.size());if(frozen)m=0;
 if(stats[1].retained>16*m || stats[1].peak>24*m || stats[1].peak>stats[0].peak+16*m)std::abort();
 if(frozen && stats[1].allocations!=0)std::abort();
 if(equal && stats[0].allocations-stats[1].allocations!=outputs[0].lines.size()-(frozen?0:1))std::abort();
 printf("%s frozen=%d N=%zu L=%zu M_active=%zu allocations=%zu/%zu peak=%zu/%zu retained=%zu/%zu live_return=0 budgets=PASS\n",name,frozen,items.size(),outputs[1].lines.size(),m,stats[0].allocations,stats[1].allocations,stats[0].peak,stats[1].peak,stats[0].retained,stats[1].retained);
}
int main(){
 for(bool frozen:{false,true}){
 std::vector<aether_flex_item_input_s> v;
 for(size_t n=1;n<=1025;++n)for(size_t k=0;k<n;++k){aether_flex_item_input_s i;i.basis={AETHER_FLEX_BASIS_LENGTH,NAN,0,k==0?8192.f-float(n-1):1.f,true};i.min_main_size=0;i.flex_grow=frozen?0:1;i.flex_shrink=frozen?0:1;v.push_back(i);}run("increasing",v,8192,frozen,false);
 v.resize(4096);for(auto& i:v){i.basis.resolved_length=30;}run("equal-lines",v,100,frozen,true);
 v.clear();for(float size:{90.f,30.f,30.f,30.f,18.f,18.f,18.f,18.f,18.f,15.f,15.f,15.f,15.f,15.f,15.f,90.f}){aether_flex_item_input_s i;i.basis={AETHER_FLEX_BASIS_LENGTH,NAN,0,size,true};i.min_main_size=0;i.flex_grow=frozen?0:1;i.flex_shrink=frozen?0:1;v.push_back(i);}run("1-3-5-6-1",v,100,frozen,false);
 }
 {std::vector<double,LedgerAllocator<double>> v;for(size_t n:{1,3,5,6,1}){if(v.size()<n)v.resize(n);printf("STL stage n=%zu size=%zu capacity=%zu retained=%zu\n",n,v.size(),v.capacity(),scratch_live);}}
 if(scratch_live)std::abort();
}
