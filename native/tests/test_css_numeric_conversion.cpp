// Include first: the conversion header is self-contained and stays private.
#include "../src/css/css_numeric_conversion_internal.h"
#include "../src/css/css_length_internal.h"
#include <bit>
#include <charconv>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <vector>

namespace {
using namespace aether::css;
unsigned checks = 0, failures = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}
template<class T> bool same(T a, T b) {
    using Bits = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
    return std::bit_cast<Bits>(a) == std::bit_cast<Bits>(b);
}
std::string spelling(double value) {
    char text[64];
    auto result = std::to_chars(text, text + sizeof(text), value,
        std::chars_format::scientific, std::numeric_limits<double>::max_digits10);
    check(result.ec == std::errc{}, "round-trip spelling");
    return std::string(text, result.ptr);
}
struct LocaleGuard {
    std::string saved = std::setlocale(LC_ALL, nullptr);
    ~LocaleGuard() { std::setlocale(LC_ALL, saved.c_str()); }
};
void conversions() {
    static_assert(!std::is_copy_constructible_v<c_numeric_locale>);
    static_assert(!std::is_move_constructible_v<c_numeric_locale>);
    c_numeric_locale owner;
    std::vector<std::string> tokens = {"+.5", "-0", "0e-9999", "-0e-9999", "", ".", "1x",
        std::string("1\0.5", 5), "1.0000000596046448", "1e-40", "1e-45", "1e-999", "-1e-999",
        "3.4028234e38", "3.4028235e38", "3.4028236e38", "-3.4028236e38", "1e100",
        "1e-300", "1e-400", "1.7976931348623157e308", "1.7976931348623159e308",
        "2.2250738585072014e-308", "2.225073858507201e-308", "4.9406564584124654e-324",
        "nan", "inf", "0x1p-1", " 1.5", "1.5 "};
    for (const double number : {std::numeric_limits<double>::max(),
             std::nextafter(std::numeric_limits<double>::max(), 0.),
             std::numeric_limits<double>::min(),
             std::nextafter(std::numeric_limits<double>::min(), 0.),
             std::nextafter(std::numeric_limits<double>::min(), 1.),
             std::numeric_limits<double>::denorm_min()}) {
        tokens.push_back(spelling(number));
        tokens.push_back(spelling(-number));
    }
    for (const auto& token : tokens) {
        char* end = nullptr;
        errno = 0;
        const float f = std::strtof(token.c_str(), &end);
        const int f_errno = errno;
        const bool f_valid = end != token.c_str() && end == token.c_str() + token.size() && std::isfinite(f);
        errno = ERANGE; // An earlier channel/caller cannot poison this conversion.
        const auto actual_f = owner.parse_float(token);
        check(actual_f.valid == f_valid && actual_f.range_error == (f_errno == ERANGE) &&
              (!f_valid || same(actual_f.value, f)), "float C oracle " + token);
        errno = 0;
        const double d = std::strtod(token.c_str(), &end);
        const int d_errno = errno;
        const bool d_valid = end != token.c_str() && end == token.c_str() + token.size() && std::isfinite(d);
        errno = ERANGE;
        const auto actual_d = owner.parse_double(token);
        check(actual_d.valid == d_valid && actual_d.range_error == (d_errno == ERANGE) &&
              (!d_valid || same(actual_d.value, d)), "double C oracle " + token);
    }
    const auto f = owner.parse_float("1.0000000596046448");
    const auto d = owner.parse_double("1.0000000596046448");
    check(f.valid && d.valid && same(f.value, std::nextafter(1.f, 2.f)) &&
          same(static_cast<float>(d.value), 1.f), "native float conversion avoids double rounding");
    owner.parse_float("1e-999");
    check(owner.parse_float("1.5").valid && !owner.parse_float("1.5").range_error,
          "earlier float ERANGE cannot leak");
    owner.parse_double("1e999");
    check(owner.parse_double("1.5").valid && !owner.parse_double("1.5").range_error,
          "earlier double ERANGE cannot leak");
}
void lengths() {
    for (const char* input : {"0", "-0", "0e-9999", "-0e-9999", "1e-46", "1e-300", "1e100",
             "1.7976931348623157e308", "2.2250738585072014e-308", "2.225073858507201e-308",
             "4.9406564584124654e-324", "1e-400", "1.7976931348623159e308"}) {
        char* end = nullptr;
        errno = 0;
        const double expected = std::strtod(input, &end);
        const int error = errno;
        const bool valid = error != ERANGE && std::isfinite(expected);
        calc_value_s actual{};
        errno = ERANGE;
        const bool parsed = try_parse_numeric_literal(input, actual);
        check(parsed == valid && (!valid || (actual.kind == calc_value_kind_e::number && same(actual.number, expected))),
            std::string("Number preserves C double policy ") + input);
    }
    for (const double sign : {1., -1.}) {
        const double maximum = sign * static_cast<double>(std::numeric_limits<float>::max());
        const double beyond = std::nextafter(maximum, sign * std::numeric_limits<double>::infinity());
        aether_css_length length{};
        check(aether_css_length_try_parse((spelling(maximum) + "px").c_str(), &length) &&
              same(length.value, static_cast<float>(maximum)), "exact FLT_MAX length");
        check(!aether_css_length_try_parse((spelling(beyond) + "px").c_str(), &length) &&
              length.unit == AETHER_CSS_UNIT_AUTO, "next double beyond FLT_MAX rejected before narrowing");
    }
    aether_css_length length{};
    // The decimal bound is the parsed double, not arbitrary precision decimal.
    check(aether_css_length_try_parse("340282346638528859811704183484516925441px", &length) &&
        same(length.value, std::numeric_limits<float>::max()), "decimal just above exact FLT_MAX still rounds to its double");
    for (const char* input : {"3.4028235e38px", "-3.4028235e38px", "1e100px", "1e-400px",
             "calc(1e-300 * 1e-100 * 1px)", "calc(1e-300 / 1e100 * 1px)", "calc(1px / 0)",
             "calc(1px / 1px)", "calc(1px * 1px)", "calc(1e308 * 10 * 1px)",
             "1,5px", "0x10px", "NaNpx", "Infinitypx", "1.px", "1e+px", "1.5 px", "1/**/.5px"}) {
        length = {42, AETHER_CSS_UNIT_PX};
        check(!aether_css_length_try_parse(input, &length) && length.unit == AETHER_CSS_UNIT_AUTO &&
            same(length.value, 0.f), std::string("invalid length AUTO ") + input);
    }
    struct Case { const char* input; float value; };
    for (const auto& test : {Case{"calc((16777217 - 16777216) * 1px)", 1.f},
             Case{"calc(1e100 * 1e-100 * 1px)", 1.f},
             Case{"calc(1e-46 * 1e30 * 1e16 * 1px)", 1.f},
             Case{"calc(1e-30px / 1e-40)", 1e10f},
             Case{"calc((1e100 - 1e100) * 1px)", 0.f},
             Case{"calc(0 * 1e-300 * 1px)", 0.f}, Case{"1e-46px", 0.f}, Case{"1e-300px", 0.f},
             Case{"1.0000000596046448px", 1.f}, Case{"1.5", 1.5f}}) {
        check(aether_css_length_try_parse(test.input, &length) && same(length.value, test.value),
            std::string("precision and quantization ") + test.input);
    }
    calc_value_s sentinel{calc_value_kind_e::length, 123.5, {42, AETHER_CSS_UNIT_REM}};
    for (const char* input : {"1,5px", "1e-400", "1e999", "bogus"}) {
        auto output = sentinel;
        check(!try_parse_numeric_literal(input, output) && output.kind == sentinel.kind &&
            same(output.number, sentinel.number) && output.length.unit == sentinel.length.unit &&
            same(output.length.value, sentinel.length.value), "failed internal literal leaves output intact");
    }
    for (const char* input : {"calc(1.5px + bogus)", "calc(1px / 0)", "calc(1e999)", "1.5px"}) {
        auto output = sentinel;
        check(!try_parse_calc(input, output) && output.kind == sentinel.kind &&
            same(output.number, sentinel.number) && output.length.unit == sentinel.length.unit &&
            same(output.length.value, sentinel.length.value), "failed internal calc leaves output intact");
    }
    length = {42, AETHER_CSS_UNIT_REM};
    check(!aether_css_length_try_parse(nullptr, &length) && length.unit == AETHER_CSS_UNIT_REM &&
        same(length.value, 42.f), "null input leaves output intact");
    check(!aether_css_length_try_parse("1.5px", nullptr) && !aether_css_length_try_parse(nullptr, nullptr),
        "null output is rejected");
}
void colors() {
    for (const char* number : {"1e-40", "1e-45", "1e-999", "-1e-999", "-1e-40", "-0",
             "3.4028234e38", "3.4028235e38", "3.4028236e38", "-3.4028236e38", "1.0000000596046448"}) {
        const float parsed = std::strtof(number, nullptr);
        for (const bool percentage : {false, true}) {
            const std::string token = std::string(number) + (percentage ? "%" : "");
            const std::string input = "rgb(" + token + (percentage ? ",0%,0%)" : ",0,0)");
            aether_color_rgba result{};
            const bool ok = aether_color_rgba_try_parse(input.c_str(), &result);
            check(ok == std::isfinite(parsed) && (!ok ||
                same(result.r, fminf(fmaxf(parsed / (percentage ? 100.f : 255.f), 0.f), 1.f))),
                "RGB keeps native float range/rounding " + input);
        }
    }
    aether_color_rgba color{};
    check(aether_color_rgba_try_parse("rgb(1.0000000596046448,0,0)", &color) &&
          same(color.r, std::nextafter(1.f, 2.f) / 255.f) && !same(color.r, 1.f / 255.f),
          "public RGB detects double-rounding regression after scale");
}
}
int main() {
    LocaleGuard restore;
    check(std::setlocale(LC_ALL, "C") != nullptr, "select C oracle locale");
    conversions(); lengths(); colors();
    std::printf("css_numeric_conversion: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
