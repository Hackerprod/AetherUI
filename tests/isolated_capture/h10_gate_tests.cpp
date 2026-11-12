#include "h5_profile.h"
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include <cassert>
#include <iostream>
static long budget=-1;
void* operator new(size_t n){if(budget==0)throw std::bad_alloc();if(budget>0)--budget;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
int main(){style_property_registry_init();size_t checks=0;
 for(auto value:{"translateZ(0)","translateZ(-0px)","TRANSLATEZ(+0.000)",R"(translat\65 Z(0p\78 ))","translateZ(/*x*/0/*y*/)"}){assert(h4::value("transform",value));auto a=h5::analyze(std::string("div{transform:")+value+"}",true);assert(a.valid&&a.functions.size()==1);++checks;}
 for(auto value:{"translateZ(0%)","translateZ(0em)","translateZ(calc(0px))","translateZ(1e-999px)","translateZ(1e999px)","translateZ(0) translateZ(0)","translateZ(0) scale(1)","translate(0)","perspective(0)","translateZ(0,0)","translateZ(0 0)","translateZ(none)","translateZ(0) garbage",R"(translateZ\(0\))","translateZ /**/ (0)"}){assert(!h4::value("transform",value));++checks;}
 size_t failures=0;bool done=false;std::string s=R"(translat\65 Z(0p\78 ))";
 for(long n=0;n<500;++n){budget=n;try{bool ok=h4::value("transform",s);budget=-1;assert(ok);done=true;break;}catch(const std::bad_alloc&){budget=-1;++failures;}}
 assert(done&&failures>0);std::cout<<"H10 gate="<<checks<<" OOM="<<failures<<"\n";
}
