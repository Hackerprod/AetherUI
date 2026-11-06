#include "../src/css/linear_gradient.h"
#include "../src/css/background_layers.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int checks = 0, failures = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}
bool same(float a, float b) { return std::fabs(a - b) < .00001f; }
bool color_equal(const aether_color_rgba& a, const aether_color_rgba& b) {
    return same(a.r, b.r) && same(a.g, b.g) && same(a.b, b.b) && same(a.a, b.a);
}
bool length_equal(const aether_css_length& a, const aether_css_length& b) {
    return a.unit == b.unit && a.value == b.value && a.linear_percent == b.linear_percent &&
        a.linear_em == b.linear_em && a.linear_rem == b.linear_rem &&
        a.linear_vw == b.linear_vw && a.linear_vh == b.linear_vh;
}
bool gradient_equal(const aether_linear_gradient& a, const aether_linear_gradient& b) {
    if (!same(a.direction_x, b.direction_x) || !same(a.direction_y, b.direction_y) || a.stop_count != b.stop_count) return false;
    for (int i = 0; i < a.stop_count; ++i) {
        const auto& x = a.stops[i]; const auto& y = b.stops[i];
        if (!color_equal(x.color, y.color) || !length_equal(x.position, y.position) ||
            x.has_position != y.has_position || x.offset != y.offset) return false;
    }
    return true;
}
bool layer_equal(const aether_background_layer& a, const aether_background_layer& b) {
    return a.kind == b.kind && ((a.image_source == nullptr && b.image_source == nullptr) ||
        (a.image_source && b.image_source && std::strcmp(a.image_source, b.image_source) == 0)) &&
        gradient_equal(a.gradient, b.gradient) && length_equal(a.position_x, b.position_x) &&
        length_equal(a.position_y, b.position_y) && a.size_kind == b.size_kind &&
        length_equal(a.size_x, b.size_x) && length_equal(a.size_y, b.size_y) &&
        a.repeat_x == b.repeat_x && a.repeat_y == b.repeat_y;
}
void accepted(const std::string& source, const char* canonical) {
    aether_linear_gradient actual{}, expected{};
    aether_linear_gradient_parse_info_s info{}, expected_info{};
    bool ok = aether_linear_gradient_try_parse(source.c_str(), &actual, &info);
    check(ok, "accept " + source);
    check(aether_linear_gradient_try_parse(canonical, &expected, &expected_info), "canonical valid");
    if (ok) check(gradient_equal(actual, expected) && info.uses_fixed_corner_direction == expected_info.uses_fixed_corner_direction,
                  "all authored gradient fields and parse info " + source);
    aether_linear_gradient_free(&actual); aether_linear_gradient_free(&expected);
    aether_linear_gradient_free(&actual);
    check(actual.stops == nullptr && actual.stop_count == 0, "repeat gradient free");
}
void rejected(const std::string& source) {
    aether_linear_gradient actual{};
    aether_linear_gradient_parse_info_s info{true};
    bool ok = aether_linear_gradient_try_parse(source.c_str(), &actual, &info);
    check(!ok, "reject " + source);
    if (!ok) check(actual.stops == nullptr && actual.stop_count == 0 && actual.direction_x == 0 && actual.direction_y == 0 &&
                   !info.uses_fixed_corner_direction, "failure leaves no stops, direction or parse info");
    aether_linear_gradient_free(&actual);
}
void direct() {
    const char* plain = "linear-gradient(red,blue)";
    accepted(plain, plain);
    accepted("LiNeAr-GrAdIeNt(red,blue)", plain);
    accepted("/*(*/linear-gradient(red,blue)/*)*/", plain);
    accepted("/**//**/linear-gradient(red,blue)/**/ /* , ( ] ) */", plain);
    accepted("linear-gradient(red,blue)/*", plain);
    accepted("linear-gradient(red,blue)/* ) ] ( , EOF", plain);
    accepted("/*)*/linear-gradient(red,blue) /**/ /*EOF", plain);
    for (char space : {' ', '\t', '\r', '\n', '\f'})
        accepted(std::string(1, space) + plain + space, plain);
    for (const char* body : {
        "linear-gradient(to right,red 10px,blue 90%)",
        "linear-gradient(to bottom left,red 70%,green 20%,blue)",
        "linear-gradient(90deg,red 20%,green,blue 80%)",
        "linear-gradient(rgb(1,2,3),rgba(4,5,6,.5))",
        "linear-gradient(red calc(10% + 1px),blue)",
        "linear-gradient(red 1em,green 1rem,blue 20vw,black 40vh)",
        "linear-gradient(to/**/right,red/**/7%,blue 100%)"})
        accepted(std::string("/*(*/\f") + body + " /*),]*/", body);
    for (const char* suffix : {"junk", " junk", " none", "42", "0%", "1px", "rgb(1,2,3)",
        "/**/none", "/**/junk/*", "/*)*/ junk", ")", "[]", "{}", "()", "\"tail\"", ";", "!important", "/", ".",
        ",linear-gradient(red,blue)", "\v", "\xc2\xa0", "\"unterminated", "url(unterminated", "'bad\nstring"})
        rejected(std::string(plain) + suffix);
    for (const char* value : {"", "/*", "/**/", "linear-gradient (red,blue)", "linear-gradient/**/(red,blue)",
        "linear-/**/gradient(red,blue)", "linear-gradientx(red,blue)", "xlinear-gradient(red,blue)",
        "repeating-linear-gradient(red,blue)", "-webkit-linear-gradient(red,blue)", "linear\\-gradient(red,blue)",
        "\\6c inear-gradient(red,blue)", "\vlinear-gradient(red,blue)", "\xc2\xa0linear-gradient(red,blue)",
        "linear-gradient(red,blue", "linear-gradient(red,blue]", "linear-gradient(red,blue/*)",
        "linear-gradient(red)", "linear-gradient(to sideways,red,blue)", "linear-gradient(to right left,red,blue)",
        "linear-gradient(red 10/**/px,blue)", "linear-gradient(red 10% 20%,blue)", "linear-gradient(red auto,blue)",
        "linear-gradient(red,blue,'bad\nstring')", "linear-gradient(red,blue,url(unterminated)",
        "linear-gradient(red,blue,[)])"}) rejected(value);
    aether_linear_gradient gradient{};
    check(!aether_linear_gradient_try_parse(nullptr, &gradient), "null source rejected");
    check(!aether_linear_gradient_try_parse(plain, nullptr), "null output rejected");

    check(aether_linear_gradient_try_parse("/*(*/linear-gradient(to right,red 10px,green calc(50% + 1em),blue 60vw)/*)*/", &gradient),
          "symbolic resolution parse");
    aether_linear_gradient used{};
    aether_linear_gradient_resolution_context_s context{{0,0,200,100},10,20,200,300};
    if (gradient.stops) {
        check(aether_linear_gradient_resolve(gradient, context, &used), "symbolic resolution");
        if (used.stops) check(same(used.stops[0].offset,.05f) && same(used.stops[1].offset,.55f) && same(used.stops[2].offset,.6f),
                              "length calc and viewport offsets remain unchanged");
        check(gradient.stops[1].position.unit == AETHER_CSS_UNIT_LINEAR && gradient.stops[1].offset == 0,
              "resolution leaves authored positions immutable");
    }
    aether_linear_gradient_free(&gradient); aether_linear_gradient_free(&used);
}
void lists() {
    const char* seed = "url('sentinel'),linear-gradient(to right,red 10px,blue 90%)";
    for (const char* bad : {
        "linear-gradient(red,blue) junk,url('new'),linear-gradient(red,blue)",
        "url('new'),linear-gradient(red,blue) junk,linear-gradient(red,blue)",
        "url('new'),linear-gradient(red,blue),linear-gradient(red,blue) junk",
        "linear-gradient (red,blue),url('new')", "url('new'),linear-gradient/**/(red,blue)",
        ",linear-gradient(red,blue)", "linear-gradient(red,blue),", "url('new'),,linear-gradient(red,blue)",
        "none,linear-gradient(red,blue)", "linear-gradient(red,blue)/*"}) {
        aether_background_layer_list list{};
        check(aether_background_image_list_try_parse(seed, &list), "seed owned list");
        if (!list.items || list.count != 2) continue;
        const auto* items = list.items; const auto* url = list.items[0].image_source;
        const auto* stops = list.items[1].gradient.stops;
        auto first = list.items[0], second = list.items[1];
        std::vector<aether_gradient_stop> stop_values(stops, stops + 2);
        char sentinel[] = "sentinel";
        first.image_source = sentinel;
        second.gradient.stops = stop_values.data();
        check(!aether_background_image_list_try_parse(bad, &list), std::string("atomic reject ") + bad);
        check(list.items == items && list.count == 2 && list.items[0].image_source == url && list.items[1].gradient.stops == stops,
              "list failure retains all allocation identities");
        if (list.items == items && list.count == 2 && list.items[0].image_source == url && list.items[1].gradient.stops == stops) {
            check(layer_equal(first, list.items[0]) && layer_equal(second, list.items[1]),
                  "list failure retains every layer and stop field");
        }
        aether_background_layer_list_free(&list); aether_background_layer_list_free(&list);
        check(list.count == 0 && list.items == nullptr, "repeat list free");
    }
    for (const char* good : {"/*(*/linear-gradient(red,blue)/*)*/", "\flinear-gradient(red,blue)\f",
        "linear-gradient(red,blue),linear-gradient(to right,red 1em,blue 90%)",
        "url('new'),/*(*/linear-gradient(red,blue)/*)*/"}) {
        aether_background_layer_list list{}, expected{};
        check(aether_background_image_list_try_parse(seed, &list), "seed replacement list");
        check(aether_background_image_list_try_parse(good, &list), std::string("replace with ") + good);
        check(aether_background_image_list_try_parse(good, &expected), "fresh replacement comparison");
        check(list.count == expected.count, "replacement count");
        if (list.count == expected.count) for (int i = 0; i < list.count; ++i) {
            const auto& a = list.items[i]; const auto& b = expected.items[i];
            check(a.kind == b.kind && length_equal(a.position_x,b.position_x) && length_equal(a.position_y,b.position_y) &&
                a.size_kind == b.size_kind && length_equal(a.size_x,b.size_x) && length_equal(a.size_y,b.size_y) &&
                a.repeat_x == b.repeat_x && a.repeat_y == b.repeat_y, "replacement layer metadata");
            if (a.kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT && b.kind == a.kind)
                check(gradient_equal(a.gradient,b.gradient) && a.gradient.stops != b.gradient.stops, "independent gradient ownership");
            if (a.kind == AETHER_BACKGROUND_LAYER_IMAGE_URL && b.kind == a.kind)
                check(std::strcmp(a.image_source,b.image_source) == 0 && a.image_source != b.image_source, "independent URL ownership");
        }
        aether_background_layer_list_free(&list); aether_background_layer_list_free(&expected);
    }
    // EOF-comment recovery is deliberately private to the single-function parser;
    // the unchanged list splitter still rejects it before image dispatch.
    aether_background_layer_list list{};
    check(!aether_background_image_list_try_parse("linear-gradient(red,blue)/*", &list), "existing list EOF-comment limitation");
    aether_background_shorthand_s shorthand{};
    check(aether_background_shorthand_try_parse("linear-gradient(red,blue) red", shorthand) && shorthand.has_color && shorthand.color == "red",
          "shorthand permits an image followed by a color");
    check(aether_background_shorthand_try_parse("linear-gradient(red,blue) left top / 20px auto repeat-x red", shorthand),
          "shorthand exterior position size repeat and color remain valid");
    check(shorthand.position == "left top" && shorthand.size == "20px auto" && shorthand.repeat == "repeat-x" && shorthand.color == "red",
          "shorthand preserves every exterior component");
    for (int i = 0; i < 128; ++i) {
        aether_linear_gradient gradient{};
        check(aether_linear_gradient_try_parse("/*(*/linear-gradient(red,blue)/*)*/", &gradient), "repeated valid parse");
        aether_linear_gradient_free(&gradient);
        check(!aether_linear_gradient_try_parse("linear-gradient(red,blue)junk", &gradient), "repeated invalid parse");
        aether_linear_gradient_free(&gradient);
        check(aether_background_image_list_try_parse(seed, &list), "repeated list parse");
        check(!aether_background_image_list_try_parse("url('new'),linear-gradient(red,blue),linear-gradient(red,blue) junk", &list),
              "repeated mixed-allocation rollback");
        aether_background_layer_list_free(&list);
    }
}
} // namespace
int main() {
    direct(); lists();
    std::printf("Gradient boundaries: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
