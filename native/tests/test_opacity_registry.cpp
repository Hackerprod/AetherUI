#include "../src/style_runtime/style_property_registry_internal.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#if defined(AETHER_OPACITY_LOCALE_WRAPPERS)
#include <cerrno>
#include <locale.h>
#include <set>
#endif
void builtin_properties_register(style_property_registry_s*);
namespace {
int checks = 0, failures = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL opacity registry: %s\n", label.c_str()); }
}
bool equal(float actual, float expected) {
    return std::isfinite(actual) && actual == expected && (expected != 0 || !std::signbit(actual));
}
#if defined(AETHER_OPACITY_LOCALE_WRAPPERS)
bool monitor = false, fail_locale = false, force_range = false, force_infinite = false, force_partial = false;
int acquisitions = 0, releases = 0, conversions = 0, global_calls = 0;
std::set<locale_t> owned;
#endif
}
#if defined(AETHER_OPACITY_LOCALE_WRAPPERS)
extern "C" locale_t __real_newlocale(int, const char*, locale_t);
extern "C" void __real_freelocale(locale_t);
extern "C" double __real_strtod_l(const char*, char**, locale_t);
extern "C" double __real_strtod(const char*, char**);
extern "C" float __real_strtof(const char*, char**);
extern "C" locale_t __wrap_newlocale(int mask, const char* name, locale_t base) {
    if (!monitor) return __real_newlocale(mask, name, base);
    ++acquisitions;
    check(mask == LC_NUMERIC_MASK && std::strcmp(name, "C") == 0 && !base, "private locale identity");
    if (fail_locale) { errno = ENOMEM; return nullptr; }
    locale_t result = __real_newlocale(mask, name, base);
    check(result && owned.insert(result).second, "acquire owned locale");
    errno = ERANGE;
    return result;
}
extern "C" void __wrap_freelocale(locale_t locale) {
    if (!monitor) { __real_freelocale(locale); return; }
    ++releases;
    if (locale && owned.erase(locale) == 1) __real_freelocale(locale);
    else check(false, "release exactly one owned locale");
}
extern "C" double __wrap_strtod_l(const char* text, char** end, locale_t locale) {
    if (!monitor) return __real_strtod_l(text, end, locale);
    ++conversions;
    check(errno == 0 && locale && owned.count(locale), "conversion resets errno and uses owned locale");
    double value = __real_strtod_l(text, end, locale);
    if (force_range) errno = ERANGE;
    if (force_infinite) value = std::numeric_limits<double>::infinity();
    if (force_partial) *end = const_cast<char*>(text) + 1;
    return value;
}
extern "C" double __wrap_strtod(const char* text, char** end) {
    if (monitor) ++global_calls;
    return __real_strtod(text, end);
}
extern "C" float __wrap_strtof(const char* text, char** end) {
    if (monitor) ++global_calls;
    return __real_strtof(text, end);
}
#endif
int run_opacity_registry_tests() {
    style_property_registry_s registry;
    builtin_properties_register(&registry); registry.seal();
    const auto* p = registry.try_get("opacity");
    const auto* before = registry.try_get("background-repeat");
    const auto* after = registry.try_get("-webkit-background-clip");
    check(p && before && after, "registration exists");
    if (!p || !before || !after) return 1;
    check(p->id == before->id + 1 && after->id == p->id + 1, "unchanged registration order and ID");
    check(p->name == "opacity" && p->initial_value == "1" && !p->inherited &&
          p->impact == (STYLE_IMPACT_COMPOSITE | STYLE_IMPACT_HITTEST), "unchanged metadata");
    check(p->parse && p->apply_checked && !p->apply && p->css_wide && p->inherit_explicit &&
          !p->inherit_context && !p->parse_failure && !p->apply_with_context &&
          !p->apply_with_context_checked, "checked scalar apply with explicit inheritance and CSS-wide hooks");
    if (!p->parse || !p->apply_checked || !p->css_wide || !p->inherit_explicit) return 1;
    for (const char* keyword : {"inherit", "INHERIT", "/**/\\69 NHeRiT/**/"})
        check(p->css_wide(keyword) == property_css_wide_e::inherit, "whole decoded inherit keyword");
    check(p->css_wide("initial") == property_css_wide_e::initial &&
          p->css_wide("revert") == property_css_wide_e::initial &&
          p->css_wide("unset") == property_css_wide_e::unset, "existing CSS-wide compatibility");
    for (const char* keyword : {"revert-layer", "in/**/herit", "inherit initial", "inherit /*", ".5"})
        check(p->css_wide(keyword) == property_css_wide_e::none, "reject non-keyword token sequences");
    for (float value : {0.f, .3f, 1.f, 1e-7f, 1e-40f}) {
        aether::style::StyleScalars scalar{}; scalar.opacity = value;
        using namespace aether::style;
        auto payload = std::make_shared<const FrozenStylePayload>(
            ImmutableArray<FrozenBackgroundLayer>{}, ImmutableArray<aether_backdrop_filter>{},
            FrozenGradient{}, ImmutableArray<aether_transform_operation>{},
            ImmutableArray<aether_shadow>{}, ImmutableArray<aether_shadow>{});
        FrozenStyle parent(scalar, std::move(payload));
        aether_computed_style_t style{}; style.opacity = .123f; style.flex_grow = 17;
        unsigned char saved[sizeof(style)]; std::memcpy(saved, &style, sizeof(style));
#if defined(AETHER_OPACITY_LOCALE_WRAPPERS)
        const int acq = acquisitions, conv = conversions;
        monitor = true; fail_locale = true;
#endif
        p->inherit_explicit(&style, parent.view());
        check(equal(style.opacity, value), "inherit exact computed float");
#if defined(AETHER_OPACITY_LOCALE_WRAPPERS)
        monitor = false; fail_locale = false;
        check(acquisitions == acq && conversions == conv, "inherit never reparses or acquires locale");
#endif
        std::memcpy(&style.opacity, saved + offsetof(aether_computed_style_t, opacity), sizeof(style.opacity));
        check(std::memcmp(saved, &style, sizeof(style)) == 0, "inherit changes only opacity bytes");
    }
    struct Case { const char* text; float expected; };
    const Case valid[] = {
        {".5", .5f}, {"+0.5", .5f}, {"00.50", .5f}, {"5e-1", .5f}, {"5E+1%", .5f},
        {"50%", .5f}, {"2", 1}, {"150%", 1}, {"1e100", 1}, {"1e100%", 1},
        {"-.5", 0}, {"-20%", 0}, {"-1e100", 0}, {"-0", 0}, {"-0%", 0},
        {"1e-7", 1e-7f}, {"1e-5%", 1e-7f}, {"1e-40", 1e-40f}, {"1e-400", 0},
        {"-1e-400", 0}, {"/**/50%/**/", .5f}, {" \t\r\n\f.5\f\n\r\t ", .5f},
        {"00000000000000000000000000000000000000000000000050%", .5f}
    };
    for (const auto& c : valid) {
        std::string normalized = "sentinel";
        check(p->parse(c.text, normalized), std::string("parse ") + c.text);
        aether_computed_style_t style{};
        style.opacity = .123f; style.flex_grow = 17;
        check(p->apply_checked(&style, c.text).success && equal(style.opacity, c.expected) &&
              style.flex_grow == 17, std::string("direct apply ") + c.text);
        check(p->apply_checked(&style, normalized).success && equal(style.opacity, c.expected), "normalized apply");
        for (int padding : {0, 1, 128, 4096}) {
            std::string alias = std::string(padding, ' ') + c.text + std::string(padding, '\t');
            check(p->parse(alias.c_str(), alias) && alias == normalized, "same-buffer alias");
            check(p->parse(alias.c_str(), alias) && alias == normalized, "stable normalization");
            alias = "prefix:" + std::string(padding, ' ') + c.text + std::string(padding, '\t');
            check(p->parse(alias.c_str() + 7, alias) && alias == normalized, "interior-pointer alias");
        }
    }
    for (const char* text : {"", " ", "/**/", "/*", ".5/*", "\v.5", "\xc2\xa0.5", "bogus", "nan", "NaN",
            "nan(1)", "inf", "Infinity", "-inf", "0x1p-1", ".", "+", "1.", "1e", "1e+", "1e-",
            "1px", "50 %", "50/**/%", "1/**/e-1", "+/**/.5", "1 0", ".5,.6", ".5/1", ".5)",
            "calc(.5)", "1e400", "-1e400", "1e400%", "0,5", "initial", "inherit", "unset", "revert"}) {
        std::string out = "sentinel";
        check(!p->parse(text, out) && out == "sentinel", std::string("reject ") + text);
        std::string alias = text;
        check(!p->parse(alias.c_str(), alias) && alias == text, "invalid same-buffer unchanged");
        alias = std::string("prefix:") + text;
        const auto saved_alias = alias;
        check(!p->parse(alias.c_str() + 7, alias) && alias == saved_alias, "invalid interior unchanged");
        aether_computed_style_t style{};
        style.opacity = .123f; style.flex_grow = 17;
        unsigned char saved[sizeof(style)]; std::memcpy(saved, &style, sizeof(style));
        const auto result = p->apply_checked(&style, text);
        check(!result.success && result.diagnostic_code && std::strcmp(result.diagnostic_code, "AETHER-CSS-OPACITY") == 0,
              "checked diagnostic");
        check(std::memcmp(saved, &style, sizeof(style)) == 0, "entire destination unchanged");
    }
    std::string out = "sentinel";
    check(!p->parse(nullptr, out) && out == "sentinel", "null rejected atomically");
#if defined(AETHER_OPACITY_LOCALE_WRAPPERS)
    monitor = true;
    for (int mode = 0; mode < 5; ++mode) {
        fail_locale = mode == 0; force_range = mode == 1; force_infinite = mode == 2; force_partial = mode == 3;
        const bool expected = mode == 1 || mode == 4;
        int acq = acquisitions, rel = releases, conv = conversions;
        out = "sentinel";
        check(p->parse("50%", out) == expected && (expected || out == "sentinel"), "injected parser outcome");
        check(acquisitions-acq == 1 && releases-rel == (fail_locale ? 0 : 1) &&
              conversions-conv == (fail_locale ? 0 : 1), "parser exact acquisition/release/conversion");
        aether_computed_style_t style{}; style.opacity = .123f; style.flex_grow = 17;
        unsigned char saved[sizeof(style)]; std::memcpy(saved, &style, sizeof(style));
        acq = acquisitions; rel = releases; conv = conversions;
        check(p->apply_checked(&style, "50%").success == expected, "injected apply outcome");
        check(expected ? equal(style.opacity, .5f) : std::memcmp(saved, &style, sizeof(style)) == 0,
              "apply finite ERANGE allowed; failures atomic");
        check(acquisitions-acq == 1 && releases-rel == (fail_locale ? 0 : 1) &&
              conversions-conv == (fail_locale ? 0 : 1), "apply exact acquisition/release/conversion");
    }
    monitor = false;
    check(owned.empty() && global_calls == 0, "balanced locale lifetime and no global conversion fallback");
#else
    std::printf("opacity registry: Linux locale-failure wrappers unavailable on this platform\n");
#endif
    std::printf("opacity registry: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
