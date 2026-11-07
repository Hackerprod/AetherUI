// Direct grammar, float policy and locale regression. Checks stay active with NDEBUG.
#include "../src/css/linear_gradient.h"
#include "../src/css/css_numeric_conversion_internal.h"
#include <atomic>
#include <bit>
#include <cerrno>
#include <charconv>
#include <cfenv>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what.c_str()); }
}
std::string current() { const char* p = std::setlocale(LC_ALL, nullptr); return p ? p : ""; }
struct LocaleGuard {
    std::string saved = current();
    ~LocaleGuard() { check(std::setlocale(LC_ALL, saved.c_str()) != nullptr, "restore locale"); }
};
void select(const std::string& name) {
    if (!std::setlocale(LC_ALL, name.c_str())) { std::fprintf(stderr, "HARNESS: locale %s unavailable\n", name.c_str()); std::exit(2); }
}
struct Result {
    bool valid = false;
    uint32_t x = 0, y = 0;
    bool operator==(const Result&) const = default;
};
struct Direction { float x, y; };
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
Direction paired_direction(float radians) {
    return {std::sin(radians), -std::cos(radians)};
}
void diagnose_direction(const std::string& source, float degrees, float radians,
                        float actual_x, float actual_y, int rounding_before, int rounding_after) {
    const auto paired = paired_direction(radians);
    // Indirect calls retain the separate scalar libm paths for diagnosis.
    float (*volatile scalar_sin)(float) = static_cast<float (*)(float)>(std::sin);
    float (*volatile scalar_cos)(float) = static_cast<float (*)(float)>(std::cos);
    const float separate_x = scalar_sin(radians);
    const float separate_y = -scalar_cos(radians);
    const auto bits = [](float value) { return std::bit_cast<uint32_t>(value); };
    const float residue = std::fmod(degrees, 360.f);
    std::fprintf(stderr,
        "ANGLE DIAGNOSTIC [%s] locale=%s rounding(before/after)=%d/%d degrees=%a/%08x residue=%a/%08x radians=%a/%08x\n"
        "  product=(%a/%08x,%a/%08x) paired=(%a/%08x,%a/%08x) scalar=(%a/%08x,%a/%08x)\n",
        source.c_str(), current().c_str(), rounding_before, rounding_after, static_cast<double>(degrees), bits(degrees),
        static_cast<double>(residue), bits(residue), static_cast<double>(radians), bits(radians),
        static_cast<double>(actual_x), bits(actual_x), static_cast<double>(actual_y), bits(actual_y),
        static_cast<double>(paired.x), bits(paired.x), static_cast<double>(paired.y), bits(paired.y),
        static_cast<double>(separate_x), bits(separate_x), static_cast<double>(separate_y), bits(separate_y));
}
Result parse(const std::string& source, bool expected, const char* numeric = nullptr) {
    const auto before = current();
    aether_linear_gradient g{73, 91, nullptr, 5};
    aether_linear_gradient_parse_info_s info{true};
    errno = ERANGE;
    const int rounding_before = std::fegetround();
    const bool valid = aether_linear_gradient_try_parse(source.c_str(), &g, &info);
    const int rounding_after = std::fegetround();
    check(valid == expected, source + " validity");
    check(before == current(), source + " preserves caller locale");
    if (!valid) {
        check(g.direction_x == 0 && g.direction_y == 0 && !g.stops && g.stop_count == 0,
              source + " failure clears all output fields");
        check(!info.uses_fixed_corner_direction, source + " failure clears parse info");
    } else {
        check(g.stop_count == 2 && g.stops, source + " stop ownership");
        check(std::isfinite(g.direction_x) && std::isfinite(g.direction_y), source + " finite direction");
        check(std::fabs(std::hypot(g.direction_x, g.direction_y) - 1.f) < 5e-7f, source + " unit direction");
        if (numeric) {
            // Explicit private C-float conversion; never derive the oracle from
            // the process locale or from the unrounded decimal's ideal value.
            aether::css::c_numeric_locale locale;
            const auto value = locale.parse_float(numeric);
            check(value.valid, "reference converts finite C float");
            constexpr float pi = 3.14159265358979323846f;
            const float r = std::fmod(value.value, 360.f) * pi / 180.f;
            const bool matches = std::bit_cast<uint32_t>(g.direction_x) == std::bit_cast<uint32_t>(std::sin(r)) &&
                                 std::bit_cast<uint32_t>(g.direction_y) == std::bit_cast<uint32_t>(-std::cos(r));
            if (!matches) diagnose_direction(source, value.value, r, g.direction_x, g.direction_y, rounding_before, rounding_after);
            check(matches, source + " matches signed fmod float reference on this platform");
            check(!info.uses_fixed_corner_direction, "angle is not a corner approximation");
        }
    }
    Result result{valid, std::bit_cast<uint32_t>(g.direction_x), std::bit_cast<uint32_t>(g.direction_y)};
    aether_linear_gradient_free(&g);
    aether_linear_gradient_free(&g);
    check(!g.stops && g.stop_count == 0, "repeated release safe");
    return result;
}
std::string gradient(const std::string& angle) { return "linear-gradient(" + angle + ",red,blue)"; }
struct Case { std::string text; bool valid; std::string numeric; };
std::vector<Case> cases() {
    std::vector<Case> all;
    for (const char* number : {"0", "-0", "+0", "+.5", "12.5", "-12.5", "1e2", "1E+2", "1e-2",
            "372.5", "-347.5", "90", "450", "-270", "180", "540", "-180", "360", "-360", "720", "-720",
            "1.0000000596046448", "3e38", "-3e38", "3.4028234e38",
            "3.4028235e38", "1e-40", "-1e-40", "1e-999", "-1e-999", "0e999999", "-0e999999"})
        all.push_back({gradient(std::string(number) + "deg"), true, number});
    for (float value : {std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                        std::nextafter(std::numeric_limits<float>::max(), 0.f),
                        std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min()}) {
        char buffer[64];
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
        check(converted.ec == std::errc{}, "float round-trip formatting");
        const std::string number(buffer, converted.ptr);
        all.push_back({gradient(number + "deg"), true, number});
    }
    all.push_back({gradient("/*prefix*/\f\t+.5DeG/**/\f"), true, "+.5"});
    all.push_back({gradient("1E+2DEG"), true, "1E+2"});
    for (const char* angle : {"0x10deg", "0x1p2deg", "1.deg", "1e+deg", "NaNdeg", "Infinitydeg", "infdeg",
             "12,5deg", "12/**/.5deg", "12.5/**/deg", "12.5 deg", "12.5deg extra", "12.5degg",
             "12.5deg\\", "12.5deg/*", "1e100deg", "-1e100deg", "3.4028236e38deg",
             "-3.4028236e38deg", "[12.5deg]", "(12.5deg)", "\"12.5deg\""})
        all.push_back({gradient(angle), false, {}});
    // Deliberately unsupported product subset, not invalid full CSS syntax.
    for (const char* angle : {"0", "1.5rad", "200grad", ".5turn", "1d\\65 g", "calc(180deg)"})
        all.push_back({gradient(angle), false, {}});
    all.push_back({"linear-gradient(red,blue)", true, {}});
    all.push_back({gradient("to top right"), true, {}});
    all.push_back({gradient("to/**/right"), true, {}});
    all.push_back({"linear-gradient(12.5deg,red calc(10% + 2px),blue 80%)", true, "12.5"});
    all.push_back({"linear-gradient(12.5deg,red,blue)junk", false, {}});
    all.push_back({"linear-gradientx(12.5deg,red,blue)", false, {}});
    return all;
}
void concurrency(const std::vector<Case>& all, const std::vector<Result>& expected) {
    const auto before = current();
    std::atomic<unsigned> errors{0};
    std::vector<std::thread> workers;
    // Global locale is fixed throughout concurrent work. Never call setlocale
    // (even its read form) from these workers.
    for (int thread = 0; thread < 6; ++thread) workers.emplace_back([&] {
        for (int repeat = 0; repeat < 30; ++repeat) for (size_t i = 0; i < all.size(); ++i) {
            aether_linear_gradient g{};
            const bool valid = aether_linear_gradient_try_parse(all[i].text.c_str(), &g);
            Result actual{valid, std::bit_cast<uint32_t>(g.direction_x), std::bit_cast<uint32_t>(g.direction_y)};
            if (!(actual == expected[i])) ++errors;
            aether_linear_gradient_free(&g);
        }
    });
    for (auto& worker : workers) worker.join();
    check(errors == 0, "simultaneous parses match C baseline");
    check(before == current(), "concurrent parses preserve global locale");
}
}
int main(int argc, char** argv) {
    const bool require_all = argc == 2 && std::string(argv[1]) == "--require-all";
    if (argc > 2 || (argc == 2 && !require_all)) return 2;
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
                check(std::string(std::localeconv()->decimal_point) == ",", "alternate locale decimal comma");
                locales.emplace_back(name); found = true; break;
            }
            if (!found) std::printf("SKIP unavailable locale family %s\n", family[0]);
            if (require_all) check(found, std::string("required locale ") + family[0]);
        }
        select(locales.back());
        // First parser call and its first actual conversion occur in a non-C locale.
        parse(gradient("12.5deg"), true, "12.5");
        const auto all = cases();
        select("C");
        std::vector<Result> baseline;
        for (const auto& c : all) baseline.push_back(parse(c.text, c.valid, c.numeric.empty() ? nullptr : c.numeric.c_str()));
        for (const auto& locale : locales) {
            select(locale);
            for (size_t i = 0; i < all.size(); ++i) {
                const auto& c = all[i];
                check(parse(c.text, c.valid, c.numeric.empty() ? nullptr : c.numeric.c_str()) == baseline[i],
                      locale + " matches C validity and direction bits");
            }
            // Geometric periodicity is tolerant, unlike same-platform reference bits.
            for (const char* angle : {"372.5deg", "-347.5deg"}) {
                aether_linear_gradient a{}, b{};
                check(aether_linear_gradient_try_parse(gradient("12.5deg").c_str(), &a) &&
                      aether_linear_gradient_try_parse(gradient(angle).c_str(), &b), "periodic pair parses");
                check(std::fabs(a.direction_x - b.direction_x) < 5e-7f && std::fabs(a.direction_y - b.direction_y) < 5e-7f,
                      "equivalent signed residues preserve geometry");
                aether_linear_gradient_free(&a); aether_linear_gradient_free(&b);
            }
            concurrency(all, baseline);
        }
        std::printf("gradient_angle: %zu locales, %zu grammar/numeric cases\n", locales.size(), all.size());
    }
    check(original == current(), "original locale restored");
    std::printf("gradient_angle: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
