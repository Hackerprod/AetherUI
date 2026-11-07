// Dedicated serial process: locale mutation must not overlap product work.
// Compare named fields, not struct padding; checks remain enabled in Release.
#include "../src/css/transforms.h"
#include <bit>
#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
unsigned checks = 0, failures = 0;
std::string context;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL [%s]: %s\n", context.c_str(), label.c_str()); }
}
std::string current() { const char* p = std::setlocale(LC_ALL, nullptr); return p ? p : ""; }
struct LocaleGuard {
    std::string saved = current();
    ~LocaleGuard() { check(std::setlocale(LC_ALL, saved.c_str()) != nullptr, "restore locale"); }
};
void select(const std::string& locale) {
    if (!std::setlocale(LC_ALL, locale.c_str())) { std::fprintf(stderr, "HARNESS: cannot select %s\n", locale.c_str()); std::exit(2); }
}
using Bits = std::vector<uint32_t>;
void append(Bits& b, float v) { b.push_back(std::bit_cast<uint32_t>(v)); }
void append(Bits& b, const aether_css_length& v) {
    b.push_back(static_cast<uint32_t>(v.unit));
    for (float f : {v.value, v.linear_percent, v.linear_em, v.linear_rem, v.linear_vw, v.linear_vh}) append(b, f);
}
struct Result { bool valid; Bits bits; };
Result parse(const char* input) {
    const auto before = current();
    aether_transform_list list{};
    errno = ERANGE; // A previous conversion must not poison this parse.
    const bool valid = aether_transform_list_try_parse(input, &list);
    check(before == current(), std::string(input) + " preserves LC_ALL");
    if (!valid) check(list.count == 0 && list.items == nullptr, std::string(input) + " fails atomically");
    Bits bits{static_cast<uint32_t>(list.count)};
    for (int i = 0; i < list.count; ++i) {
        const auto& op = list.items[i];
        bits.push_back(static_cast<uint32_t>(op.kind)); append(bits, op.x); append(bits, op.y);
        for (float f : {op.scalar_x, op.scalar_y, op.angle_radians, op.matrix_a, op.matrix_b,
                        op.matrix_c, op.matrix_d, op.matrix_e, op.matrix_f}) append(bits, f);
    }
    aether_transform_list_free(&list);
    return {valid, bits};
}
struct Case { const char* text; bool valid; };
const Case cases[] = {
    {"none", true}, {"/**/ NoNe /**/", true},
    {"scale(1.5, -.25)", true}, {"scaleX(1.25e1) scaleY(+.5)", true},
    {"rotate(1.5deg) skewX(-2.5grad) skewY(.125turn) rotate(1.25rad)", true},
    {"rotate(-0) scale(-0)", true},
    {"translateX(c\\61 lc(1.5p\\78  + 2.5%))", true},
    {"translateX(c\\61 lc(0))", false},
    {"rotate(1.5d\\65 g) translateX(1.5p\\78)", true},
    {"translateX(calc(1.5p\\78  + 2.5%))", true},
    {"translateX(calc(1.5p\\78 + 2.5%))", false},
    {"translateX(1\\65 2px)", false},
    {"matrix(1.5,.25,-.5,2.5,3.5,-4.5)", true},
    {"translate(1.5px,2.5%) translateX(3.5em) translateY(4.5rem)", true},
    {"translate(calc(1.5px + 2.5%),calc(3.5vw + 4.5vh)) scale(.5) rotate(.25turn)", true},
    {"scale(1.0000000596046448)", true}, {"rotate(1.0000000596046448rad)", true},
    {"translateX(1e-46px)", true}, // Length conversion retains its double policy.
    {"scale(1e-46)", false}, {"scale(1e-40)", false}, {"rotate(1e-40rad)", false},
    {"scale(1e100)", false}, {"rotate(3e38turn)", false},
    {"scale(nan)", false}, {"scale(inf)", false}, {"scale(0x1p0)", false},
    {"scale(1,5,2)", false}, {"rotate(1,5deg)", false}, {"translateX(1,5px)", false},
    {"scale(1/**/.5)", false}, {"rotate(1.5 deg)", false},
    {"scale(1.5) rotate(nan)", false}, {"none scale(1)", false}, {"", false}
};
void rounding() {
    for (const char* source : {"scale(1.0000000596046448)", "rotate(1.0000000596046448rad)",
                               "matrix(1.0000000596046448,0,0,1,0,0)"}) {
        aether_transform_list list{};
        const bool valid = aether_transform_list_try_parse(source, &list);
        check(valid && list.count == 1, "rounding case parses");
        if (valid && list.count == 1) {
            const auto& op = list.items[0];
            const float value = op.kind == AETHER_TRANSFORM_SCALE ? op.scalar_x :
                                op.kind == AETHER_TRANSFORM_ROTATE ? op.angle_radians : op.matrix_a;
            check(std::bit_cast<uint32_t>(value) == 0x3f800001u, "direct float conversion avoids double rounding");
        }
        aether_transform_list_free(&list);
    }
}
}
int main(int argc, char** argv) {
    const bool require_all = argc == 2 && std::string(argv[1]) == "--require-all";
    if (argc > 2 || (argc == 2 && !require_all)) { std::fprintf(stderr, "usage: test_transform_locale [--require-all]\n"); return 2; }
    const auto original = current();
    {
        LocaleGuard restore;
        std::vector<std::string> locales{"C"};
        const char* families[][4] = {
            {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "German_Germany.1252"},
            {"fr_FR.UTF-8", "fr_FR.utf8", "fr_FR", "French_France.1252"},
            {"tr_TR.UTF-8", "tr_TR.utf8", "tr_TR", "Turkish_Turkey.1254"}
        };
        for (const auto& family : families) {
            bool found = false;
            for (const char* name : family) if (std::setlocale(LC_ALL, name)) {
                check(std::string(std::localeconv()->decimal_point) == ",", "alternate locale has decimal comma");
                locales.emplace_back(name); found = true;
                std::printf("AVAILABLE %s decimal_point=%s\n", name, std::localeconv()->decimal_point); break;
            }
            if (!found) std::printf("SKIP unavailable locale family %s\n", family[0]);
            if (require_all) check(found, std::string("required locale ") + family[0]);
        }
        // First product conversion happens in the alternate locale, not C.
        select(locales.back()); context = "first conversion";
        aether_transform_list first{};
        const auto before = current();
        check(aether_transform_list_try_parse("scale(1.5)", &first), "first numeric conversion parses");
        check(first.count == 1 && first.items && first.items[0].scalar_x == 1.5f, "first conversion uses decimal dot");
        check(before == current(), "first conversion preserves locale");
        aether_transform_list_free(&first);
        select("C"); context = "C baseline";
        std::vector<Result> baseline;
        for (const auto& c : cases) {
            baseline.push_back(parse(c.text)); check(baseline.back().valid == c.valid, c.text);
        }
        for (const auto& locale : locales) {
            select(locale); context = locale;
            for (size_t i = 0; i < std::size(cases); ++i) {
                const auto result = parse(cases[i].text);
                check(result.valid == baseline[i].valid && result.bits == baseline[i].bits,
                      std::string(cases[i].text) + " validity and all field bits match C");
            }
            rounding();
        }
        std::printf("transform_locale: %zu locales\n", locales.size());
    }
    check(original == current(), "original LC_ALL restored");
    std::printf("transform_locale: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
