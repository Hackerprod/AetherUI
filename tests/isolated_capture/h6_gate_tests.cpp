#include "h5_profile.h"
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include <cassert>
#include <iostream>
#include <new>
#include <cstdlib>
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
int main(){
 style_property_registry_init();
 size_t checks=0;
 auto accept=[&](const std::string& s,size_t n){auto a=h5::analyze(s,true);assert(a.valid&&a.at_rules.size()==n);++checks;};
 accept("@supports (display:flex){.a{width:1px}}",1);
 accept("@supports not (display:flex){@media print{.a{width:1px}}}",2);
 accept("@media screen{@supports (display:flex){.a{width:1px}}}",2);
 std::string nested=".a{width:1px}";for(int n=0;n<32;++n)nested=(n%2?"@media screen{":"@supports (display:flex){")+nested+"}";accept(nested,32);
 for(auto s:{"@supports (display:bogus){.a{width:1px}}","@supports (display:flex) or (unknown:x){.a{width:1px}}","@supports not (future stuff){.a{width:1px}}","@supports (display:revert){.a{width:1px}}","@supports (display:flex;){.a{width:1px}}"}){assert(!h5::analyze(s,true).valid);++checks;}
 assert(!h5::analyze("@supports (display:flex){"+nested+"}",true).valid);++checks;
 std::string many;for(int i=0;i<14000;++i)many+="@supports (display:flex){.a{width:1px}}";assert(!h5::analyze(many,true).valid);++checks;
 bool finished=false;size_t oom=0;for(long n=0;n<1000;++n){fail_after=n;try{auto a=h5::analyze("@supports (display:flex){@media screen{.a:nth-child(2n){padding:calc(2px + 3%);background-color:rgba(0,0,0,.5)}}}",true);fail_after=-1;assert(a.valid&&a.functions.size()==3&&a.at_rules.size()==2);finished=true;break;}catch(const std::bad_alloc&){fail_after=-1;++oom;}}assert(finished);
 std::cout<<"H6 shared gate checks="<<checks<<" OOM="<<oom<<"\n";
}
