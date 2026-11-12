#include "../src/css/contextual_math.h"
#include <cassert>
#include <iostream>
using namespace aether::css;
static int assertions=0;
#define CHECK(v) do {++assertions;assert(v);} while(0)
static math_tree parse(const std::string& s) {math_tree t;if(!parse_contextual_math(s,t)){std::cerr<<"parse failed: "<<s<<"\n";CHECK(false);}return t;}
static double used(const math_tree& t,double basis) {double out=-777;CHECK(resolve_contextual_math(t,basis,out));return out;}
static void reject(const std::string& s) {auto sentinel=parse("1px");auto out=sentinel;CHECK(!parse_contextual_math(s,out));CHECK(out==sentinel);}
int main() {
 auto header=compute_contextual_math(parse("max(24px,calc((100% - 1180px)/2))"),16,16,800,600);
 CHECK(used(header,1000)==24);CHECK(used(header,1228)==24);CHECK(used(header,1280)==50);CHECK(used(header,1600)==210);
 auto clamp=parse("clamp(80px,30%,180px)");CHECK(used(clamp,200)==80);CHECK(used(clamp,500)==150);CHECK(used(clamp,800)==180);
 CHECK(used(parse("clamp(180px,100px,80px)"),0)==180);
 CHECK(used(parse("min(10px, max(2px, calc(5px * 2)))"),0)==10);
 CHECK(used(parse("max(-10px, -5px)"),0)==-5); // property clamps only final used result
 CHECK(used(parse("max(24px,50%)"),0)==24); // intrinsic cyclic phase supplied by caller
 auto escaped=parse("m\\61 x(1\\65 m, 10%)");CHECK(math_dependencies(escaped)==(math_percent|math_self_font));
 auto inherited=compute_contextual_math(escaped,20,16,1000,600);CHECK(used(inherited,100)==20);CHECK(math_dependencies(inherited)==math_percent);
 CHECK(used(compute_contextual_math(parse("max(1rem,2vw,3vh,1em)"),20,30,2000,1000),0)==40);
 CHECK(math_dependencies(parse("max(1rem,2vw,3vh,1em,2%)"))==15);
 CHECK(contextual_math_equal(inherited,compute_contextual_math(escaped,20,16,1000,600)));
 CHECK(!contextual_math_equal(inherited,compute_contextual_math(escaped,40,16,1000,600)));
 CHECK(used(parse("max(2px, calc(2 * 3px / min(2, 3)))"),0)==3);
 CHECK(used(parse("MAX(1px,/*x*/2px)"),0)==2);
 CHECK(used(parse("max(calc(1px /*x*/ + /*y*/ 2px), 2px)"),0)==3);
 CHECK(used(parse("max(1p\\78,2px)"),0)==2);
 CHECK(used(parse("0"),0)==0);
 for(const auto& s:{"calc(0)","min(0,0)","clamp(0,0,0)","max()","min(1px,)","clamp(1px,2px)","clamp(1px,2px,3px,4px)","max(1px,2)","calc(2px * 3px)","calc(2px / 1px)","calc(1px / 0)","calc(1px / (2 - 2))","calc(1px+ 2px)","calc(1px +2px)","calc(1px/**/+/**/2px)","max (1px,2px)","max/**/(1px,2px)","max(1px,2px) junk","max(1px,none)","max(NaN,1px)","max(infinity,1px)","max(1px,fit-content(2px))","max(1px,1e999px)","max(1px,1e-999px)","max(0x1px,2px)","max(1.px,2px)","max(1px,2px)/*","max(1px,2px)\\","calc(1px / max(0,0))","calc(1e308 * 1e308 * 1px)","1px + 2px","(1px)","2"})reject(s);
 std::string deep="1px";for(int i=0;i<34;++i)deep="max(1px,"+deep+")";reject(deep);
 std::string many="max(";for(int i=0;i<300;++i)many+=(i?",1px":"1px");many+=")";reject(many);
 std::string chain="calc(1px";for(int i=0;i<35;++i)chain+=" + 1px";chain+=")";reject(chain);
 reject(std::string(17000,' ') + "1px");
 double unchanged=123;CHECK(!resolve_contextual_math(parse("1em"),100,unchanged));CHECK(unchanged==123);
 CHECK(!resolve_contextual_math(header,std::numeric_limits<double>::infinity(),unchanged));CHECK(unchanged==123);
 bool threw=false;try{compute_contextual_math(parse("max(1em,2px)"),std::numeric_limits<float>::infinity(),16,100,100);}catch(const std::overflow_error&){threw=true;}CHECK(threw);
 threw=false;try{compute_contextual_math(parse("max(calc(3e38px * 2),1px)"),16,16,100,100);}catch(const std::overflow_error&){threw=true;}CHECK(threw);
 std::cout<<"contextual_math: "<<assertions<<" checks PASS\n";
}
