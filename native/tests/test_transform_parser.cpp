#include <aether/aether.h>
#include "transforms.h"
#include "../src/style_runtime/css_variable_tokens.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}
bool near(float a, float b) { return std::fabs(a-b) < .0001f; }
struct List {
    aether_transform_list value{};
    ~List() { aether_transform_list_free(&value); }
};
void valid(const std::string& source, int count = 1) {
    List parsed;
    const bool ok = aether_transform_list_try_parse(source.c_str(), &parsed.value);
    check(ok, "accept " + source);
    if (ok) check(parsed.value.count == count && (count == 0 || parsed.value.items), "operation count " + source);
}
void invalid(const std::string& source) {
    List parsed;
    check(!aether_transform_list_try_parse(source.c_str(), &parsed.value), "reject " + source);
    check(!parsed.value.items && parsed.value.count == 0, "no partial list " + source);
}
}
int main() {
    for (const char* value : {"none", "NONE", "/**/NoNe/**/", "n\\6f ne", "\\6e one", " \t\r\n\fnone \t"}) valid(value, 0);
    for (const char* value : {"scale(2)", "scale(2,3)", "scaleX(-2.5)", "scaleY(+.5e1)",
        "matrix(1,0,0,1,-30,+4e1)", "translate(1px)", "translate(1px,2%)", "translateX(-2em)",
        "translateY(3REM)", "translate(2vw,3vh)", "translate(calc(10% + 2px),calc(2 * 3em))",
        "translateX(calc(2px / 2))", "rotate(90deg)", "rotate(100grad)", "rotate(1RAD)", "rotate(.25TURN)",
        "skewX(-45deg)", "skewY(+45deg)", "ScAlE(2/**/,/**/3)", "r\\6f tate(20deg)", "\\73 caleX(2)",
        "translate(/**/1px/**/,/**/2px/**/)", "scale(1.0000000596046448)", "scale(1e-30)",
        "translateX(1e-100px)", "translateX(calc(1e-100 * 1px))"}) valid(value);
    for (const char* zero : {"0", "+0", "-0", "0.0", ".0", "+0e2", "0e-3"}) {
        for (const char* fn : {"translateX", "translateY", "rotate", "skewX", "skewY"})
            valid(std::string(fn) + "(" + zero + ")");
    }
    for (const char* value : {"rotate(90d\\65 g)", "rotate(.25t\\75rn)", "rotate(1\\72 ad)",
        "skewX(100g\\72 ad)", "translateX(10p\\78)", "translateY(2\\65m)", "translate(1r\\65m,2v\\77)",
        "translateY(3v\\68)", "translateX(calc(1p\\78  + 2%))", "translateX(calc(1\\70x + 2%))",
        "translateX(calc(1p\\78 /**/ + 2%))", "translateX(calc(1p\\78 * 2))", "translateX(calc(1p\\78  + 2p\\78))"}) valid(value);
    for (const char* value : {"translateX(1\\65 2px)", "translateX(calc(1\\65 2px + 2px))", "rotate(1\\65 2deg)",
        "translateX(1p\\2f x)", "translateX(1\\25)", "translateX(1\\69n)", "translateX(calc(1p\\78 + 2%))",
        "translateX(calc(1p\\78/**/+ 2%))", "translateX(calc(1p\\78 /**/+ 2%))", "rotate(1d\\65 gfoo)",
        "translateX(1p\\0x)", "translateX(1p\\110000x)", "translateX(1p\\78 2px)"}) invalid(value);
    for (const char* source : {"+12.5e-2p\\78", "12p\\78", "1\\65 2px", "1.2e3%", "-.5e+1", "12px"}) {
        const std::string value = source;
        css_variable_lexer lexer(value.data(), value.size());
        const auto token = lexer.next();
        const auto expected = value.starts_with("+12") ? 8u : value.starts_with("1.2") ? 5u : value.starts_with("-.") ? 6u : value.starts_with("12") ? 2u : 1u;
        check(token.numeric_end == expected && token.end == value.size(), "lexer numeric boundary " + value);
    }
    for (const char* value : {
        "translateX(c\\61 lc(1px))",
        "translateY(\\63 alc(1px))",
        "translate(c\\61 lc(1px + 2%),\\63 alc(3px))",
        "translateX(C\\41 LC(1p\\78  + 2%))",
        "translateX(\\63\\61\\6c\\63(1px))"}) valid(value);
    for (const char* value : {
        "translateX(c\\61 lc/**/(1px))",
        "translateX(c\\61 lc (1px))",
        "translateX(calc\\(1px\\))",
        "translateX(c\\61 lc(0))",
        "translateX(c\\61 lc(1 + 2))",
        "translateX(c\\61 lc(1px+ 2px))",
        "translateX(c\\61 lc(1p\\78 + 2%))",
        "translateX(c\\61 lc(1\\65 2px))",
        "translateX(c\\61 lc(1px / 0))",
        "translateX(c\\61 lc(c\\61 lc(1px)))",
        "translateX(m\\69 n(1px,2px))",
        "rotate(c\\61 lc(1deg))",
        "scale(c\\61 lc(1 + 1))"}) invalid(value);
    valid("rotate(1deg)scale(2)", 2);
    valid("/**/translate(1px,2px)/**/rotate(3deg) scale(4,5)/**/", 3);
    for (const char* value : {"", " ", "/**/", "no/**/ne", "none none", "none scale(2)", "scale(2) none",
        "none()", "none)", "none /*", "scale (2)", "sca/**/le(2)", "scale()", "scale(2 3)", "scale(2/**/3)",
        "scale(2/**/+3)", "scale(,2)", "scale(2,)", "scale(2,,3)", "scale(2,3,4)", "scaleX(2,3)", "scaleY(2 3)",
        "translate(1px 2px)", "translate(,1px)", "translate(1px,)", "translate(1px,,2px)", "translate(1px,2px,3px)",
        "translateX(1px,2px)", "translateY(1px 2px)", "matrix(1 0 0 1 2 3)", "matrix(1,0,0,1,2)",
        "matrix(1,0,0,1,2,3,4)", "matrix(1,0,0,1,2 3)", "matrix(1,0,0,1,2,3,)", "matrix(1,,0,0,1,2,3)",
        "rotate(1deg,2deg)", "rotate(1deg 2deg)", "skewX()", "skewY(1deg,2deg)", "translateX(12)",
        "translateX(-.5)", "translateX(calc(0))", "translateX(calc(1 + 2))", "translateX(auto)",
        "translateX(1deg)", "translateX(1pt)", "translateX(calc(1px + 1))", "translateX(calc(2px * 3px))",
        "translateX(calc(1px / 0))", "translateX(1e-999)", "translateX(0x0px)", "translateX(0x1p2)",
        "scale(0x1p2)", "scale(0x1)", "scale(nan)", "scale(inf)", "scale(-infinity)", "scale(2.)", "scale(2.e1)",
        "scale(1e)", "scale(1e+)", "scale(2px)", "scale(50%)", "scale(calc(1 + 1))", "scale(1e100)",
        "scale(1e-40)", "scale(1e-100)", "matrix(1,0,0,1,0x1p2,0)", "matrix(1,0,0,1,0,1e-100)",
        "rotate(0x1p2deg)", "rotate(2.deg)", "rotate(2e+deg)", "rotate(1)", "rotate(2px)", "rotate(50%)",
        "rotate(calc(0deg))", "rotate(1e-100)", "rotate(1e-100deg)", "rotate(1e100deg)", "rotate(1e38turn)",
        "skew(1deg,2deg)", "translateZ(1px)", "translate3d(1px,2px,3px)", "scale3d(1,2,3)", "rotateX(0deg)",
        "rotate3d(1,0,0,90deg)", "perspective(2px)", "matrix3d(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)",
        "scale(2),rotate(1deg)", "scale(2);", "scale(2)foo(1)", "scale(2)translateX(12)", "scale(2)rotate(nope)",
        "scale(2", "scale(2])", "[scale(2)]", "scale(2)/**", "scale(2))", "scale(2)!important"}) invalid(value);

    List all;
    check(aether_transform_list_try_parse("translateX(4px) translateY(5%) scaleX(2) scaleY(3) scale(4) rotate(100grad) skewX(1rad) skewY(.25turn) matrix(1,2,3,4,5,6)", &all.value), "all operation kinds");
    if (all.value.count == 9) {
        const auto* op = all.value.items;
        check(op[0].kind == AETHER_TRANSFORM_TRANSLATE && op[0].x.value == 4 && op[0].y.value == 0, "translateX axes");
        check(op[1].kind == AETHER_TRANSFORM_TRANSLATE && op[1].x.value == 0 && op[1].y.value == 5 && op[1].y.unit == AETHER_CSS_UNIT_PERCENT, "translateY axes");
        check(op[2].scalar_x == 2 && op[2].scalar_y == 1 && op[3].scalar_x == 1 && op[3].scalar_y == 3, "scale axis defaults");
        check(op[4].scalar_x == 4 && op[4].scalar_y == 4, "single scale copies first axis");
        check(op[5].kind == AETHER_TRANSFORM_ROTATE && near(op[5].angle_radians, 1.5707963f), "grad normalization");
        check(op[6].kind == AETHER_TRANSFORM_SKEW_X && op[6].angle_radians == 1, "skewX radians");
        check(op[7].kind == AETHER_TRANSFORM_SKEW_Y && near(op[7].angle_radians, 1.5707963f), "skewY turns");
        check(op[8].matrix_a == 1 && op[8].matrix_b == 2 && op[8].matrix_c == 3 && op[8].matrix_d == 4 && op[8].matrix_e == 5 && op[8].matrix_f == 6, "matrix order");
    } else check(false, "all operation count");
    List calc;
    check(aether_transform_list_try_parse("translate(calc(25% + 2em + 3px),calc(-2 * 4px))", &calc.value), "symbolic calc");
    if (calc.value.count == 1) {
        const auto& op = calc.value.items[0];
        check(op.x.unit == AETHER_CSS_UNIT_LINEAR && op.x.value == 3 && op.x.linear_percent == 25 && op.x.linear_em == 2 && op.y.value == -8, "symbolic lengths retained");
    }
    check(!aether_transform_list_try_parse(nullptr, &calc.value), "null source");
    check(!aether_transform_list_try_parse("none", nullptr), "null output");
    aether_css_length ordinary{};
    check(aether_css_length_try_parse("12", &ordinary) && ordinary.value == 12 && ordinary.unit == AETHER_CSS_UNIT_PX, "public length compatibility unchanged");
    std::printf("transform parser: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
