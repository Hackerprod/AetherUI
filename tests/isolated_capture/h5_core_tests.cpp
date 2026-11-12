#include "../../native/src/style_runtime/style_runtime_types.h"
#include "../../native/src/style_runtime/h5_parser_profile.h"
#include <cassert>
#include <iostream>
#include <new>
#include <cstdlib>
static long fail_after=-1;
void* operator new(std::size_t n) {if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after; if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
int main(){
 size_t checks=0;std::vector<compiled_selector_s> out;
 auto selector=[&](const std::string& text,bool wanted){auto before=out.size();bool ok=selector_parse_list_strict_profile(text,out);assert(ok==wanted);if(!ok)assert(out.size()==before);++checks;};
 for(auto s:{"div:nth-child(2n)","div:nth-child(3n+1)","div:nth-child(4n)","div:nth-child(-n+3)","div:nth-child(0)","div:nth-child(-2147483648n+2147483647)","div:\\6e th-child(2n)",".a:is(.b,:nth-child(2n))"})selector(s,true);
 for(auto s:{"div:nth-child(2147483648n)","div:nth-child(2 n)","div:nth-child(2n) garbage!","div, :bogus",":is(.a,:bogus)",":where(:bogus)","div,",",div",":nth-child(2n of .a)",":nth-child(2n"})selector(s,false);
 std::string nested=".a";for(int i=0;i<33;++i)nested=":is("+nested+")";selector(nested,false);
 selector(std::string(".a\0.b",5),false);
 auto media=[&](const std::string& text,bool wanted){auto e=media_condition_profile(text);assert((e.full_consumption&&e.profile_supported&&!e.saw_unknown_or_discarded)==wanted);if(wanted)assert(e.consumed_bytes==text.size());++checks;};
 for(auto s:{"screen","all","print","(width:700px)","(min-width:0px)","(max-height:12.5px)","screen and (max-width:700px)","print and (height:0px)","SCREEN /**/ and (width:700px)"})media(s,true);
 for(auto s:{"print and (unknown:1)","all, (unknown:1)","not print","only screen","screen and (width:700px) and (height:2px)","(width:1em)","(width:-1px)","(width:calc(1px))","(width:0)","(width:1e999px)","(width:700px","screen and","screen or print","speech","(width:1px) and (height:1px)","(pointer:fine)","screen garbage","screen,","screen /* unfinished"})media(s,false);
 // Allocation exceptions cannot publish a partially parsed selector destination.
 bool completed=false;size_t oom=0;for(long n=0;n<1000;++n){fail_after=-1;selector_parse_list_strict_profile(".sentinel",out);auto* old=out.data();fail_after=n;try{bool ok=selector_parse_list_strict_profile("div:nth-child(2n),.b:is(.c,.d)",out);fail_after=-1;assert(ok);completed=true;break;}catch(const std::bad_alloc&){fail_after=-1;assert(out.data()==old&&out.size()==1);++oom;}}assert(completed);
 completed=false;size_t media_oom=0;for(long n=0;n<1000;++n){fail_after=n;try{auto e=media_condition_profile("screen and (min-width:700px)");fail_after=-1;assert(e.profile_supported);completed=true;break;}catch(const std::bad_alloc&){fail_after=-1;++media_oom;}}assert(completed);
 std::cout<<"H5 core checks="<<checks<<" selector OOM="<<oom<<" media OOM="<<media_oom<<"\n";
}
