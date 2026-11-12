#include "h5_profile.h"
#include <cassert>
#include <iostream>
#include <new>
#include <cstdlib>
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
int main(){
 size_t checks=0;
 auto accept=[&](const std::string& s,bool sheet,size_t f,size_t at){auto a=h5::analyze(s,sheet);assert(a.valid&&a.functions.size()==f&&a.at_rules.size()==at);++checks;};
 accept(".a:nth-child(2n){padding:max(1px,2%);background-color:rgba(0,0,0,.5)}",true,3,0);
 accept("@media screen{.a:nth-child(2n){padding:calc(1px + 2%);box-shadow:0 0 rgba(0,0,0,.07)}}",true,3,1);
 accept("@media print{.a{padding:clamp(1px,2%,3px)}}",true,1,1);
 accept("padding:max(1px,2%);background-color:rgba(0,0,0,.5)!important",false,2,0);
 accept(".a:\\6e th-child(2n){padding:m\\61 x(1px,2%)}",true,2,0);
 std::string nested=".a{padding:max(1px,2px)}";for(int n=0;n<32;++n)nested="@media all{"+nested+"}";accept(nested,true,1,32);
 assert(!h5::analyze("@media all{"+nested+"}",true).valid);++checks;
 for(auto s:{".a{padding:max(1px,2px)","@media print and (bad:1){.a{color:red}}",".a,:bad{padding:1px}",".a:is(.b,:bad){padding:1px}",".a{padding:1px;broken}","@media print{@keyframes x{from{color:red}}}"}){assert(!h5::analyze(s,true).valid);++checks;}
 bool finished=false;size_t oom=0;for(long n=0;n<1000;++n){fail_after=n;try{auto a=h5::analyze("@media screen{.a:nth-child(2n){padding:calc(2px + 3%);background-color:rgba(0,0,0,.5)}}",true);fail_after=-1;assert(a.valid&&a.functions.size()==3&&a.at_rules.size()==1);finished=true;break;}catch(const std::bad_alloc&){fail_after=-1;++oom;}}assert(finished);
 std::cout<<"H5 shared gate checks="<<checks<<" OOM="<<oom<<"\n";
}
