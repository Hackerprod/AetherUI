#define main capture_entry_unused
#include "main.cpp"
#undef main
#include <cassert>
static long allocation_budget=-1;
void* operator new(size_t n){if(allocation_budget==0)throw std::bad_alloc();if(allocation_budget>0)--allocation_budget;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
static size_t checks=0;
static void check(bool v){if(!v)std::cerr<<"check "<<checks<<" failed\n";assert(v);++checks;}
int main(){
 style_property_registry_init();
 const std::string css="a{width:10px}@media(min-width:1px){@supports(display:flex){b{height:20px}}}@keyframes k{from{gap:10px}to{gap:20px}}";
 auto a=h5::analyze(css,true);check(a.valid);check(a.declarations.size()==4);check(a.at_rules.size()==3);
 check(css.substr(a.declarations[0].begin,a.declarations[0].end-a.declarations[0].begin)=="width:10px");
 check(a.declarations[0].depth==0&&!a.declarations[0].keyframe);
 check(a.declarations[1].depth==2&&!a.declarations[1].keyframe);
 check(a.declarations[2].keyframe&&a.declarations[3].keyframe);
 h3::authored(css,true);
 for(auto s:{"@media print{a{writing-mode:vertical-rl}}","@keyframes k{from{gap:10px;writing-mode:vertical-rl!important}to{gap:20px}}","@media print{a{min-height:1%}}"}){
  bool rejected=false;try{h3::authored(s,true);}catch(Failure){rejected=true;}check(rejected);
 }
 for(auto s:{"@media(min-width:1px){a{width:10px}","@media(min-width:1px){a{width:10px}}}","@media(min-width:1px);","@keyframes k{a{gap:10px}to{gap:20px}}"})check(!h5::analyze(s,true).valid);
 for(int which=0;which<4;++which){h5::WorkBudget b;
  if(which==0)b.bytes=css.size()-1;if(which==1)b.tokens=2;if(which==2)b.rules=2;if(which==3)b.steps=2;
  check(!h5::analyze(css,true,&b).valid);
 }
 // Aggregate limits apply across sheets, including separate style elements.
 h5::WorkBudget b;b.bytes=css.size()*2-1;check(h5::analyze(css,true,&b).valid);check(!h5::analyze(css,true,&b).valid);
 std::string deep;for(int i=0;i<33;++i)deep+="@media(min-width:1px){";deep+="a{width:10px}";deep+=std::string(33,'}');check(!h5::analyze(deep,true).valid);
 // OOM never returns a usable partial analysis. Callers can only consume spans
 // after complete success; the next attempt uses a fresh per-capture budget.
 size_t failures=0;bool complete=false;
 for(long i=0;i<10000;++i){allocation_budget=i;try{h3::authored(css,true);allocation_budget=-1;complete=true;break;}catch(const std::bad_alloc&){allocation_budget=-1;++failures;}catch(Failure){allocation_budget=-1;++failures;}}
 check(complete&&failures>0);check(h5::analyze(css,true).valid);
 std::cout<<"H15 core checks="<<checks<<" OOM="<<failures<<"\n";
}
