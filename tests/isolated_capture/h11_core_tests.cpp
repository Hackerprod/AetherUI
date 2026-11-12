#include "h11_profile.h"
#include "../../native/src/dom/dom_types.h"
#include <aether/aether.h>
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <new>
static long budget=-1;
void* operator new(size_t n){if(budget==0)throw std::bad_alloc();if(budget>0)--budget;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
struct Doc{aether_doc_t d{};Doc(const std::string& s){assert(aether_doc_create(&d)==AETHER_OK);assert(aether_doc_parse_html(d,s.c_str())==AETHER_OK);}~Doc(){aether_doc_destroy(d);}};
int main(){size_t checks=0;Doc d("<html><head><meta id='meta'><style></style></head><body><div class='row' id='p'><canvas id='c'></canvas><div class='peer' data-v='x'></div></div><div id='hidden' style='display:none'></div></body></html>");
 auto test=[&](const std::string& selector,bool absent){h11::Proof p(d.d);auto r=p.retained(selector+"{min-height:72px}");assert(r.empty()==absent);assert(p.omitted==(absent?1u:0u));++checks;};
 for(auto s:{"#absent",".absent","missing","#not-there > canvas","#p > .missing","#p .missing","#p + .missing","#p ~ .missing","canvas:nth-child(9)","#p>canvas:nth-child(2n)","canvas:not-made-up","#none,#nope",R"(#\61 bsent)","[missing]","[data-v='absent']","#none/*x*/ > /*y*/canvas"}){
  if(std::string(s)=="canvas:not-made-up")test(s,false);else test(s,true);
 }
 for(auto s:{"html","head","meta","style","body","#p","#c","#hidden","[data-v]","[data-v='x']","#absent,canvas","#absent, #hidden","#p > canvas","#p .peer","canvas + .peer","canvas ~ .peer","canvas:nth-child(1)","canvas:first-child","canvas:empty","html:root",R"(#\63 )","#none,","#none:hover","#none::before","#none:not(:hover)","#none:is(.x)","#none:has(div)","#none:not(.x)","#none:where(.x)","#none > > canvas","#none, #other:hover","#none:where(:not(:hover))"})test(s,false);
 // All CSS in conditional bodies and at-rules is retained, not merely active CSS.
 for(auto s:{"@media print{#none{min-height:72px}}","@supports(display:flex){#none{min-height:72px}}","@keyframes x{from{min-height:72px}}","#none{min-height:72px","#none{min-height:72px} }","@import 'x';#none{min-height:72px}"}){h11::Proof p(d.d);assert(!p.retained(s).empty());++checks;}
 {h11::Proof p(d.d);auto s=std::string("#absent{min-height:72px}#c{padding:0}");auto r=p.retained(s);assert(r=="#c{padding:0}\n"&&p.omitted==1);++checks;}
 {h11::Proof p(d.d);p.remaining=0;assert(!p.retained("#absent{min-height:72px}").empty()&&p.queries==0);++checks;}
 {h11::Proof p(d.d);p.rules_left=0;assert(!p.retained("#absent{min-height:72px}").empty()&&p.queries==0);++checks;}
 {h11::Proof p(d.d);auto s=std::string(513,'x')+"{min-height:72px}";assert(p.retained(s)==s+"\n"&&p.queries==0);++checks;}
 {std::string html="<body>";for(int i=0;i<1025;++i)html+="<div></div>";Doc big(html);h11::Proof p(big.d);assert(!p.retained("#absent{min-height:72px}").empty()&&p.queries==0);++checks;}
 {std::string s;for(int i=0;i<513;++i)s+="#none{min-height:72px}";h11::Proof p(d.d);assert(p.retained(s)==s&&p.queries==0);++checks;}
 {h11::Proof p(d.d);auto s="a a a a a{min-height:72px}";assert(!p.retained(s).empty()&&p.queries==0);++checks;}
 {h11::Proof p(d.d);std::string s;for(int i=0;i<17;++i){if(i)s+=",";s+="#absent";}s+="{min-height:72px}";assert(!p.retained(s).empty()&&p.queries==0);++checks;}
 {h11::Proof p(d.d);assert(!p.retained("[id*='missing']{min-height:72px}").empty()&&p.queries==0);++checks;}
 {h11::Proof p(d.d);assert(!p.retained(".a.b.c.d.e.f.g.h{min-height:72px}").empty()&&p.queries==0);++checks;}
 {Doc deep("<body>"+std::string(9000,' ') + "<div title='"+std::string(8200,'x')+"'></div></body>");h11::Proof p(deep.d);assert(!p.retained("#none{min-height:72px}").empty()&&p.queries==0);++checks;}
 {h11::Proof p(d.d);assert(!p.retained("#none{min-height:72px}#bad[").empty()&&p.omitted==0&&p.queries==0);++checks;}
 // No absence conclusion is reused by the caller across a different DOM.
 {Doc changed("<body><div id='absent'></div></body>");h11::Proof p(changed.d);assert(!p.retained("#absent{min-height:72px}").empty());++checks;}
 size_t oom=0;bool done=false;const std::string css="#none > canvas:nth-child(9),#other{min-height:72px}#c{padding:0}";
 for(long n=0;n<2000;++n){std::string result="UNPUBLISHED";budget=n;try{h11::Proof p(d.d);result=p.retained(css);budget=-1;assert(result=="#c{padding:0}\n");done=true;break;}catch(const std::bad_alloc&){budget=-1;assert(result=="UNPUBLISHED");++oom;}}
 assert(done&&oom>0);std::cout<<"H11 proof="<<checks<<" OOM="<<oom<<"\n";
}
