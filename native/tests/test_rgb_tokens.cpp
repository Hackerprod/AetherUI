#include <aether/aether.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
unsigned checks = 0, failures = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message.c_str()); }
}
bool same(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }
void expect(const std::string& input, bool valid, aether_color_rgba expected = {}) {
    aether_color_rgba actual{7, 8, 9, 10};
    const bool parsed = aether_color_rgba_try_parse(input.c_str(), &actual);
    check(parsed == valid, input + " accepted/rejected");
    check(same(actual.r, expected.r) && same(actual.g, expected.g) &&
          same(actual.b, expected.b) && same(actual.a, expected.a), input + " exact output");
}
std::string rgb(const std::vector<std::string>& channels) {
    std::string s = channels.size() == 4 ? "rgba(" : "rgb(";
    for (size_t i = 0; i < channels.size(); ++i) s += (i ? "," : "") + channels[i];
    return s + ")";
}
}
int main() {
    expect("rgb(255,0,127.5)", true, {1, 0, .5f, 1});
    expect("rgba(+255,-0,1.275e2,+.5)", true, {1, 0, .5f, .5f});
    expect("rgb(+1e2%,0%,50%)", true, {1, 0, .5f, 1});
    expect("rgba(100%,0%,50%,.5)", true, {1, 0, .5f, .5f});
    expect("rgba(255,0,127.5,50%)", true, {1, 0, .5f, .5f});
    expect("RgBa(/**/255/**/,\t0\n,127.5\f,/**/.5/**/)", true, {1, 0, .5f, .5f});
    expect("rgb(999,-999,255)", true, {1, 0, 1, 1});
    expect("rgba(200%,-100%,50%,200%)", true, {1, 0, .5f, 1});
    expect("rgba(0,0,0,-2)", true, {0, 0, 0, 0});
    expect("#1234", true, {17 / 255.f, 34 / 255.f, 51 / 255.f, 68 / 255.f});
    expect("transparent", true, {});
    expect("RED", true, {1, 0, 0, 1});

    // All six mixed category permutations fail, even when every channel is zero.
    for (unsigned mask = 1; mask < 7; ++mask) for (int arity : {3, 4}) {
        std::vector<std::string> channels;
        for (unsigned i = 0; i < 3; ++i) channels.emplace_back(mask & (1u << i) ? "0%" : "0");
        if (arity == 4) channels.emplace_back("50%");
        expect(rgb(channels), false);
    }
    const std::vector<std::string> invalid = {
        "0x10", "+0X1p2", "-0x1.8p+2", "1px", "1deg", "1e", "1e+", "1e-", "1.",
        "nan", "NaN(1)", "inf", "infinity", "'1'", "\"1\"", "calc(1)", "var(--n)",
        "none", "10/**/%", "1/**/2", "1 2", "1+2", "1%2", "1/", "1#x", "(1)",
        "[1]", "{1}", "1\\31", "1/*", "", "/**/", "+", ".", "1%%", "1%px",
        "1e999", "-1e999", "3.4028236e38", "-3.4028236e38"
    };
    for (const auto& token : invalid) for (size_t channel = 0; channel < 4; ++channel) {
        std::vector<std::string> channels{"1", "2", "3", ".5"};
        channels[channel] = token;
        expect(rgb(channels), false);
    }
    for (const char* input : {"", "rgb()", "rgb(1,2)", "rgb(1,2,3,4)", "rgba(1,2,3)",
             "rgba(1,2,3,4,5)", "rgb(1 2 3)", "rgb(1 2 3 / .5)", "rgb(1,2,3,)",
             "rgb(,1,2)", "rgb(1,,2)", "rgb(1,2,3))", "rgb(1,2,3) junk", "rgb/**/(1,2,3)",
             "rgb(1,2,3/*)", "rgba(1,2,3,.5/*)", "rgb(1,2,[3)", "rgb(1,2,3) ",
             "rgb(1,2,\v3)", "rgb(1,2,\xc2\xa0" "3)"}) expect(input, false);

    // Direct float conversion, not double conversion followed by clamping.
    // Tiny alpha values expose subnormal preservation without RGB /255 rounding.
    for (const char* number : {"1e-40", "1e-45", "1e-999", "-1e-999", "-1e-40",
                              "3.4028234e38", "3.4028235e38", "-3.4028235e38"}) {
        char* end = nullptr;
        const float parsed = std::strtof(number, &end); // A new process starts in C locale.
        check(end && *end == '\0' && std::isfinite(parsed), std::string(number) + " C reference");
        const float alpha = fminf(fmaxf(parsed, 0.f), 1.f);
        const float channel = fminf(fmaxf(parsed / 255.f, 0.f), 1.f);
        const float percentage = fminf(fmaxf(parsed / 100.f, 0.f), 1.f);
        expect(rgb({"0", "0", "0", number}), true, {0, 0, 0, alpha});
        expect(rgb({number, "0", "0"}), true, {channel, 0, 0, 1});
        expect(rgb({"0", "0", "0", std::string(number) + "%"}), true, {0, 0, 0, percentage});
        expect(rgb({std::string(number) + "%", "0%", "0%"}), true, {percentage, 0, 0, 1});
    }
    const std::string pct31 = std::string(30, '0') + "1%";
    const std::string pct32 = std::string(31, '0') + "1%";
    expect(rgb({pct31, "0%", "0%"}), true, {.01f, 0, 0, 1});
    expect(rgb({pct32, "0%", "0%"}), false);
    expect(rgb({"0", "0", "0", pct31}), true, {0, 0, 0, .01f});
    expect(rgb({"0", "0", "0", pct32}), false);
    expect(rgb({std::string(32, '0') + "255", "0", "0"}), true, {1, 0, 0, 1});
    expect("rgb(" + std::string(117, ' ') + "0,0,0)", true, {0, 0, 0, 1}); // 127 bytes
    expect("rgb(" + std::string(118, ' ') + "0,0,0)", false); // 128 bytes
    aether_color_rgba untouched{7, 8, 9, 10};
    check(!aether_color_rgba_try_parse(nullptr, &untouched) && untouched.r == 7 && untouched.a == 10,
          "null input preserves existing output contract");
    check(!aether_color_rgba_try_parse("rgb(1,2,3)", nullptr), "null output");
    check(!aether_color_rgba_try_parse(nullptr, nullptr), "both null");
    std::printf("rgb_tokens: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
