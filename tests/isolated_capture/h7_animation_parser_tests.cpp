#include "../../native/src/style_runtime/paused_animation_properties.h"
#include <cassert>
#include <iostream>
#include <new>
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
int main(){
 std::string v="sentinel";size_t n=0;
 auto good=[&](size_t i,const std::string&s,const std::string&w){assert(paused_animation::parse(i,s,v));assert(v==w);++n;};
 auto bad=[&](size_t i,const std::string&s){v="sentinel";assert(!paused_animation::parse(i,s,v));assert(v=="sentinel");++n;};
 good(0,"Foo","Foo");good(0,R"(F\6f o)","Foo");good(0,R"(\5c 66lex)",R"(\66lex)");good(0,"NONE","none");
 good(1,"10s","10");good(1,"1000ms","1");good(2,"-5s","-5");good(2,"-500ms","-0.5");good(1,"0s","0");
 good(3,"LINEAR","linear");good(3,"ease","ease");good(4,"1.0","1");good(4,"infinite","infinite");good(7,"PAUSED","paused");
 for(auto s:{"0","1 s","nan", "inf", "1e999s", "-1s", "1s junk", "1s,2s", "calc(1s)"})bad(1,s);
 for(auto s:{"foo bar", "foo,bar", "inherit", "default", "\"Foo\""})bad(0,s);
 for(auto s:{"foo 10s linear -5s paused",R"(F\6f o 10000ms linear -5000ms paused)","none"}){assert(paused_animation_declaration_supported("animation",s));++n;}
 for(auto s:{"foo 10s ease -5s paused","foo 10s linear -5s running","foo 0s linear paused","foo 10s linear paused,bar 1s","foo 10s linear paused 2","foo 10s linear paused both","foo 10s linear paused reverse","foo 10s linear paused junk"}){assert(!paused_animation_declaration_supported("animation",s));++n;}
 style_shorthand_writer_s w;w.set("sentinel","old");assert(!paused_animation::expand("foo 1s linear paused ???",w));assert(w.items.size()==1);
 assert(paused_animation::expand("foo 1s linear paused",w));assert(w.items.size()==9);assert(w.items[1].value=="foo");
 size_t failures=0;bool complete=false;
 for(long i=0;i<1000;++i){style_shorthand_writer_s candidate;candidate.set("sentinel","old");fail_after=i;
  try{bool ok=paused_animation::expand("SomeLongAnimationName 10s linear -5s paused",candidate);fail_after=-1;assert(ok&&candidate.items.size()==9);complete=true;break;}
  catch(const std::bad_alloc&){fail_after=-1;assert(candidate.items.size()==1&&candidate.items[0].value=="old");++failures;}}
 assert(complete);
 style_property_registry_s registry;registry.definitions.reserve(8);paused_animation_register_longhands(&registry);
 std::vector<cascade_entry_s> cascade(8);
 auto sample=[&](const char* text){style_shorthand_writer_s declaration;assert(paused_animation::expand(text,declaration));
  for(size_t i=0;i<8;++i){cascade[i].is_set=true;cascade[i].value.mode=compiled_value_mode::value;assert(paused_animation::parse(i,declaration.items[i].value,cascade[i].value.typed_value));}
  return paused_animation_read(cascade,&registry);};
 for(auto delay:{"-2.5s","-4.9s","-5s","-5.1s","-7.5s"}){auto p=sample((std::string("Foo 10s linear paused ")+delay).c_str());assert(p.profile_supported&&p.active&&p.name=="Foo");++n;}
 for(auto delay:{"1s","-10s","-10.1s"}){auto p=sample((std::string("Foo 10s linear paused ")+delay).c_str());assert(p.profile_supported&&!p.active);++n;}
 assert(sample("Foo 10s linear paused -5s").progress==0.5);
 assert(!sample("Foo 10s ease paused -5s").profile_supported);
 assert(!sample("Foo 10s linear running -5s").profile_supported);
 assert(!sample("none").active);
 std::cout<<"H7 animation parser checks="<<n+7<<" atomic OOM="<<failures<<"\n";
}
