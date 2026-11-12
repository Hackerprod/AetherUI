#include "../../native/src/style_runtime/style_runtime_types.h"
#include "../../native/src/style_runtime/supports_condition.h"
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include <cassert>
#include <iostream>
#include <new>
#include <cstdlib>
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
int main(){
 style_property_registry_init();
 size_t count=0;
 auto check=[&](const std::string&s,bool syntax,bool match,bool profile){size_t budget=65536;auto e=supports_condition_probe(s,budget);if(e.syntax_valid!=syntax||e.matches!=match||e.profile_supported!=profile){std::cerr<<s<<" got "<<e.syntax_valid<<e.matches<<e.profile_supported<<"\n";std::abort();}assert(!e.budget_exhausted);++count;};
 for(auto s:{"(display:flex)","( DISPLAY : FLEX )","(display:f\\6c ex)","(d\\69 splay:flex)","(display:/**/flex/**/)","(display:initial)","(display:inherit)","(display:unset)","(display:flex !important)","((display:flex))","(display:flex) and (display:block)","not (display:bogus)"}) check(s,true,true,std::string(s)!="not (display:bogus)");
 check("not (display:flex)",true,false,true);check("(display:bogus)",true,false,false);check("(foo:bar)",true,false,false);
 check("(display:revert)",true,true,false);check("(display:revert-layer)",true,false,false);check("(display:line-break)",true,true,false);
 check("selector(.a)",true,false,false);check("(future foo)",true,false,false);check("not selector(.a)",true,true,false);
 check("(display:flex) or selector(.a)",true,true,false);check("not (display:flex) and selector(.a)",false,false,false);
 for(auto s:{"(display:flex) junk","(display:flex","(display:flex))","(display:flex) and (display:block) or (display:none)","not not (display:flex)","(display:flex) and"})check(s,false,false,false);
 for(auto s:{"()","(42)","(not)","(display:flex;display:none)","(display:flex;)"})check(s,true,false,false);
 check("(display:flex !important junk)",true,false,false);
 check(R"((display:\5c 66lex))",true,false,false);
 check(R"((display:\5c 69nitial))",true,false,false);
 check(R"((display:\69 nitial))",true,true,true);
 size_t budget=1;auto e=supports_condition_probe("(display:flex)",budget);assert(e.budget_exhausted&&budget==0);
 std::string deep="(display:flex)";for(int i=0;i<32;++i)deep="("+deep+")";check(deep,false,false,false);
 // Probe must not change compiler IDs or invoke diagnostic callbacks.
 int32_t id=17;std::vector<compiled_rule_s> rules;
 assert(style_source_compile("@supports (display:flex){a{display:block}}",1,0,rules,id,nullptr,nullptr)==AETHER_OK);assert(rules.size()==1&&id==18);
 rules.clear();id=17;assert(style_source_compile("@supports not (display:flex){a{display:block}}b{display:flex}",1,0,rules,id,nullptr,nullptr)==AETHER_OK);assert(rules.size()==1&&id==18);
 rules.clear();id=17;assert(style_source_compile("@supp\\6f rts (display:flex){a{display:f\\6c ex}}",1,0,rules,id,nullptr,nullptr)==AETHER_OK);assert(rules.size()==1&&rules[0].declarations.size()==1);
 // Combined conditional depth is bounded even when the outer branch is false.
 for(bool inactive:{false,true})for(int depth:{32,33}){
  std::string sheet="a{display:block}";
  for(int i=0;i<depth;++i)sheet=(i%2?"@media all{":(inactive?"@supports not (display:flex){":"@supports (display:flex){"))+sheet+"}";
  rules.clear();id=17;auto st=style_source_compile(sheet.c_str(),1,0,rules,id,nullptr,nullptr);
  assert(st==(depth==32?AETHER_OK:AETHER_ERROR_INVALID_STATE));
  if(depth==33)assert(rules.empty()&&id==17);++count;
 }
 // Success and failure both charge the shared token budget; no reset at OR/not.
 budget=12;auto one=supports_condition_probe("(display:flex)",budget);assert(one.matches);
 auto two=supports_condition_probe("(display:flex)",budget);assert(two.matches);
 auto three=supports_condition_probe("(display:flex)",budget);assert(three.budget_exhausted);
 rules.clear();id=17;assert(style_source_compile(R"(@supports (display:\5c 66lex){a{display:flex}})",1,0,rules,id,nullptr,nullptr)==AETHER_OK);assert(rules.empty()&&id==17);
 rules.clear();id=17;assert(style_source_compile(R"(@supports not (display:\5c 66lex){a{display:flex}})",1,0,rules,id,nullptr,nullptr)==AETHER_OK);assert(rules.size()==1&&id==18);
 // Decoded backslashes remain identifier data, never re-lexed into keywords.
 rules.clear();id=17;assert(style_source_compile(R"(a{display:\5c 66lex})",1,0,rules,id,nullptr,nullptr)==AETHER_OK);assert(rules.empty()&&id==17);
 rules.clear();id=17;assert(style_source_compile(R"(a{display:\5c 69nitial})",1,0,rules,id,nullptr,nullptr)==AETHER_OK);assert(rules.empty()&&id==17);
 rules.clear();id=17;assert(style_source_compile(R"(a{display:\69 nitial})",1,0,rules,id,nullptr,nullptr)==AETHER_OK);assert(rules.size()==1&&rules[0].declarations[0].value.mode==compiled_value_mode::initial);
 // Stylesheet publication is transactional for every allocation failure.
 size_t failures=0;bool done=false;
 for(long n=0;n<1000;++n){rules.clear();id=17;fail_after=n;try{auto st=style_source_compile("@media (min-width:1px){@supports (display:flex) and (display:block){a{display:flex}b{display:none}}}",1,0,rules,id,nullptr,nullptr);fail_after=-1;assert(st==AETHER_OK&&rules.size()==2&&id==19);done=true;break;}catch(const std::bad_alloc&){fail_after=-1;assert(rules.empty()&&id==17);++failures;}}
 assert(done);std::cout<<"H6 core checks="<<count<<" transactional OOM="<<failures<<"\n";
}
