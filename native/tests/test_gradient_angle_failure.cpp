// Linux only: compile the actual gradient and registered property sources
// here; wrapping a previously linked shared object would not intercept them.
// Link --wrap=newlocale,--wrap=freelocale,--wrap=strtof_l,--wrap=strtod_l,
//      --wrap=strtof,--wrap=strtod. Never wrap the exported list free function.
#include "../src/css/linear_gradient.h"
#include "../src/css/background_layers.h"
#include "../src/style_runtime/style_property_registry_internal.h"

void builtin_properties_register(style_property_registry_s*);
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <locale.h>
#include <set>

namespace {
unsigned checks = 0, failures = 0, creates = 0, frees = 0, floats = 0, doubles = 0, globals = 0;
bool fail_creation = false, force_range = false, force_nonfinite = false;
int acquisitions_until_failure = -1;
std::set<locale_t> owned;
void check(bool ok, const char* message) {
    ++checks;
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
    aether_linear_gradient gradient{73, 91, nullptr, 5};
    aether_linear_gradient_parse_info_s info{true};
    check(aether_linear_gradient_try_parse(input, &gradient, &info) == expected, input);
    if (!expected) {
        check(gradient.stops == nullptr && gradient.stop_count == 0 &&
              gradient.direction_x == 0 && gradient.direction_y == 0, "failure publishes empty gradient atomically");
        check(!info.uses_fixed_corner_direction, "failure resets parse info");
    } else {
        check(gradient.stops && gradient.stop_count == 2 &&
              std::isfinite(gradient.direction_x) && std::isfinite(gradient.direction_y), "success owns finite gradient");
    }
    aether_linear_gradient_free(&gradient);
    aether_linear_gradient_free(&gradient);
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
    if (fail_creation || acquisitions_until_failure == 0) { errno = ENOMEM; return nullptr; }
    if (acquisitions_until_failure > 0) --acquisitions_until_failure;
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
    for (const char* input : {"linear-gradient(12.5deg,red,blue)", "linear-gradient(3e38deg,red,blue)",
                               "linear-gradient(-1e-999deg,red,blue)"}) {
        Counts counts; parse(input, false); counts.expect(1, 0, 0);
    }
    // Named colors and keyword/default directions do not acquire a numeric locale.
    for (const char* input : {"linear-gradient(red,blue)", "linear-gradient(to right,red,blue)",
                               "linear-gradient(to top left,red,blue)"}) {
        Counts counts; parse(input, true); counts.expect(0, 0, 0);
    }
    for (const char* input : {"linear-gradient(0x10deg,red,blue)", "linear-gradient(0x1p2deg,red,blue)",
            "linear-gradient(1.deg,red,blue)", "linear-gradient(12.5/**/deg,red,blue)",
            "linear-gradient(NaNdeg,red,blue)", "linear-gradient(12.5turn,red,blue)"}) {
        Counts counts; parse(input, false); counts.expect(0, 0, 0);
    }
    fail_creation = false;
    for (const char* input : {"linear-gradient(12.5deg,red,blue)", "linear-gradient(3e38deg,red,blue)",
            "linear-gradient(-3e38deg,red,blue)", "linear-gradient(1e-40deg,red,blue)",
            "linear-gradient(-1e-999deg,red,blue)", "linear-gradient(-0e9999deg,red,blue)"}) {
        Counts counts; errno = ERANGE; parse(input, true); counts.expect(1, 1, 1);
    }
    for (const char* input : {"linear-gradient(1e100deg,red,blue)", "linear-gradient(3.4028236e38deg,red,blue)"}) {
        Counts counts; parse(input, false); counts.expect(1, 1, 1);
    }
    force_range = true;
    { Counts counts; parse("linear-gradient(.5deg,red,blue)", true); counts.expect(1, 1, 1); }
    force_range = false; force_nonfinite = true;
    { Counts counts; parse("linear-gradient(.5deg,red,blue)", false); counts.expect(1, 1, 1); }
    force_nonfinite = false;
    { Counts counts; errno = ERANGE; parse("linear-gradient(.5deg,red,blue)", true); counts.expect(1, 1, 1); }

    style_property_registry_s registry;
    builtin_properties_register(&registry);
    registry.seal();
    const auto* image = registry.try_get("background-image");
    const auto* background = registry.try_get_shorthand("background");
    check(image && image->parse && image->apply_checked && background && background->expand,
          "real property callbacks registered");
    if (!image || !image->parse || !image->apply_checked || !background || !background->expand) return 1;
    aether_computed_style_t style{};
    check(image->apply_checked(&style, "linear-gradient(to right,red,blue)").success, "seed live destination");
    auto* original = style.background_layers.items;
    auto* stops = original ? original[0].gradient.stops : nullptr;
    fail_creation = true;
    const char* number = "linear-gradient(12.5deg,red,blue)";
    {
        Counts counts;
        std::string parsed = "sentinel";
        check(!image->parse(number, parsed), "validator fails closed on missing locale");
        check(parsed == "sentinel", "validator preserves caller output");
        counts.expect(1, 0, 0);
    }
    {
        Counts counts;
        check(!image->apply_checked(&style, number).success, "applicator fails closed on missing locale");
        // One standalone attempt, then one attempt through the list parser.
        counts.expect(2, 0, 0);
    }
    {
        Counts counts;
        style_shorthand_writer_s writer;
        writer.set("sentinel", "preserved");
        check(!background->expand(number, writer), "shorthand fails closed on missing locale");
        check(writer.items.size() == 1 && writer.items[0].name == "sentinel" && writer.items[0].value == "preserved",
              "failed shorthand publishes no siblings");
        // The primary parser and compatibility fallback may each attempt a parse.
        check(creates > counts.c && frees == counts.f && floats == counts.s && doubles == counts.d && globals == counts.g,
              "each failed shorthand parse makes no conversion or fallback to global locale");
        check(owned.empty(), "failed shorthand owns no locales");
    }
    check(style.background_layers.items == original && style.background_layers.count == 1 &&
          original && original[0].gradient.stops == stops && stops && stops[0].color.r == 1 && stops[1].color.b == 1,
          "failed apply retains complete prior allocation and stops");
    fail_creation = false;
    for (int preceding = 0; preceding < 3; ++preceding) {
        Counts counts;
        acquisitions_until_failure = preceding;
        check(!image->apply_checked(&style,
            "linear-gradient(12.5deg,red,blue),linear-gradient(3e38deg,red,blue),linear-gradient(-12.5deg,red,blue)").success,
            "locale failure in first/middle/last layer rejects whole replacement");
        check(style.background_layers.items == original && original[0].gradient.stops == stops,
              "partial locale-failed replacement preserves previous layers and stops");
        counts.expect(static_cast<unsigned>(preceding + 1), static_cast<unsigned>(preceding), static_cast<unsigned>(preceding));
        acquisitions_until_failure = -1;
    }
    for (const char* bad : {"linear-gradient(0x10deg,red,blue)", "linear-gradient(0x1p2deg,red,blue)",
                             "linear-gradient(3.4028236e38deg,red,blue)"}) {
        const std::string good = "linear-gradient(12.5deg,red,blue)";
        for (const std::string& value : {std::string(bad) + "," + good + "," + good,
                                       good + "," + bad + "," + good, good + "," + good + "," + bad}) {
            check(!image->apply_checked(&style, value.c_str()).success, "invalid layer fails entire replacement");
            check(style.background_layers.items == original && original[0].gradient.stops == stops,
                  "invalid first/middle/last layer preserves prior ownership");
        }
    }
    {
        Counts counts;
        check(image->apply_checked(&style, "linear-gradient(3e38deg,red,blue)").success, "applicator recovers after locale failure");
        check(creates > counts.c && creates - counts.c == frees - counts.f && floats - counts.s == creates - counts.c &&
              doubles == counts.d && globals == counts.g && owned.empty(), "recovery owns and releases each parse locale");
        check(style.background_layers.count == 1 && style.background_layers.items &&
              std::isfinite(style.background_layers.items[0].gradient.direction_x) &&
              std::isfinite(style.background_layers.items[0].gradient.direction_y), "replacement contains finite direction");
    }
    aether_background_layer_list_free(&style.background_layers);
    check(owned.empty(), "all handles released");
    check(globals == 0, "global conversions never called");
    std::printf("gradient_angle_failure: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
