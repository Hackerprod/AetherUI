#include "../src/css/background_layers.h"
#include "../src/style_runtime/style_property_registry_internal.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

void builtin_properties_register(style_property_registry_s*);
namespace {
int checks = 0, failures = 0;
void check(bool value, const std::string& label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}
bool equal(const aether_css_length& a, const aether_css_length& b) {
    return a.unit == b.unit && a.value == b.value && a.linear_percent == b.linear_percent &&
        a.linear_em == b.linear_em && a.linear_rem == b.linear_rem &&
        a.linear_vw == b.linear_vw && a.linear_vh == b.linear_vh;
}
aether_css_length length(float value, aether_css_unit_t unit = AETHER_CSS_UNIT_PERCENT) {
    aether_css_length result{}; result.value = value; result.unit = unit; return result;
}
bool equal(const std::vector<aether_background_position_s>& a,
           const std::vector<aether_background_position_s>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!equal(a[i].x, b[i].x) || !equal(a[i].y, b[i].y)) return false;
    return true;
}
void position(const std::string& source, aether_css_length x, aether_css_length y) {
    std::vector<aether_background_position_s> out;
    const bool parsed = aether_background_position_list_try_parse(source, out);
    check(parsed, "accept " + source);
    check(out.size() == 1, "one position: " + source);
    if (out.size() == 1) {
        check(equal(out[0].x, x), "X unit and coefficients: " + source);
        check(equal(out[0].y, y), "Y unit and coefficients: " + source);
    }
}
const std::vector<aether_background_position_s> seed = {
    {length(13, AETHER_CSS_UNIT_EM), length(29)},
    {length(-7, AETHER_CSS_UNIT_PX), length(61)}
};
void reject(const std::string& source) {
    auto out = seed;
    check(!aether_background_position_list_try_parse(source, out), "reject " + source);
    check(equal(out, seed), "transactional list failure: " + source);
}
}
int main() {
    const char* words[] = {"left", "right", "top", "bottom", "center", "10px", "25%"};
    const aether_css_length values[] = {length(0), length(100), length(0), length(100),
        length(50), length(10, AETHER_CSS_UNIT_PX), length(25)};
    // Independent explicit expectation table: 0=reject, 1=XY, 2=YX.
    // Covers all 25 keyword pairs, 20 keyword/number pairs and four number pairs.
    const int order[7][7] = {
        {0,0,1,1,1,1,1}, {0,0,1,1,1,1,1},
        {2,2,0,0,2,0,0}, {2,2,0,0,2,0,0},
        {2,2,1,1,1,1,1}, {0,0,1,1,1,1,1}, {0,0,1,1,1,1,1}
    };
    for (int a = 0; a != 7; ++a) {
        position(words[a], a == 2 || a == 3 ? length(50) : values[a],
                 a == 2 || a == 3 ? values[a] : length(50));
        for (int b = 0; b != 7; ++b) {
            const std::string source = std::string(words[a]) + " " + words[b];
            if (!order[a][b]) reject(source);
            else position(source, values[order[a][b] == 1 ? a : b], values[order[a][b] == 1 ? b : a]);
        }
    }
    struct Numeric { const char* text; float value; aether_css_unit_t unit; };
    const Numeric numeric[] = {
        {"0",0,AETHER_CSS_UNIT_PX}, {"-10px",-10,AETHER_CSS_UNIT_PX},
        {"-25%",-25,AETHER_CSS_UNIT_PERCENT}, {"+10px",10,AETHER_CSS_UNIT_PX},
        {"1e1px",10,AETHER_CSS_UNIT_PX}, {"2em",2,AETHER_CSS_UNIT_EM},
        {"1rem",1,AETHER_CSS_UNIT_REM}, {"10vw",10,AETHER_CSS_UNIT_VW},
        {"10vh",10,AETHER_CSS_UNIT_VH}
    };
    for (const auto& n : numeric) {
        const auto expected = length(n.value, n.unit);
        aether_css_length native{};
        check(aether_css_length_try_parse(n.text, &native) && equal(native, expected),
              std::string("native numeric control: ") + n.text);
        position(n.text, expected, length(50));
        position(std::string(n.text) + " top", expected, length(0));
        position(std::string("center ") + n.text, length(50), expected);
    }
    auto calc = length(2, AETHER_CSS_UNIT_LINEAR); calc.linear_percent = 25;
    position("calc(25% + 2px)", calc, length(50));
    position("calc(25% + 2px) center", calc, length(50));
    position("left calc(25% + 2px)", length(0), calc);
    position("calc(25% /**/ + 2px) center", calc, length(50));
    for (const char* s : {"CeNtEr LeFt", " center\tleft ", "center/**/left"})
        position(s, length(0), length(50));
    position("7%11px", length(7), length(11, AETHER_CSS_UNIT_PX));
    position("7px/**/11px", length(7, AETHER_CSS_UNIT_PX), length(11, AETHER_CSS_UNIT_PX));
    for (const char* s : {"", " ", "auto", "banana", "10qu", "10/**/px", "left top 1px",
            "right 1px bottom 2px", "center left extra", "inherit left", "center, initial",
            ",center", "center,", "center,,left", "top 10px,center", "center,top 10px,left",
            "center,left,top 10px"}) reject(s);
    auto list = seed;
    check(aether_background_position_list_try_parse("center left, 10px bottom", list), "valid two-entry list");
    check(equal(list, {{length(0),length(50)}, {length(10,AETHER_CSS_UNIT_PX),length(100)}}),
          "list retains order and all units");
    check(aether_background_position_list_try_parse("right", list) &&
          equal(list, {{length(100),length(50)}}), "shorter success replaces rather than appends");

    style_property_registry_s registry;
    builtin_properties_register(&registry); registry.seal();
    const auto* property = registry.try_get("background-position");
    const auto* images = registry.try_get("background-image");
    check(property && property->parse && property->apply_checked && images && images->apply_checked,
          "registered parser and flat applicators");
    if (!property || !property->parse || !property->apply_checked || !images || !images->apply_checked) return 1;
    check(!property->inherited && property->impact == STYLE_IMPACT_PAINT, "position remains non-inherited PAINT only");
    for (const char* source : {"center left", "CeNtEr RiGhT", "calc(25% + 2px) center", "2em 1rem, 10vw 10vh"}) {
        for (size_t padding : {size_t(0), size_t(1), size_t(128), size_t(4096)}) {
            std::string out = std::string(padding, ' ') + source + std::string(padding, '\t');
            check(property->parse(out.c_str(), out), "valid aliased callback");
            check(out == source, "aliased callback preserves trimmed authored text");
            check(property->parse(out.c_str(), out) && out == source, "repeated aliased callback");
        }
        std::string out = "sentinel";
        check(property->parse(source, out) && out == source, "separate callback accepts reordered and relative text");
    }
    for (const char* source : {"", "top 10px", "left right", "center,top 10px", "center,", "auto", "inherit left"}) {
        std::string out = source;
        check(!property->parse(out.c_str(), out), "invalid aliased callback rejects");
        check(out == source, "invalid aliased callback leaves output unchanged");
        out = "sentinel";
        check(!property->parse(source, out) && out == "sentinel", "invalid separate callback leaves output unchanged");
    }
    std::string out = "sentinel";
    check(!property->parse(nullptr, out) && out == "sentinel", "null input callback preserves destination");
    aether_computed_style_t style = aether_computed_style_default();
    check(images->apply_checked(&style, "linear-gradient(red,blue),linear-gradient(black,white),linear-gradient(green,blue)").success,
          "seed three real flat image layers");
    check(property->apply_checked(&style, "center left,10px bottom").success, "flat apply accepts reordered list");
    check(style.background_layers.count == 3, "flat image count retained");
    if (style.background_layers.count == 3) {
        for (int i = 0; i != 3; ++i) {
            const auto& layer = style.background_layers.items[i];
            check(equal(layer.position_x, i == 1 ? length(10,AETHER_CSS_UNIT_PX) : length(0)) &&
                  equal(layer.position_y, i == 1 ? length(100) : length(50)), "flat list uses A,B,A modulo repetition");
        }
        for (const char* invalid : {"top 10px", "center left,top 10px"}) {
            check(!property->apply_checked(&style, invalid).success, "flat apply rejects full invalid list");
            for (int i = 0; i != 3; ++i) {
                const auto& layer = style.background_layers.items[i];
                check(equal(layer.position_x, i == 1 ? length(10,AETHER_CSS_UNIT_PX) : length(0)) &&
                      equal(layer.position_y, i == 1 ? length(100) : length(50)), "failed flat list changes no layer");
            }
        }
        check(property->apply_checked(&style, "left,center,right,bottom").success, "flat accepts more positions than images");
        for (int i = 0; i != 3; ++i)
            check(equal(style.background_layers.items[i].position_x, length(i * 50)) &&
                  equal(style.background_layers.items[i].position_y, length(50)), "flat extra positions do not change first entries");
    }
    aether_computed_style_free(&style);
    std::printf("background-position parser/registry: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
