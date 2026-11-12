#include "../../native/src/style_runtime/style_runtime_types.h"
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include "h5_profile.h"
#include <cassert>
#include <cstdlib>
#include <new>
#include <iostream>
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
int main(){style_property_registry_init();size_t checks=0;
 auto check=[&](std::string css,bool ok){auto a=h5::analyze(css,true);if(a.valid!=ok){std::cerr<<"Unexpected gate "<<ok<<": "<<css<<"\n";std::abort();}++checks;};
 const std::string k="@keyframes a{from{gap:10px}to{gap:50px}}";
 check(k+"div{animation:a 10s linear -5s paused}",true);
 check("@media print{"+k+"div{animation:a 10s linear -5s paused}}",true);
 check("@supports not (display:flex){"+k+"}",true);
 check("@keyframes MixedCase{0%{order:-2}100%{order:2}}div{animation:MixedCase 10s linear -5s paused}",true);
 for(auto body:{"from{gap:10px}","from{gap:10px!important}to{gap:50px}","from{gap:10px}50%{gap:20px}to{gap:50px}","from{width:10px}to{width:50px}","from{gap:10%}to{gap:50px}","from{gap:var(--x)}to{gap:50px}","from{flex-basis:auto}to{flex-basis:50px}"})check("@keyframes a{"+std::string(body)+"}",false);
 for(auto value:{"a 10s linear -5s running","a 10s ease -5s paused","a 0s linear paused","a 10s linear paused,a 10s linear paused","a 10s linear paused garbage","a 10s linear paused infinite","a 10s linear paused alternate","a 10s linear paused forwards"})check(k+"div{animation:"+value+"}",false);
 check(k+"div{animation:none}",true);
 check(k+"div{animation-name:a;animation-duration:10s;animation-timing-function:linear;animation-play-state:paused}",true);
 check("@supports not (display:flex){@keyframes a{from{width:1px}to{width:2px}}}",false);
 // No state published by analysis on any allocation failure; retry exact same source.
 const std::string css="@media screen{@supports (display:flex){"+k+"div{animation:a 10s linear -5s paused}}}";
 size_t oom=0;bool done=false;
 for(long n=0;n<2000;++n){fail_after=n;try{auto a=h5::analyze(css,true);fail_after=-1;assert(a.valid&&a.at_rules.size()==3);done=true;break;}catch(const std::bad_alloc&){fail_after=-1;++oom;}}
 assert(done);assert(h5::analyze(css,true).valid);
 std::cout<<"H7 gate checks="<<checks<<" OOM="<<oom<<"\n";
}
