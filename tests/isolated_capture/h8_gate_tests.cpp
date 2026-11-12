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
 auto check=[&](std::string css,bool ok){auto a=h5::analyze(css,true);if(a.valid!=ok){std::cerr<<"Unexpected H8 gate "<<ok<<": "<<css<<"\n";std::abort();}++checks;};
 for(auto property:{"width","min-width","max-width","flex-basis"})for(auto value:{"min-content","max-content","fit-content",R"(m\69 n-content)",R"(f\69 t-content)"})check("div{"+std::string(property)+":"+value+"}",true);
 for(auto value:{"min-content","max-content","fit-content","0 1 min-content","2 3 max-content","0 1 fit-content"})check("div{flex:"+std::string(value)+"}",true);
 for(auto property:{"width","min-width","max-width","flex-basis","flex"})for(auto value:{"min-content junk","max-content 2px","fit-content(150px)","calc-size(min-content,size)","min-content max-content"})check("div{"+std::string(property)+":"+value+"}",false);
 for(auto value:{"1 min-content","1 2 3 fit-content","-1 1 min-content"})check("div{flex:"+std::string(value)+"}",false);
 for(auto decl:{"writing-mode:vertical-rl","writing-mode:horizontal-tb","direction:ltr",R"(wr\69 ting-mode:vertical-rl)",R"(d\69 rection:rtl)"}){
  check("div{"+std::string(decl)+"}",false);check("@media print{div{"+std::string(decl)+"}}",false);
  check("@supports not (display:flex){div{"+std::string(decl)+"}}",false);
 }
 // Column syntax is not globally banned: only actual intrinsic-basis items
 // are refused by the core before layout, preserving ordinary/H7 columns.
 check("div{flex-direction:column}",true);
 for(auto property:{"height","min-height","max-height"})check("div{"+std::string(property)+":min-content}",false);
 assert(h5::analyze("width:min-content;flex:0 1 fit-content",false).valid);
 assert(!h5::analyze(R"(wr\69 ting-mode:vertical-rl)",false).valid);
 const std::string css=R"(@media screen{@supports (display:flex){div{width:m\69 n-content;min-width:max-content;max-width:fit-content;flex:0 1 fit-content}}})";
 size_t oom=0;bool done=false;for(long n=0;n<3000;++n){fail_after=n;try{auto a=h5::analyze(css,true);fail_after=-1;assert(a.valid);done=true;break;}catch(const std::bad_alloc&){fail_after=-1;++oom;}}
 assert(done&&h5::analyze(css,true).valid);std::cout<<"H8 gate checks="<<checks<<" OOM="<<oom<<"\n";
}
