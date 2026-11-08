// Dedicated process: locale mutations here are serial and restored on exit.
#include <aether/aether.h>
#include <bit>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}
struct LocaleGuard {
    std::string saved = std::setlocale(LC_ALL, nullptr);
    ~LocaleGuard() { std::setlocale(LC_ALL, saved.c_str()); }
};
bool set(const std::string& locale) {
    const bool ok = std::setlocale(LC_ALL, locale.c_str()) != nullptr;
    check(ok, "select locale " + locale); return ok;
}
bool same(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }
bool same_color(aether_color_rgba a, aether_color_rgba b) {
    return same(a.r, b.r) && same(a.g, b.g) && same(a.b, b.b) && same(a.a, b.a);
}
void pipeline(const std::string& compile_locale, const std::string& resolve_locale) {
    set(compile_locale);
    aether_doc_t doc{};
    check(aether_doc_create(&doc) == AETHER_OK, "create document");
    check(aether_doc_parse_html(doc, "<html><body><div id='parent'><div id='literal'></div><div id='deferred'></div><div id='badliteral'></div><div id='badvar'></div><div id='namedliteral'></div><div id='nameddeferred'></div></div></body></html>") == AETHER_OK, "parse HTML");
    aether_style_runtime_t runtime{};
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
    check(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
    int64_t sheet{};
    check(aether_style_runtime_add_stylesheet(runtime,
        "#parent{color:#2468ac;}"
        "#literal{color:rgba(12.5,25.5,255,.25);background-color:rgba(50%,25%,0%,50%);}"
        "#deferred{--r:12.5;--a:.25;--p:50%;color:rgba(var(--r),25.5,255,var(--a));background-color:rgba(var(--p),25%,0%,50%);}"
        "#namedliteral{color:INDIGO;background-color:ReBeccAPuRple;}"
        "#nameddeferred{--tone:INDIGO;--bg:ReBeccAPuRple;color:var(--tone);background-color:var(--bg);}"
        "#badliteral{color:red;color:rgb(1,2%,3);}"
        "#badvar{--bad:rgb(1,2%,3);color:red;color:var(--bad,blue);}",
        2, "rgb-locale.css", &sheet, nullptr) == AETHER_OK, "compile stylesheet");
    set(resolve_locale);
    const std::string before = std::setlocale(LC_ALL, nullptr);
    aether_style_update_t* updates{};
    const int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
    check(count > 0, "resolve locale pair " + compile_locale + " -> " + resolve_locale);
    unsigned seen = 0;
    for (int i = 0; i < count; ++i) {
        aether_node_t node{};
        aether_computed_style_handle_t handle{};
        check(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "read update");
        for (const char* id : {"literal", "deferred", "badliteral", "badvar", "namedliteral", "nameddeferred"}) {
            if (node != aether_doc_get_element_by_id(doc, id)) continue;
            aether_computed_style_t style{};
            check(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy style");
            if (std::string(id) == "literal" || std::string(id) == "deferred") {
                check(same_color(style.color, {12.5f/255.f,25.5f/255.f,1,.25f}), "decimal RGB compile/resolve " + std::string(id));
                check(same_color(style.background_color, {.5f,.25f,0,.5f}), "percentage compile/resolve " + std::string(id));
            } else if (std::string(id) == "namedliteral" || std::string(id) == "nameddeferred") {
                check(same_color(style.color, {75/255.f,0,130/255.f,1}), "named color compile/resolve " + std::string(id));
                check(same_color(style.background_color, {102/255.f,51/255.f,153/255.f,1}), "named background compile/resolve " + std::string(id));
            } else if (std::string(id) == "badliteral") check(same_color(style.color, {1,0,0,1}), "invalid literal lower survives");
            else check(same_color(style.color, {36/255.f,104/255.f,172/255.f,1}), "invalid computed color inherits");
            aether_computed_style_free(&style); ++seen;
        }
        aether_computed_style_handle_destroy(handle);
    }
    check(seen == 6, "all locale targets inspected");
    check(before == std::setlocale(LC_ALL, nullptr), "pipeline did not change locale");
    aether_style_updates_free(updates, count);
    aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
}
struct Result { bool valid; aether_color_rgba color; };
Result parse(const std::string& input) {
    Result result{};
    result.valid = aether_color_rgba_try_parse(input.c_str(), &result.color);
    return result;
}
}
int main(int argc, char** argv) {
    const bool require_all = argc == 2 && std::string(argv[1]) == "--require-all";
    const std::string original = std::setlocale(LC_ALL, nullptr);
    {
        LocaleGuard restore;
        std::vector<std::string> locales{"C"};
        const char* names[][4] = {
            {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "German_Germany.1252"},
            {"fr_FR.UTF-8", "fr_FR.utf8", "fr_FR", "French_France.1252"},
            {"tr_TR.UTF-8", "tr_TR.utf8", "tr_TR", "Turkish_Turkey.1254"}
        };
        for (const auto& aliases : names) {
            bool found = false;
            for (const char* name : aliases) if (std::setlocale(LC_ALL, name)) {
                locales.emplace_back(name); found = true;
                check(std::string(std::localeconv()->decimal_point) == ",", std::string(name) + " actual comma decimal locale");
                std::printf("AVAILABLE %s decimal_point=%s\n", name, std::localeconv()->decimal_point); break;
            }
            if (!found) std::printf("SKIP unavailable locale family %s\n", aliases[0]);
            if (require_all) check(found, std::string("required locale ") + aliases[0]);
        }
        // The first parse occurs in a non-C locale where one is installed.
        set(locales.back());
        const std::string first_locale = std::setlocale(LC_ALL, nullptr);
        auto first = parse("rgba(127.5,0,255,.5)");
        check(first.valid && same_color(first.color, {.5f,0,1,.5f}), "first parse in caller locale");
        check(first_locale == std::setlocale(LC_ALL, nullptr), "first parse did not change caller locale");
        std::vector<std::string> inputs = {
            "rgba(12.5,25.5,255,.25)", "rgba(50%,25%,0%,50%)", "rgb(1,2%,3)",
            "rgb(0x10,2,3)", "rgba(1,2,3,0x1p-1)", "rgb(1.,2,3)", "rgb(1,2,3/*)",
            "rgba(1,2,3,0,5)", "rgba(1,2,3,50%)", "rgba(10%,20%,30%,.5)",
            "INDIGO", "LIME", "ReBeccAPuRple", "LIGHTSLATEGRAY", "transparent", "notacolor"
        };
        for (const char* n : {"1e-40", "1e-45", "1e-999", "-1e-999", "-1e-40", "+.5",
                              "3.4028234e38", "3.4028235e38", "3.4028236e38", "-3.4028236e38", "1e39", "-1e39"}) {
            inputs.emplace_back("rgba(1,2,3," + std::string(n) + ")");
            inputs.emplace_back("rgba(1,2,3," + std::string(n) + "%)");
            inputs.emplace_back("rgb(" + std::string(n) + ",0,0)");
            inputs.emplace_back("rgb(" + std::string(n) + "%,0%,0%)");
        }
        set("C");
        std::vector<Result> baseline;
        for (const auto& input : inputs) baseline.push_back(parse(input));
        for (const auto& locale : locales) {
            set(locale);
            const std::string before = std::setlocale(LC_ALL, nullptr);
            for (size_t i = 0; i < inputs.size(); ++i) {
                const auto actual = parse(inputs[i]);
                check(actual.valid == baseline[i].valid && same_color(actual.color, baseline[i].color),
                      locale + " bit-exact C result " + inputs[i]);
            }
            check(before == std::setlocale(LC_ALL, nullptr), "direct parsing did not change " + locale);
        }
        for (const auto& a : locales) for (const auto& b : locales) pipeline(a, b);
        std::printf("rgb_locale: %zu locales, %zu compile/resolve pairs\n", locales.size(), locales.size() * locales.size());
    }
    check(original == std::setlocale(LC_ALL, nullptr), "restore original LC_ALL");
    std::printf("rgb_locale: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
