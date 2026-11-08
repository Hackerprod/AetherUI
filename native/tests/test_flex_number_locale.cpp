#include "../src/style_runtime/style_property_registry_internal.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#if defined(AETHER_FLEX_NUMBER_LOCALE_WRAPPERS)
#include <cerrno>
#include <locale.h>
#include <set>
#endif

void builtin_properties_register(style_property_registry_s*);
namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL flex number locale: %s\n", label.c_str()); }
}
#if defined(AETHER_FLEX_NUMBER_LOCALE_WRAPPERS)
bool monitor = false, fail_locale = false;
unsigned acquisitions = 0, releases = 0, conversions = 0, doubles = 0, globals = 0;
std::set<locale_t> owned;
enum class End { complete, partial, empty };
struct RawCase {
    const char* label;
    float raw;
    int error;
    End end;
    bool accepted;
    float expected;
};
const RawCase* injected = nullptr;
struct Counts {
    unsigned a = acquisitions, r = releases, c = conversions, d = doubles, g = globals;
    void expect(unsigned acquire, unsigned release, unsigned convert) const {
        check(acquisitions - a == acquire, "exact acquisition count");
        check(releases - r == release, "exact release count");
        check(conversions - c == convert, "exact native-float conversion count");
        check(doubles == d, "no double conversion or double rounding path");
        check(globals == g, "no global-locale conversion fallback");
        check(owned.empty(), "all owners released after callback");
    }
};
bool equal(float actual, float expected) {
    return std::isfinite(actual) && actual == expected && (expected != 0 || !std::signbit(actual));
}
#endif
}

#if defined(AETHER_FLEX_NUMBER_LOCALE_WRAPPERS)
extern "C" locale_t __real_newlocale(int, const char*, locale_t);
extern "C" void __real_freelocale(locale_t);
extern "C" float __real_strtof_l(const char*, char**, locale_t);
extern "C" double __real_strtod_l(const char*, char**, locale_t);
extern "C" float __real_strtof(const char*, char**);
extern "C" double __real_strtod(const char*, char**);
extern "C" locale_t __wrap_newlocale(int mask, const char* name, locale_t base) {
    if (!monitor) return __real_newlocale(mask, name, base);
    ++acquisitions;
    check(mask == LC_NUMERIC_MASK && std::strcmp(name, "C") == 0 && !base, "private C locale identity");
    if (fail_locale) { errno = ENOMEM; return nullptr; }
    const auto locale = __real_newlocale(mask, name, base);
    check(locale && owned.insert(locale).second, "unique owned nonnull locale");
    errno = ERANGE; // Acquisition must not poison the conversion.
    return locale;
}
extern "C" void __wrap_freelocale(locale_t locale) {
    if (!monitor) { __real_freelocale(locale); return; }
    ++releases;
    if (locale && owned.erase(locale) == 1) __real_freelocale(locale);
    else check(false, "release exactly one owned locale");
    errno = ERANGE; // Validate must use the captured status, not cleanup errno.
}
extern "C" float __wrap_strtof_l(const char* text, char** end, locale_t locale) {
    if (!monitor) return __real_strtof_l(text, end, locale);
    ++conversions;
    check(errno == 0 && locale && owned.count(locale), "reset errno and use private owned locale");
    if (!injected) return __real_strtof_l(text, end, locale);
    const size_t size = std::strlen(text);
    check(size > 1, "nonzero multi-character input reaches injected conversion");
    *end = const_cast<char*>(text) + (injected->end == End::complete ? size :
        injected->end == End::partial ? 1 : 0);
    errno = injected->error;
    return injected->raw;
}
extern "C" double __wrap_strtod_l(const char* text, char** end, locale_t locale) {
    if (monitor) ++doubles;
    return __real_strtod_l(text, end, locale);
}
extern "C" float __wrap_strtof(const char* text, char** end) {
    if (monitor) ++globals;
    return __real_strtof(text, end);
}
extern "C" double __wrap_strtod(const char* text, char** end) {
    if (monitor) ++globals;
    return __real_strtod(text, end);
}
#endif

int run_flex_number_locale_tests() {
#if defined(AETHER_FLEX_NUMBER_LOCALE_WRAPPERS)
    style_property_registry_s registry;
    builtin_properties_register(&registry); registry.seal();
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const RawCase cases[] = {
        {"positive infinity ERANGE", infinity, ERANGE, End::complete, true, std::numeric_limits<float>::max()},
        {"positive infinity no ERANGE", infinity, 0, End::complete, false, 0},
        {"negative infinity ERANGE", -infinity, ERANGE, End::complete, false, 0},
        {"negative infinity no ERANGE", -infinity, 0, End::complete, false, 0},
        {"NaN ERANGE", nan, ERANGE, End::complete, false, 0},
        {"NaN no ERANGE", nan, 0, End::complete, false, 0},
        {"partial positive infinity ERANGE", infinity, ERANGE, End::partial, false, 0},
        {"empty positive infinity ERANGE", infinity, ERANGE, End::empty, false, 0},
        {"finite ERANGE", .5f, ERANGE, End::complete, true, .5f},
        {"finite subnormal ERANGE", std::numeric_limits<float>::denorm_min(), ERANGE, End::complete, true,
            std::numeric_limits<float>::denorm_min()},
        {"finite maximum ERANGE", std::numeric_limits<float>::max(), ERANGE, End::complete, true,
            std::numeric_limits<float>::max()},
        {"positive zero underflow ERANGE", 0.f, ERANGE, End::complete, true, 0.f},
        {"negative zero underflow ERANGE", -0.f, ERANGE, End::complete, true, 0.f},
        {"finite no ERANGE", .5f, 0, End::complete, true, .5f},
        {"negative finite defense", -.5f, ERANGE, End::complete, false, 0.f},
        {"partial finite ERANGE", .5f, ERANGE, End::partial, false, 0.f},
    };
    for (const char* name : {"flex-grow", "flex-shrink"}) {
        const auto* property = registry.try_get(name);
        check(property && property->parse && property->apply_checked, std::string(name) + " registered callbacks");
        if (!property || !property->parse || !property->apply_checked) continue;
        monitor = true;
        // Use a nonzero number, never the zero shortcut, for every injected outcome.
        for (const auto& test : cases) {
            injected = &test;
            for (const unsigned padding : {0u, 1u, 128u, 4096u}) {
                std::string output = std::string(padding, 'x') + "12";
                const std::string saved = output;
                Counts counts;
                const bool parsed = property->parse(output.c_str() + padding, output);
                check(parsed == test.accepted, std::string(name) + " alias parse " + test.label);
                check(test.accepted || output == saved, "failed parse keeps entire alias destination");
                counts.expect(1, 1, 1);
                if (parsed) {
                    // Disable injection to independently consume the canonical result.
                    injected = nullptr;
                    aether_computed_style_t style{};
                    Counts canonical_counts;
                    check(property->apply_checked(&style, output).success &&
                        equal(name[5] == 'g' ? style.flex_grow : style.flex_shrink, test.expected),
                        std::string("canonical value ") + test.label);
                    canonical_counts.expect(test.expected == 0 ? 0 : 1, test.expected == 0 ? 0 : 1,
                        test.expected == 0 ? 0 : 1);
                    injected = &test;
                }
            }
            std::string output = "separate sentinel";
            {
                Counts counts;
                const bool parsed = property->parse("12", output);
                check(parsed == test.accepted && (test.accepted || output == "separate sentinel"),
                    std::string("separate parse atomicity ") + test.label);
                counts.expect(1, 1, 1);
            }
            aether_computed_style_t style{};
            style.flex_grow = 7; style.flex_shrink = 9; style.opacity = .75f;
            unsigned char saved[sizeof(style)]; std::memcpy(saved, &style, sizeof(style));
            aether_computed_style_t expected;
            std::memcpy(&expected, &style, sizeof(style));
            (name[5] == 'g' ? expected.flex_grow : expected.flex_shrink) = test.expected;
            Counts counts;
            const auto result = property->apply_checked(&style, "12");
            check(result.success == test.accepted, std::string("raw checked apply ") + test.label);
            check(test.accepted ? (equal(name[5] == 'g' ? style.flex_grow : style.flex_shrink, test.expected) &&
                std::memcmp(&style, &expected, sizeof(style)) == 0) : std::memcmp(saved, &style, sizeof(style)) == 0,
                "only target changes on success; whole destination intact on failure");
            if (!test.accepted)
                check(result.diagnostic_code && std::strcmp(result.diagnostic_code, "AETHER-CSS-FLEX-NUMBER") == 0 &&
                    result.diagnostic_message && *result.diagnostic_message, "checked failure diagnostic");
            counts.expect(1, 1, 1);
        }
        injected = nullptr;
        fail_locale = true;
        for (const unsigned padding : {0u, 1u, 128u, 4096u}) {
            std::string output = std::string(padding, 'x') + "12";
            const std::string saved = output;
            Counts counts;
            check(!property->parse(output.c_str() + padding, output) && output == saved,
                "locale failure preserves full/interior parse alias");
            counts.expect(1, 0, 0);
        }
        {
            aether_computed_style_t style{}; style.flex_grow = 7; style.flex_shrink = 9;
            unsigned char saved[sizeof(style)]; std::memcpy(saved, &style, sizeof(style));
            Counts counts;
            const auto result = property->apply_checked(&style, "12");
            check(!result.success && result.diagnostic_code &&
                std::strcmp(result.diagnostic_code, "AETHER-CSS-FLEX-NUMBER") == 0 &&
                std::memcmp(saved, &style, sizeof(style)) == 0, "locale failure preserves apply destination");
            counts.expect(1, 0, 0);
        }
        // Zero and invalid syntax do not even attempt acquiring a locale. Huge
        // exponent digits do not make an all-zero significand mathematically nonzero.
        for (const char* zero : {"0", "-0e10", "-0.000e-99999", "+0e9999999999999999999999"}) {
            std::string output = "sentinel";
            aether_computed_style_t style{}; style.flex_grow = 7; style.flex_shrink = 9;
            Counts counts;
            check(property->parse(zero, output) && output == "0", "zero shortcut canonical parse");
            check(property->apply_checked(&style, zero).success &&
                equal(name[5] == 'g' ? style.flex_grow : style.flex_shrink, 0), "zero shortcut checked apply");
            counts.expect(0, 0, 0);
        }
        for (const char* invalid : {"nan", "inf", "0x1p1", "12 3", "-1e-9999", "1.", "1/**/2"}) {
            std::string output = "sentinel";
            aether_computed_style_t style{};
            unsigned char saved[sizeof(style)]; std::memcpy(saved, &style, sizeof(style));
            Counts counts;
            check(!property->parse(invalid, output) && output == "sentinel", "lexical/range failure precedes conversion");
            check(!property->apply_checked(&style, invalid).success && std::memcmp(saved, &style, sizeof(style)) == 0,
                "lexical/range apply fails atomically before conversion");
            counts.expect(0, 0, 0);
        }
        fail_locale = false;
        monitor = false;
    }
    check(owned.empty() && globals == 0 && doubles == 0, "balanced ownership and float-only locale-isolated path");
#else
    std::puts("SKIP flex number injected locale: GNU linker wrappers require Linux");
#endif
    std::printf("flex number locale: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
