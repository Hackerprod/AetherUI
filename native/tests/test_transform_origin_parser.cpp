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
bool same(float a, float b) { return std::fabs(a-b) < .001f; }
void valid(const std::string& source, float x, float y,
           aether_css_unit_t xu = AETHER_CSS_UNIT_PERCENT, aether_css_unit_t yu = AETHER_CSS_UNIT_PERCENT) {
    aether_transform_origin origin{};
    const bool ok = aether_transform_origin_try_parse(source.c_str(), &origin);
    check(ok, "accept " + source);
    if (ok) check(origin.x.unit == xu && origin.y.unit == yu && same(origin.x.value,x) && same(origin.y.value,y), "axes " + source);
}
void invalid(const std::string& source) {
    aether_transform_origin sentinel{};
    sentinel.x.value = 123.f; sentinel.x.unit = AETHER_CSS_UNIT_EM; sentinel.x.linear_percent = 44.f;
    sentinel.y.value = -17.f; sentinel.y.unit = AETHER_CSS_UNIT_REM; sentinel.y.linear_em = 3.f;
    auto origin = sentinel;
    check(!aether_transform_origin_try_parse(source.c_str(), &origin), "reject " + source);
    check(std::memcmp(&origin, &sentinel, sizeof(origin)) == 0, "unchanged rejected output " + source);
}
}
int main() {
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
    check(calc.x.unit==AETHER_CSS_UNIT_LINEAR && same(calc.x.linear_percent,25) && same(calc.x.linear_em,2) && same(calc.x.value,3) && same(calc.y.value,-8), "symbolic calc retained");
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
