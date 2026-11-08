#include "../src/style_runtime/style_property_registry_internal.h"
#include <cfenv>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

void builtin_properties_register(style_property_registry_s*);
int run_flex_number_locale_tests();
namespace {
int checks = 0, cases = 0, failures = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL flex registry: %s\n", label.c_str()); }
}
bool equal(float actual, float expected) {
    return std::isfinite(actual) && actual == expected && (expected != 0 || !std::signbit(actual));
}
using Member = float aether_computed_style_t::*;
struct Case { std::string text; float expected; };
void valid_case(const style_property_definition_s& p, Member member, const Case& c) {
    ++cases;
    const auto label = p.name + ": " + c.text;
    std::string canonical = "sentinel";
    check(p.parse(c.text.c_str(), canonical), label + " parse");
    aether_computed_style_t style{};
    style.flex_grow = 17; style.flex_shrink = 19; style.opacity = .25f;
    style.flex_basis = {AETHER_FLEX_BASIS_LENGTH, {23, AETHER_CSS_UNIT_PX}};
    aether_computed_style_t expected; std::memcpy(&expected, &style, sizeof(style));
    expected.*member = c.expected;
    check(p.apply_checked(&style, c.text).success && equal(style.*member, c.expected), label + " raw apply");
    check(std::memcmp(&style, &expected, sizeof(style)) == 0, label + " only selected field written");
    check(p.apply_checked(&style, canonical).success && equal(style.*member, c.expected), label + " canonical apply");
    std::string again;
    check(p.parse(canonical.c_str(), again) && again == canonical, label + " canonical idempotent");
    if (c.expected == 0) check(canonical == "0", label + " positive canonical zero");
    for (int padding : {0, 1, 128, 4096}) {
        std::string alias = std::string(padding, ' ') + c.text + std::string(padding, '\t');
        check(p.parse(alias.c_str(), alias) && alias == canonical, label + " whole alias");
        check(p.parse(alias.c_str(), alias) && alias == canonical, label + " repeated alias");
        alias = "prefix:" + std::string(padding, ' ') + c.text + std::string(padding, '\t');
        check(p.parse(alias.c_str() + 7, alias) && alias == canonical, label + " interior alias");
    }
}
void invalid_case(const style_property_definition_s& p, const std::string& text) {
    ++cases;
    const auto label = p.name + ": " + text;
    std::string out = "sentinel";
    check(!p.parse(text.c_str(), out) && out == "sentinel", label + " parse atomic rejection");
    for (int padding : {0, 1, 128, 4096}) {
        std::string alias = std::string(padding, ' ') + text + std::string(padding, '\t');
        const auto saved = alias;
        check(!p.parse(alias.c_str(), alias) && alias == saved, label + " whole alias failure");
        alias = "prefix:" + saved;
        const auto interior_saved = alias;
        check(!p.parse(alias.c_str() + 7, alias) && alias == interior_saved, label + " interior alias failure");
    }
    aether_computed_style_t style{};
    style.flex_grow = 17; style.flex_shrink = 19; style.opacity = .25f;
    style.flex_basis = {AETHER_FLEX_BASIS_LENGTH, {23, AETHER_CSS_UNIT_PX}};
    unsigned char saved[sizeof(style)]; std::memcpy(saved, &style, sizeof(style));
    const auto result = p.apply_checked(&style, text);
    check(!result.success && result.diagnostic_code &&
          std::strcmp(result.diagnostic_code, "AETHER-CSS-FLEX-NUMBER") == 0 &&
          result.diagnostic_message && *result.diagnostic_message, label + " checked diagnostic");
    check(std::memcmp(saved, &style, sizeof(style)) == 0, label + " whole destination intact");
}
}
int main() {
    // These independent decimal/hexadecimal expected values target the supported
    // IEEE binary32 platform; fail explicitly rather than silently skipping them.
    check(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::radix == 2 &&
          std::numeric_limits<float>::digits == 24 && std::numeric_limits<float>::max_exponent == 128 &&
          std::numeric_limits<float>::has_denorm == std::denorm_present &&
          std::numeric_limits<float>::denorm_min() == 0x1p-149f && std::fegetround() == FE_TONEAREST,
          "binary32, subnormal and round-to-nearest oracle prerequisites");
    if (failures) return 1;
    style_property_registry_s registry;
    builtin_properties_register(&registry); registry.seal();
    const auto* grow = registry.try_get("flex-grow");
    const auto* shrink = registry.try_get("flex-shrink");
    const auto* before = registry.try_get("flex-wrap");
    const auto* after = registry.try_get("flex-basis");
    check(grow && shrink && before && after, "registry neighbors exist");
    if (!grow || !shrink || !before || !after) return 1;
    check(grow->id == before->id + 1 && shrink->id == grow->id + 1 && after->id == shrink->id + 1,
          "unchanged registration IDs and order");
    const float maximum = std::numeric_limits<float>::max();
    std::vector<Case> valid = {
        {"0", 0}, {"+0", 0}, {"-0", 0}, {"-0e10", 0}, {"-0.000e-99", 0},
        {"003", 3}, {".5", .5f}, {"+.5", .5f}, {"3.25", 3.25f}, {"1e2", 100},
        {"1E+2", 100}, {"125e-2", 1.25f}, {"1e-2", .01f}, {"2/**/", 2},
        {"/**/2/**/", 2}, {" \t\r\n\f.5\f\n\r\t ", .5f},
        {"340282346638528859811704183484516925440", maximum},
        {"340282346638528859811704183484516925441", maximum},
        {"1e39", maximum}, {"1e40", maximum}, {"1e400", maximum}, {"1e-400", 0},
        {"1e-40", 1e-40f}, {"1.401298464324817070923729583289916131280e-45", 0x1p-149f},
        {"1.17549435082228750796873653722224568e-38", 0x1p-126f},
        {"16777216", 16777216.0f}, {"16777217", 16777216.0f}, {"16777218", 16777218.0f},
        {"1.0000000596046448", 0x1.000002p0f},
        {"1" + std::string(100, '0') + "e-100", 1},
        {"0." + std::string(100, '0') + "1e101", 1},
        {"1e" + std::string(512, '9'), maximum}, {"1e-" + std::string(512, '9'), 0},
        {"-0.0e" + std::string(512, '9'), 0}, {"-0e-" + std::string(512, '9'), 0}
    };
    std::vector<std::string> invalid = {
        "", " ", "/**/", "/*", "1/*", "\v1", "1\v", "\xc2\xa0" "1", "1\xc2\xa0",
        "+", "-", ".", "+.", "1.", "1e", "1e+", "1e-", "1E", "1E+", "1E-",
        "0x1p1", "0X1P+1", "0x1p10000", "nan", "NaN", "NAN", "nan(payload)",
        "inf", "INF", "Infinity", "+inf", "-inf", "+nan", "-NaN", "+Infinity", "-Infinity",
        "-1", "-.5", "-1e-50", "-1e-400", "-1e400", "1%", "%", "1px", "1 2",
        "1,2", "1/2", "1)", "1]", "1}", "1/**/e2", "+/**/.5", "1/**/.0",
        "0,5", "calc(1)", "calc(infinity)", "calc(NaN)", "min(1,2)", "max(1,2)",
        "clamp(0,1,2)", "initial", "inherit", "unset", "revert",
        "-1e-" + std::string(512, '9'), "-1" + std::string(100, '0') + "e-100",
        "-0." + std::string(100, '0') + "1e101"
    };
    for (int index = 0; index < 2; ++index) {
        const auto& p = index == 0 ? *grow : *shrink;
        const Member member = index == 0 ? &aether_computed_style_t::flex_grow : &aether_computed_style_t::flex_shrink;
        check(p.name == (index == 0 ? "flex-grow" : "flex-shrink") &&
              p.initial_value == (index == 0 ? "0" : "1") && !p.inherited && p.impact == STYLE_IMPACT_LAYOUT,
              "unchanged factor metadata");
        check(p.parse && p.apply_checked && !p.apply && !p.css_wide && !p.inherit_explicit &&
              !p.inherit_context && !p.parse_failure && !p.apply_with_context && !p.apply_with_context_checked,
              "checked scalar callback only, no CSS-wide hooks");
        if (!p.parse || !p.apply_checked) return 1;
        for (const auto& c : valid) valid_case(p, member, c);
        for (const auto& text : invalid) invalid_case(p, text);
        std::string out = "sentinel";
        check(!p.parse(nullptr, out) && out == "sentinel", "null source leaves output intact");
    }
    const auto* shorthand = registry.try_get_shorthand("flex");
    check(shorthand && shorthand->expand, "shorthand registered");
    if (shorthand && shorthand->expand) {
        check(shorthand->target_names == std::vector<std::string>{"flex-grow", "flex-shrink", "flex-basis"},
              "exact three shorthand targets");
        for (const auto& text : {"+.5 1E+2 18px", "-0e10 2 0%", "1e400 1e400 auto"}) {
            ++cases;
            style_shorthand_writer_s writer;
            check(shorthand->expand(text, writer) && writer.items.size() == 3, "shorthand complete expansion");
            if (writer.items.size() != 3) continue;
            check(writer.items[0].name == "flex-grow" && writer.items[1].name == "flex-shrink" &&
                  writer.items[2].name == "flex-basis", "shorthand destination order");
            const auto parts = std::string(text);
            const float eg = parts[0] == '+' ? .5f : parts[0] == '-' ? 0 : maximum;
            const float es = parts[0] == '+' ? 100 : parts[0] == '-' ? 2 : maximum;
            for (int i = 0; i < 2; ++i) {
                const auto* p = i ? shrink : grow;
                std::string canonical;
                check(p->parse(writer.items[i].value.c_str(), canonical) && canonical == writer.items[i].value,
                      "shorthand factors already canonical");
                aether_computed_style_t style{};
                check(p->apply_checked(&style, canonical).success && equal(i ? style.flex_shrink : style.flex_grow, i ? es : eg),
                      "shorthand canonical numeric expected");
            }
        }
        for (const char* text : {"nan 1 auto", "1 nan auto", "inf 1 auto", "1 inf auto", "0x1p1 1 auto",
                 "1 0x1p1 auto", "-1e-400 1 auto", "1 -1e-400 auto", "1 2 nonsense", "1 2 auto 4",
                 "1 2 calc(10px + 2px", "1 2 3px)", "calc(1) 2 auto"}) {
            ++cases;
            style_shorthand_writer_s writer; writer.set("sentinel", "unchanged");
            check(!shorthand->expand(text, writer) && writer.items.size() == 1 &&
                  writer.items[0].name == "sentinel" && writer.items[0].value == "unchanged", "invalid shorthand writes no sibling");
        }
    }
    // Only test code changes the process locale, always restoring its prior value.
    const char* current = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved_locale = current ? current : "C";
    check(std::setlocale(LC_NUMERIC, "C") != nullptr, "set test C locale");
    valid_case(*grow, &aether_computed_style_t::flex_grow, {"1.5", 1.5f});
    bool comma = false;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8", "de_DE", "fr_FR"}) {
        if (!std::setlocale(LC_NUMERIC, name)) continue;
        if (std::strcmp(std::localeconv()->decimal_point, ",") != 0) continue;
        comma = true;
        for (int i = 0; i < 2; ++i) {
            valid_case(i ? *shrink : *grow, i ? &aether_computed_style_t::flex_shrink : &aether_computed_style_t::flex_grow,
                       {"1.5", 1.5f});
            invalid_case(i ? *shrink : *grow, "1,5");
        }
        break;
    }
    if (!comma) std::puts("SKIP flex registry comma locale: no requested locale with comma decimal is installed");
    check(std::setlocale(LC_NUMERIC, saved_locale.c_str()) != nullptr, "restore test locale");
    const int locale_failure = run_flex_number_locale_tests();
    std::printf("flex registry: %d cases, %d checks, %d failures\n", cases, checks, failures);
    return failures || locale_failure ? 1 : 0;
}
