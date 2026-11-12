#include "h5_profile.h"
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include <cassert>
#include <iostream>
static long fail_after=-1;
void* operator new(size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
int main(){style_property_registry_init();size_t checks=0;
 auto check=[&](const std::string& value,bool expected){bool actual=h4::value("gap",value);if(actual!=expected){std::cerr<<value<<" gate="<<actual<<" expected="<<expected<<"\n";std::abort();}++checks;};
 for(auto v:{"calc(10px + 2%)","calc(10px + 2%) calc(2px + 1%)","CALC(10px + 2%)","calc(10px /*x*/ + 2%)",R"(c\61 lc(10px + 2%))","calc((20px + 4%) / 2)","calc(5px * 2 + 2%)","calc(-10px + 2%)","calc(0px + 0%)"})check(v,true);
 for(auto v:{"min(10px,2%)","max(10px,2%)","clamp(1px,2%,10px)","calc(min(10px,20px) + 2%)","calc(2px * min(1,2))","calc(10em + 2%)","calc(10vw - 10vw + 2px)","calc(1px / 0)","calc(1px + 2px) bad","calc(1px+2%)","calc(1e999px + 2%)","-1px","calc(10px + 2%) -1px","calc(10px + 2%) 1px 2px"})check(v,false);
 check(R"(calc(1\65 3px + 2%))",false);
 check(R"(calc(10p\78  + 2%))",true);
 check(R"(calc(10p\78 + 2%))",false);
 auto a=h5::analyze(R"(div{g\61 p:calc(10px + 2%)!important;row-gap:calc(2px + 1%)})",true);assert(a.valid&&a.functions.size()==2);++checks;
 assert(!h4::value("row-gap","calc(1px + 2%) 1px"));++checks;
 size_t oom=0;bool done=false;
 const std::string value=R"(c\61 lc(10p\78  + 2%) calc(3px + 1%))";
 for(long n=0;n<3000;++n){fail_after=n;try{bool ok=h4::value("gap",value);fail_after=-1;assert(ok);done=true;break;}catch(const std::bad_alloc&){fail_after=-1;++oom;}}
 assert(done&&oom>0);
 std::string retained="unchanged";assert(!gap_length_source("calc(1px + min(2px,3px))",retained)&&retained=="unchanged");
 assert(!h4::gap_length(std::string(17000,' ')+"calc(1px + 2%)"));checks+=2;
 std::cout<<"H9 gate OOM="<<oom<<"\n";
 std::cout<<"H9 gate checks="<<checks<<"\n";
}
