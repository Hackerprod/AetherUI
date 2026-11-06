// Dedicated process. The harness changes the global locale only while no
// workers exist. POSIX per-thread locales belong to the harness, not parsers.
#include <aether/aether.h>
#include <atomic>
#include <bit>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <locale.h>
#define AETHER_TEST_POSIX_LOCALE 1
#endif

namespace {
std::atomic<unsigned> failures{0};
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
struct LocaleGuard {
    std::string saved = std::setlocale(LC_ALL, nullptr);
    ~LocaleGuard() { std::setlocale(LC_ALL, saved.c_str()); }
};
bool same(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }
bool same(const aether_css_length& a, const aether_css_length& b) {
    return a.unit == b.unit && same(a.value, b.value) &&
        same(a.linear_percent,b.linear_percent) && same(a.linear_em,b.linear_em) &&
        same(a.linear_rem,b.linear_rem) && same(a.linear_vw,b.linear_vw) && same(a.linear_vh,b.linear_vh);
}
const char* lengths[] = {"1.5px", "2.5%", "-.5em", "+.25rem", "1.25vw", "2.75vh", "15px",
    "1.5e2px", "1e-46px", "1e-300px", "auto", "-0px",
    "calc(1.5px + 2.5% + .25em + .5rem + 1.25vw + 2.75vh)",
    "calc((16777217 - 16777216) * 1px)", "calc(1e100 * 1e-100 * 1px)",
    "calc(1e-46 * 1e30 * 1e16 * 1px)", "1,5px", "1e-400px", "3.4028235e38px", "calc(1px / 0)"};
struct Result { aether_bool_t valid; aether_css_length value; };
Result parse(const char* input) { Result r{}; r.valid = aether_css_length_try_parse(input, &r.value); return r; }
void exercise(const std::vector<Result>& baseline) {
    for (unsigned iteration = 0; iteration < 300; ++iteration) {
        for (size_t i = 0; i < baseline.size(); ++i) {
            auto result = parse(lengths[i]);
            check(result.valid == baseline[i].valid && same(result.value, baseline[i].value), lengths[i]);
        }
        aether_color_rgba color{};
        check(aether_color_rgba_try_parse("rgba(12.5,25.5,255,.25)", &color) &&
            same(color.r,12.5f/255.f) && same(color.g,25.5f/255.f) && same(color.b,1) && same(color.a,.25f),
            "concurrent shared helper RGB");
    }
}
void run(const std::vector<Result>& baseline, const std::vector<std::string>& locales, bool per_thread) {
    const std::string before = std::setlocale(LC_ALL, nullptr);
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i) workers.emplace_back([&, i] {
#ifdef AETHER_TEST_POSIX_LOCALE
        locale_t own = nullptr;
        const locale_t prior = uselocale(nullptr);
        if (per_thread) {
            own = newlocale(LC_NUMERIC_MASK, locales[i % locales.size()].c_str(), nullptr);
            check(own != nullptr, "harness creates thread locale");
            if (!own) return;
            if (!uselocale(own)) { check(false, "harness selects thread locale"); freelocale(own); return; }
        }
        const locale_t expected = uselocale(nullptr);
#else
        (void)i; (void)locales; (void)per_thread;
#endif
        exercise(baseline);
#ifdef AETHER_TEST_POSIX_LOCALE
        check(uselocale(nullptr) == expected, "parser preserves exact thread locale identity");
        if (own) {
            check(uselocale(prior) != nullptr, "restore prior thread locale");
            freelocale(own);
        }
#endif
    });
    for (auto& worker : workers) worker.join();
    check(before == std::setlocale(LC_ALL, nullptr), "parsers preserve fixed global locale");
}
}
int main(int argc, char** argv) {
    const bool require_all = argc == 2 && std::string(argv[1]) == "--require-all";
    LocaleGuard restore;
    std::vector<std::string> locales{"C"};
    const char* names[][4] = {
        {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "German_Germany.1252"},
        {"fr_FR.UTF-8", "fr_FR.utf8", "fr_FR", "French_France.1252"},
        {"tr_TR.UTF-8", "tr_TR.utf8", "tr_TR", "Turkish_Turkey.1254"}
    };
    for (const auto& aliases : names) {
        bool found = false;
        for (const char* name : aliases) {
            if (std::setlocale(LC_ALL, name) && std::string(std::localeconv()->decimal_point) == ",") {
                locales.emplace_back(name); found = true; break;
            }
        }
        if (!found) std::printf("SKIP unavailable comma locale family %s\n", aliases[0]);
        if (require_all) check(found, aliases[0]);
    }
    check(std::setlocale(LC_ALL, "C") != nullptr, "select C baseline");
    std::vector<Result> baseline;
    for (const char* input : lengths) baseline.push_back(parse(input));
    check(baseline[0].valid && baseline[0].value.unit == AETHER_CSS_UNIT_PX && same(baseline[0].value.value,1.5f), "known baseline decimal");
    check(baseline[13].valid && same(baseline[13].value.value,1), "known double precision calc");
    for (const auto& locale : locales) {
        if (!std::setlocale(LC_ALL, locale.c_str())) { check(false, "select fixed global locale"); continue; }
        run(baseline, locales, false);
#ifdef AETHER_TEST_POSIX_LOCALE
        run(baseline, locales, true);
#endif
        std::printf("THREADS global=%s completed\n", locale.c_str());
    }
    if (locales.size() == 1) std::puts("SKIP non-C concurrency coverage: no comma locale installed");
#ifndef AETHER_TEST_POSIX_LOCALE
    std::puts("SKIP POSIX thread-locale identity: platform lacks POSIX locale API");
#endif
    std::printf("length_locale_threads: %u failures, %zu locale families\n", failures.load(), locales.size());
    return failures ? 1 : 0;
}
