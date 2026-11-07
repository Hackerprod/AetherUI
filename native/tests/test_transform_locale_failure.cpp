// Linux only: compile transforms.cpp, css_length.cpp (AETHER_CSS_NUMERIC_ONLY),
// and css_variable_tokens.cpp here, link Core for public ownership functions.
// Link --wrap=newlocale,--wrap=freelocale,--wrap=strtof_l,--wrap=strtod_l,
//      --wrap=strtof,--wrap=strtod. Never wrap the exported list free function.
#include "../src/css/transforms.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <locale.h>
#include <set>

namespace {
unsigned failures = 0, creates = 0, frees = 0, floats = 0, doubles = 0, globals = 0;
bool fail_creation = false, force_range = false, force_nonfinite = false;
std::set<locale_t> owned;
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
struct Counts {
    unsigned c = creates, f = frees, s = floats, d = doubles, g = globals;
    void expect(unsigned acquired, unsigned released, unsigned single, unsigned dbl = 0) const {
        check(creates - c == acquired, "acquisition count");
        check(frees - f == released, "release count");
        check(floats - s == single, "float conversion count");
        check(doubles - d == dbl, "double conversion count");
        check(globals == g, "no global-locale conversion or fallback");
        check(owned.empty(), "no acquired handle left after parse");
    }
};
void parse(const char* input, bool expected) {
    aether_transform_list list{nullptr, 73};
    check(aether_transform_list_try_parse(input, &list) == expected, input);
    if (!expected) check(list.items == nullptr && list.count == 0, "failure publishes empty list atomically");
    aether_transform_list_free(&list);
}
}
extern "C" locale_t __real_newlocale(int, const char*, locale_t);
extern "C" void __real_freelocale(locale_t);
extern "C" float __real_strtof_l(const char*, char**, locale_t);
extern "C" double __real_strtod_l(const char*, char**, locale_t);
extern "C" locale_t __wrap_newlocale(int mask, const char* name, locale_t base) {
    ++creates;
    check(mask == LC_NUMERIC_MASK && name && std::strcmp(name, "C") == 0 && !base,
          "private C numeric locale with no borrowed base");
    if (fail_creation) { errno = ENOMEM; return nullptr; }
    locale_t result = __real_newlocale(mask, name, base);
    check(result != nullptr, "real locale creation");
    if (result) check(owned.insert(result).second, "unique owned handle");
    errno = ERANGE; // Must not leak into conversion status.
    return result;
}
extern "C" void __wrap_freelocale(locale_t locale) {
    ++frees;
    if (locale && owned.erase(locale) == 1) __real_freelocale(locale);
    else check(false, "no null, foreign or duplicate release");
    errno = ERANGE; // Must not retroactively invalidate successful conversion.
}
extern "C" float __wrap_strtof_l(const char* text, char** end, locale_t locale) {
    ++floats;
    check(errno == 0, "float clears errno immediately before conversion");
    if (!locale || !owned.count(locale)) { check(false, "float requires owned locale"); *end = const_cast<char*>(text); return 0; }
    const float value = __real_strtof_l(text, end, locale);
    if (force_range) errno = ERANGE;
    return force_nonfinite ? std::numeric_limits<float>::infinity() : value;
}
extern "C" double __wrap_strtod_l(const char* text, char** end, locale_t locale) {
    ++doubles;
    check(errno == 0, "double clears errno immediately before conversion");
    if (!locale || !owned.count(locale)) { check(false, "double requires owned locale"); *end = const_cast<char*>(text); return 0; }
    const double value = __real_strtod_l(text, end, locale);
    if (force_range) errno = ERANGE;
    return force_nonfinite ? std::numeric_limits<double>::infinity() : value;
}
extern "C" float __wrap_strtof(const char* text, char** end) {
    ++globals; check(false, "forbidden global strtof");
    if (end) *end = const_cast<char*>(text);
    return 0;
}
extern "C" double __wrap_strtod(const char* text, char** end) {
    ++globals; check(false, "forbidden global strtod");
    if (end) *end = const_cast<char*>(text);
    return 0;
}
int main() {
    fail_creation = true;
    for (const char* input : {"scale(1.5)", "rotate(1.5deg)", "matrix(1,0,0,1,0,0)",
                               "scale(1.5) rotate(2.5deg)", "translateX(1.5px)"}) {
        Counts counts; parse(input, false); counts.expect(1, 0, 0);
    }
    // No numeric conversion is needed, even while allocation is unavailable.
    for (const char* input : {"none", "/**/ NoNe /**/"}) {
        Counts counts; parse(input, true); counts.expect(0, 0, 0);
    }
    for (const char* input : {"", "bogus(1)", "scale()", "scale(1,)", "scale(nan)",
                               "rotate(inf)", "rotate(1quux)", "scale(0x1p0)", "none scale(1)"}) {
        Counts counts; parse(input, false); counts.expect(0, 0, 0);
    }
    fail_creation = false;
    {
        Counts counts; errno = ERANGE;
        parse("scale(1.5,.5) rotate(2.5deg) skewX(.25rad) matrix(1,0,0,1,2,3)", true);
        counts.expect(1, 1, 10); // One owner reused for every scalar/angle in a list.
    }
    {
        Counts counts; parse("translate(calc(1.5px + 2.5%),3.5em) scale(.5) rotate(.25turn)", true);
        counts.expect(3, 3, 2, 3); // Translate keeps its typed double conversion helpers.
    }
    {
        Counts counts; parse("scale(1.5) rotate(nan)", false); counts.expect(1, 1, 1);
    }
    for (const char* input : {"scale(1e-40)", "rotate(1e-40rad)", "scale(1e100)", "rotate(1e100deg)"}) {
        Counts counts; parse(input, false); counts.expect(1, 1, 1);
    }
    force_range = true;
    for (const char* input : {"scale(.5)", "rotate(.5rad)", "skewY(.5deg)", "matrix(.5,0,0,1,0,0)"}) {
        Counts counts; parse(input, false); counts.expect(1, 1, 1);
    }
    {
        Counts counts; parse("translateX(1.5px)", false); counts.expect(1, 1, 0, 1);
    }
    force_range = false; force_nonfinite = true;
    for (const char* input : {"scale(.5)", "rotate(.5rad)"}) {
        Counts counts; parse(input, false); counts.expect(1, 1, 1);
    }
    force_nonfinite = false;
    {
        Counts counts; errno = ERANGE;
        parse("scale(.5) rotate(.25rad)", true); counts.expect(1, 1, 2);
    }
    check(owned.empty(), "all handles released");
    check(globals == 0, "global conversions never called");
    std::printf("transform_locale_failure: %u failures\n", failures);
    return failures ? 1 : 0;
}
