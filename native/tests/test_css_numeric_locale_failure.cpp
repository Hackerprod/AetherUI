// Linux only. Compile the real parser sources into this executable and link
// --wrap=newlocale,--wrap=freelocale,--wrap=strtof_l,--wrap=strtod_l.
#include <aether/aether.h>
#include "../src/css/css_length_internal.h"
#include "../src/css/css_numeric_conversion_internal.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <locale.h>
#include <set>
#include <string>

namespace {
unsigned failures = 0, creates = 0, frees = 0, conversions = 0;
bool fail_creation = false, force_range = false;
std::set<locale_t> owned;
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
struct Counts {
    unsigned c = creates, f = frees, n = conversions;
    void expect(unsigned acquired, unsigned released, unsigned parsed) const {
        check(creates - c == acquired, "acquisition count");
        check(frees - f == released, "release count");
        check(conversions - n == parsed, "conversion count");
    }
};
bool same(const aether::css::calc_value_s& a, const aether::css::calc_value_s& b) {
    return a.kind == b.kind && a.number == b.number && a.length.unit == b.length.unit &&
        a.length.value == b.length.value && a.length.linear_percent == b.length.linear_percent &&
        a.length.linear_em == b.length.linear_em && a.length.linear_rem == b.length.linear_rem &&
        a.length.linear_vw == b.length.linear_vw && a.length.linear_vh == b.length.linear_vh;
}
}
extern "C" locale_t __real_newlocale(int, const char*, locale_t);
extern "C" void __real_freelocale(locale_t);
extern "C" float __real_strtof_l(const char*, char**, locale_t);
extern "C" double __real_strtod_l(const char*, char**, locale_t);
extern "C" locale_t __wrap_newlocale(int mask, const char* name, locale_t base) {
    ++creates;
    check(mask == LC_NUMERIC_MASK && std::strcmp(name, "C") == 0 && !base,
          "private C numeric locale with no borrowed base");
    if (fail_creation) { errno = ENOMEM; return nullptr; }
    locale_t result = __real_newlocale(mask, name, base);
    check(result != nullptr, "real locale creation");
    if (result) check(owned.insert(result).second, "unique owned locale");
    errno = ERANGE; // Acquisition must not contaminate a later conversion.
    return result;
}
extern "C" void __wrap_freelocale(locale_t locale) {
    ++frees;
    if (locale && owned.erase(locale) == 1) __real_freelocale(locale);
    else check(false, "no null, foreign or duplicate locale release");
    errno = ERANGE; // Callers must use the captured conversion status.
}
extern "C" float __wrap_strtof_l(const char* text, char** end, locale_t locale) {
    ++conversions;
    check(errno == 0, "float clears errno immediately before conversion");
    if (!locale || !owned.count(locale)) { check(false, "float requires owned nonnull locale"); *end = const_cast<char*>(text); return 0; }
    float result = __real_strtof_l(text, end, locale);
    if (force_range) errno = ERANGE;
    return result;
}
extern "C" double __wrap_strtod_l(const char* text, char** end, locale_t locale) {
    ++conversions;
    check(errno == 0, "double clears errno immediately before conversion");
    if (!locale || !owned.count(locale)) { check(false, "double requires owned nonnull locale"); *end = const_cast<char*>(text); return 0; }
    double result = __real_strtod_l(text, end, locale);
    if (force_range) errno = ERANGE;
    return result;
}
int main() {
    using namespace aether::css;
    fail_creation = true;
    for (const char* input : {"1.5px", "calc(1.5px + 2px)"}) {
        Counts counts;
        aether_css_length value = aether_css_length_zero(); value.value = 99;
        check(!aether_css_length_try_parse(input, &value), "resource failure rejects length");
        check(value.unit == AETHER_CSS_UNIT_AUTO, "resource failure publishes AUTO");
        counts.expect(1, 0, 0);
    }
    calc_value_s sentinel{}; sentinel.kind = calc_value_kind_e::length;
    sentinel.number = 73; sentinel.length.value = 91; sentinel.length.linear_em = 17;
    for (bool calc : {false, true}) {
        Counts counts; auto value = sentinel;
        check(!(calc ? try_parse_calc("calc(1.5px + 2px)", value) : try_parse_numeric_literal("1.5px", value)), "internal failure");
        check(same(value, sentinel), "internal failure preserves output"); counts.expect(1, 0, 0);
    }
    for (const char* input : {"rgb(1,2,3)", "rgba(10%,20%,30%,.5)"}) {
        Counts counts; aether_color_rgba color{7,8,9,10};
        check(!aether_color_rgba_try_parse(input, &color), "resource failure rejects RGB");
        check(color.r == 0 && color.g == 0 && color.b == 0 && color.a == 0, "failure publishes transparent");
        counts.expect(1, 0, 0);
    }
    {
        Counts counts; aether_css_length length{}; aether_color_rgba color{}; auto value = sentinel;
        check(aether_css_length_try_parse("auto", &length), "auto independent of locale resource");
        check(aether_color_rgba_try_parse("red", &color) && color.r == 1, "named color independent");
        check(aether_color_rgba_try_parse("#f00", &color) && color.r == 1, "hex independent");
        check(!try_parse_calc("1.5px", value) && same(value, sentinel), "non-calc attempt unchanged");
        check(!try_parse_calc("calc(auto)", value), "no number means no acquisition");
        counts.expect(0, 0, 0);
    }
    {
        Counts counts;
        { c_numeric_locale locale;
          check(!locale.parse_float("1").valid, "failed helper float");
          fail_creation = false;
          check(!locale.parse_double("2").valid, "failed helper never retries even after recovery"); }
        counts.expect(1, 0, 0);
    }
    {
        Counts counts;
        { c_numeric_locale locale;
          check(locale.parse_double("1.5").valid, "fresh helper recovers");
          force_range = true;
          auto ranged = locale.parse_float(".5");
          check(ranged.valid && ranged.range_error && ranged.value == .5f, "finite ERANGE reported, not invalidated");
          force_range = false; errno = ERANGE;
          auto clean = locale.parse_double("2.5");
          check(clean.valid && !clean.range_error && clean.value == 2.5, "previous range error does not leak"); }
        counts.expect(1, 1, 3);
    }
    for (const char* input : {"1.5px", "calc(1.5px + 2px + 3px)"}) {
        Counts counts; aether_css_length value{}; errno = ERANGE;
        check(aether_css_length_try_parse(input, &value), "length recovers despite acquisition/destructor errno");
        counts.expect(1, 1, input[0] == 'c' ? 3 : 1);
    }
    {
        Counts counts; aether_color_rgba color{};
        check(aether_color_rgba_try_parse("rgba(1,2,3,.5)", &color), "RGBA recovers");
        counts.expect(1, 1, 4);
    }
    force_range = true;
    {
        Counts counts; aether_css_length value{};
        check(!aether_css_length_try_parse("1.5px", &value) && value.unit == AETHER_CSS_UNIT_AUTO, "length rejects finite double ERANGE");
        counts.expect(1, 1, 1);
    }
    {
        Counts counts; aether_color_rgba color{};
        check(aether_color_rgba_try_parse("rgba(1,2,3,.5)", &color) && color.a == .5f, "RGB permits finite float ERANGE");
        counts.expect(1, 1, 4);
    }
    check(owned.empty(), "all acquired handles released");
    std::printf("css_numeric_locale_failure: %u failures\n", failures);
    return failures ? 1 : 0;
}
