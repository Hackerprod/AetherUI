#include <aether/aether.h>
#include "transforms.h"
#include "transform_matrix.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}
bool same(float a, float b) { return std::isfinite(a) && std::isfinite(b) && std::fabs(a-b) < .001f; }
aether_css_length length(aether_css_unit_t unit, float value, float percent=0,
                         float em=0, float rem=0, float vw=0, float vh=0) {
    return {value,unit,percent,em,rem,vw,vh};
}
bool same_length(const aether_css_length& a, const aether_css_length& b) {
    return a.unit==b.unit && same(a.value,b.value) && same(a.linear_percent,b.linear_percent) &&
        same(a.linear_em,b.linear_em) && same(a.linear_rem,b.linear_rem) &&
        same(a.linear_vw,b.linear_vw) && same(a.linear_vh,b.linear_vh);
}
void symbolic(const std::string& source, aether_css_length x, aether_css_length y) {
    aether_transform_origin result{};
    const bool ok=aether_transform_origin_try_parse(source.c_str(),&result);
    check(ok,"accept symbolic " + source);
    if(ok) check(same_length(result.x,x) && same_length(result.y,y),"all symbolic fields " + source);
}
void valid(const std::string& source, float x, float y,
           aether_css_unit_t xu = AETHER_CSS_UNIT_PERCENT, aether_css_unit_t yu = AETHER_CSS_UNIT_PERCENT) {
    aether_transform_origin origin{};
    const bool ok = aether_transform_origin_try_parse(source.c_str(), &origin);
    check(ok, "accept " + source);
    if (ok) check(same_length(origin.x,length(xu,x)) && same_length(origin.y,length(yu,y)), "all axes fields " + source);
}
void invalid(const std::string& source) {
    aether_transform_origin sentinel{};
    sentinel.x=length(AETHER_CSS_UNIT_LINEAR,123,44,2,3,5,7);
    sentinel.y=length(AETHER_CSS_UNIT_LINEAR,-17,19,23,29,31,37);
    auto origin = sentinel;
    unsigned char before[sizeof(origin)];
    std::memcpy(before,&origin,sizeof(origin));
    check(!aether_transform_origin_try_parse(source.c_str(), &origin), "reject " + source);
    check(std::memcmp(&origin, before, sizeof(origin)) == 0, "unchanged rejected output bytes " + source);
    check(same_length(origin.x,sentinel.x) && same_length(origin.y,sentinel.y), "unchanged rejected output fields " + source);
}
void escaped_family() {
    const auto percent50=length(AETHER_CSS_UNIT_PERCENT,50);
    const auto px10=length(AETHER_CSS_UNIT_PX,10);
    const auto px20=length(AETHER_CSS_UNIT_PX,20);
    struct Unit {
        const char* ordinary;
        aether_css_unit_t kind;
        const char* escaped[4];
        aether_css_length calc;
    };
    const Unit units[] = {
        {"px",AETHER_CSS_UNIT_PX,{R"(p\x)",R"(p\78 )",R"(p\000078)",R"(P\58 )"},length(AETHER_CSS_UNIT_LINEAR,3,25)},
        {"em",AETHER_CSS_UNIT_EM,{R"(e\m)",R"(\65 m)",R"(\000065m)",R"(\45 M)"},length(AETHER_CSS_UNIT_LINEAR,0,25,3)},
        {"rem",AETHER_CSS_UNIT_REM,{R"(re\m)",R"(\72 em)",R"(\000072em)",R"(\52 EM)"},length(AETHER_CSS_UNIT_LINEAR,0,25,0,3)},
        {"vw",AETHER_CSS_UNIT_VW,{R"(\vw)",R"(\76 w)",R"(\000076w)",R"(\56 W)"},length(AETHER_CSS_UNIT_LINEAR,0,25,0,0,3)},
        {"vh",AETHER_CSS_UNIT_VH,{R"(\vh)",R"(\76 h)",R"(\000076h)",R"(\56 H)"},length(AETHER_CSS_UNIT_LINEAR,0,25,0,0,0,3)}
    };
    const char* functions[]={"calc","CALC",R"(ca\lc)",R"(c\61 lc)",R"(\63 alc)",R"(\000063alc)"};
    for(const auto& u:units) {
        // Every ordinary control and escaped spelling has independent typed expectations.
        for(const char* spelling:{u.ordinary,u.escaped[0],u.escaped[1],u.escaped[2],u.escaped[3]}) {
            const std::string a=std::string("3")+spelling, b=std::string("-2")+spelling;
            const auto x=length(u.kind,3), y=length(u.kind,-2);
            symbolic(a,x,percent50); symbolic(a+" 20px",x,px20);
            symbolic("10px "+b,px10,y); symbolic(a+" "+b,x,y);
            symbolic("left "+a,length(AETHER_CSS_UNIT_PERCENT,0),x);
            symbolic("center "+a,percent50,x);
            symbolic(a+" top",x,length(AETHER_CSS_UNIT_PERCENT,0));
            symbolic(a+" center",x,percent50);
            invalid("top "+a); invalid(a+" left");
            for(const char* function:functions) {
                const std::string calc=std::string(function)+"(3"+spelling+" + 25%)";
                symbolic(calc,u.calc,percent50); symbolic(calc+" 20px",u.calc,px20);
                symbolic("10px "+calc,px10,u.calc); symbolic(calc+" "+calc,u.calc,u.calc);
            }
        }
    }
    const auto combined=length(AETHER_CSS_UNIT_LINEAR,3,25,2,4,5,6);
    for(const char* c:{"calc(3px + 25% + 2em + 4rem + 5vw + 6vh)",
        R"(c\61 lc(3p\78  + 25% + 2e\m + 4re\m + 5\vw + 6\vh))"})
        symbolic(std::string(c)+" "+c,combined,combined);
    for(const char* c:{"calc((2px + 10%) * 3 / 2)",R"(ca\lc((2p\x + 10%) * 3 / 2))"})
        symbolic(c,length(AETHER_CSS_UNIT_LINEAR,3,15),percent50);
    symbolic(R"(-25% 3p\x)",length(AETHER_CSS_UNIT_PERCENT,-25),length(AETHER_CSS_UNIT_PX,3));
    symbolic(R"(3e\m 120%)",length(AETHER_CSS_UNIT_EM,3),length(AETHER_CSS_UNIT_PERCENT,120));
    symbolic(R"(c\61 lc(25% + 10p\x) \74 op)",length(AETHER_CSS_UNIT_LINEAR,10,25),length(AETHER_CSS_UNIT_PERCENT,0));
    valid("1e2px",100,50,AETHER_CSS_UNIT_PX);
    valid(R"(10p\78)",10,50,AETHER_CSS_UNIT_PX);
    valid(R"(10p\78  20px)",10,20,AETHER_CSS_UNIT_PX,AETHER_CSS_UNIT_PX);
    valid("10p\\78\t 20px",10,20,AETHER_CSS_UNIT_PX,AETHER_CSS_UNIT_PX);
    valid("10p\\78\r\n 20px",10,20,AETHER_CSS_UNIT_PX,AETHER_CSS_UNIT_PX);
    for(const char* c:{R"(calc(10p\78  + 25%))", "calc(10p\\78\t + 25%)", "calc(10p\\78\r\n + 25%)",
        R"(ca\lc(10p\x/**/ + /**/25%))"})
        symbolic(c,length(AETHER_CSS_UNIT_LINEAR,10,25),percent50);
    symbolic(R"(ca\lc(10p\x/**/ - /**/25%))",length(AETHER_CSS_UNIT_LINEAR,10,-25),percent50);
    for(const char* bad:{
        R"(10p\78 20px)",R"(calc(10p\78 + 25%) 20px)","top 20px","calc(0)",
        R"(c\61 lc/**/(10px) 20px)",R"(1\65 2px 20px)",
        "10p\\78\t20px","10p\\78\r\n20px", "calc(10p\\78\t+ 25%)", "calc(10p\\78\r\n+ 25%)",
        R"(calc(10p\x/**/+ 25%))",R"(calc(10p\x +/**/25%))",
        R"(calc(10p\x/**/- 25%))",R"(calc(10p\x -/**/25%))",
        R"(c\61 lc (10px))",R"(calc\(10px))",R"(ca/**/lc(10px))",
        R"(3p\t)",R"(3c\h)",R"(1\65 2px)",R"(m\69 n(1px,2px))",
        "calc(calc(1px))",R"(ca\lc(c\61 lc(1p\x)))",R"(ca\lc(0))",
        R"(ca\lc(1p\x + 2))",R"(ca\lc(2p\x * 3e\m))",R"(ca\lc(1p\x / 0))",
        R"(1e999p\x)",R"(ca\lc(3e38p\x * 2))", "10px 20p\\",
        R"(10p\x 20p\x 0)",R"(10p\x ,)",R"(10p\x ca\lc(20px)",
        R"(10p\x 20p\x !)",R"(10p\x left)",R"(top 20p\x)"}) invalid(bad);
    aether_transform_origin sentinel{length(AETHER_CSS_UNIT_EM,2,3,4,5,6,7),length(AETHER_CSS_UNIT_REM,8,9,10,11,12,13)};
    unsigned char before[sizeof(sentinel)]; std::memcpy(before,&sentinel,sizeof(sentinel));
    check(!aether_transform_origin_try_parse(nullptr,&sentinel),"null source rejects seeded output");
    check(std::memcmp(before,&sentinel,sizeof(sentinel))==0,"null source leaves every output byte unchanged");
}

}
int main() {
    escaped_family();
    const char* keywords[] = {"left", "right", "top", "bottom", "center"};
    const int expected[5][5][2] = {
        {{-1,-1},{-1,-1},{0,0},{0,100},{0,50}},
        {{-1,-1},{-1,-1},{100,0},{100,100},{100,50}},
        {{0,0},{100,0},{-1,-1},{-1,-1},{50,0}},
        {{0,100},{100,100},{-1,-1},{-1,-1},{50,100}},
        {{0,50},{100,50},{50,0},{50,100},{50,50}}
    };
    for (int i=0;i<5;++i) for (int j=0;j<5;++j) {
        const std::string value = std::string(keywords[i]) + " " + keywords[j];
        if (expected[i][j][0] < 0) invalid(value);
        else valid(value, float(expected[i][j][0]), float(expected[i][j][1]));
    }
    valid("left",0,50); valid("right",100,50); valid("top",50,0); valid("bottom",50,100); valid("center",50,50);
    valid("left 20px",0,20,AETHER_CSS_UNIT_PERCENT,AETHER_CSS_UNIT_PX);
    valid("right 20px",100,20,AETHER_CSS_UNIT_PERCENT,AETHER_CSS_UNIT_PX);
    valid("center 20px",50,20,AETHER_CSS_UNIT_PERCENT,AETHER_CSS_UNIT_PX);
    valid("20px top",20,0,AETHER_CSS_UNIT_PX); valid("20px bottom",20,100,AETHER_CSS_UNIT_PX);
    valid("20px center",20,50,AETHER_CSS_UNIT_PX);
    for (const char* value : {"top 20px","bottom 20px","20px left","20px right"}) invalid(value);
    for (const char* zero : {"0","-0",".0","+0e2","0e-3"}) valid(zero,0,50,AETHER_CSS_UNIT_PX);
    valid("-25% 120%",-25,120); valid("+2e1px -2.5px",20,-2.5f,AETHER_CSS_UNIT_PX,AETHER_CSS_UNIT_PX);
    const char* units[] = {"px","em","rem","vw","vh"};
    const aether_css_unit_t kinds[] = {AETHER_CSS_UNIT_PX,AETHER_CSS_UNIT_EM,AETHER_CSS_UNIT_REM,AETHER_CSS_UNIT_VW,AETHER_CSS_UNIT_VH};
    for (int i=0;i<5;++i) valid(std::string("3")+units[i],3,50,kinds[i]);
    valid("CENTER LEFT",0,50); valid("c\\65 nter l\\65 ft",0,50); valid("\\72 ight \\74 op",100,0);
    valid("center/**/left",0,50); valid("/**/center /* one */ right/**/",100,50);
    valid("20%top",20,0); valid("calc(20px)top",20,0,AETHER_CSS_UNIT_LINEAR);
    aether_transform_origin calc{};
    check(aether_transform_origin_try_parse("calc(25% + 2em + 3px) calc(-2 * 4px)",&calc), "length-valued calc");
    check(same_length(calc.x,length(AETHER_CSS_UNIT_LINEAR,3,25,2)) && same_length(calc.y,length(AETHER_CSS_UNIT_LINEAR,-8)), "all symbolic calc fields retained");
    for (const char* value : {"", " ","/**/","1","-2","1e2","calc(0)","calc(1 + 2)","calc(1px + 2)","calc(2px * 3px)","calc(1px / 0)","auto","none","inherit","initial","unset","revert","revert-layer","left right","top bottom","left,top","left;top","left top 0px","center left top","1px 2px 3px","[left] top","calc(20px","calc(20px])","cen/**/ter","center left)","1e999px","nan","inf","1pt","20pxleft","calc(20px)left","left !","center left /*"}) invalid(value);
    check(!aether_transform_origin_try_parse(nullptr,&calc), "null source");
    check(!aether_transform_origin_try_parse("center",nullptr), "null output");
    aether_css_length ordinary{};
    check(aether_css_length_try_parse("12",&ordinary) && ordinary.unit==AETHER_CSS_UNIT_PX && same(ordinary.value,12), "ordinary length number coercion unchanged");
    aether_transform_list rotation{};
    check(aether_transform_list_try_parse("rotate(90deg)",&rotation), "rotation fixture");
    for (const char* value : {"center left","left center","center right"}) {
        aether_transform_origin origin{};
        if (!aether_transform_origin_try_parse(value,&origin)) continue;
        const aether_pointf pivot{origin.x.value*2.f,50};
        auto matrix = aether_transform_matrix_resolve(rotation,origin,{0,0,200,100},16,16,{800,600});
        auto fixed = aether_transform_matrix_map_point(matrix,pivot);
        check(same(fixed.x,pivot.x) && same(fixed.y,pivot.y),std::string("fixed pivot ")+value);
        const aether_pointf p{17,23};
        auto q = aether_transform_matrix_map_point(matrix,p);
        const float determinant = matrix.m11*matrix.m22-matrix.m12*matrix.m21;
        aether_pointf back{((q.x-matrix.dx)*matrix.m22-(q.y-matrix.dy)*matrix.m21)/determinant,
            ((q.y-matrix.dy)*matrix.m11-(q.x-matrix.dx)*matrix.m12)/determinant};
        check(same(back.x,p.x)&&same(back.y,p.y),std::string("inverse mapping ")+value);
    }
    aether_transform_list_free(&rotation);
    std::printf("transform-origin parser: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
