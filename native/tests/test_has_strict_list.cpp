#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

namespace {
int assertions = 0, fixtures = 0, resolutions = 0, failures = 0;
void check(bool ok, const char* message) {
    ++assertions;
    if (!ok) throw std::runtime_error(message);
}
struct Values { float opacity, width, height; uint32_t impact; };
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    std::map<aether_node_t, Values> styles;
    std::map<aether_node_t, Values> changed;
    Fixture(const char* markup, const char* css, int expected_rules) {
        ++fixtures;
        try {
            check(aether_doc_create(&doc) == AETHER_OK, "create document");
            check(aether_doc_parse_html(doc, markup) == AETHER_OK, "parse HTML");
            aether_style_runtime_options_t options{};
            const aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
            check(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
            int64_t sheet = 0; int32_t rules = -1;
            check(aether_style_runtime_add_stylesheet(runtime,
                ".card{opacity:.2;width:10px;height:11px}", 2, "base", &sheet, &rules) == AETHER_OK, "base sheet");
            check(aether_style_runtime_add_stylesheet(runtime, css, 2, "guard", &sheet, &rules) == AETHER_OK, "test sheet");
            check(sheet != 0, "test sheet has a handle even when all selectors reject");
            check(rules == expected_rules, "test sheet rule count (base excluded)");
        } catch (...) { cleanup(); throw; }
    }
    void cleanup() {
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
        runtime = nullptr; doc = nullptr;
    }
    ~Fixture() { cleanup(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    aether_node_t find(const char* name) {
        return aether_doc_get_element_by_id(doc, name);
    }
    int64_t add(const char* css, int expected) {
        int64_t handle = 0; int32_t rules = -1;
        check(aether_style_runtime_add_stylesheet(runtime, css, 2, "extra", &handle, &rules) == AETHER_OK, "add sheet");
        check(handle != 0, "nonzero sheet handle, including zero-rule sheets");
        check(rules == expected, "extra sheet rule count");
        return handle;
    }
    void dimensions(const char* target, float width, float height) {
        const auto& value = styles.at(find(target));
        check(std::fabs(value.width - width) < .0001f, "computed width declaration");
        check(std::fabs(value.height - height) < .0001f, "computed height declaration");
    }
    int resolve(aether_node_t leaf = nullptr) {
        ++resolutions;
        changed.clear();
        aether_style_update_t* updates = nullptr;
        auto root = reinterpret_cast<aether_node_t>(doc);
        // The return value is the output update count; the input count is one.
        int count = leaf ? aether_style_runtime_resolve_state_changes(runtime, root, &leaf, 1, &updates)
                         : aether_style_runtime_resolve_subtree(runtime, root, &updates);
        try {
            check(count >= 0, "resolve status");
            check((count == 0) == (updates == nullptr), "output pointer matches update count");
            for (int i = 0; i < count; ++i) {
                aether_node_t node = nullptr;
                aether_computed_style_handle_t handle = nullptr;
                uint32_t impact = 0;
                const auto status = aether_style_runtime_get_update(updates[i], &node, &handle, &impact);
                if (status != AETHER_OK || !handle) {
                    if (handle) aether_computed_style_handle_destroy(handle);
                    check(false, "get current update");
                }
                aether_computed_style_t style{};
                const auto copied = aether_computed_style_copy(handle, &style);
                aether_computed_style_handle_destroy(handle);
                check(copied == AETHER_OK, "copy current update");
                Values value{style.opacity, style.width.value, style.height.value, impact};
                aether_computed_style_free(&style);
                styles[node] = value; changed[node] = value;
            }
        } catch (...) { aether_style_updates_free(updates, count); throw; }
        aether_style_updates_free(updates, count);
        return count;
    }
    void expect(const char* target, float opacity) {
        auto node = find(target); // Query after Resolve; cached values own only scalar snapshots.
        check(node && styles.count(node) == 1, "fresh target has computed style");
        const auto& s = styles.at(node);
        check(std::fabs(s.opacity - opacity) < .0001f, "computed opacity");
    }
};
// Public ABI only: invalid selectors must neither publish rules nor change styles.
const char* markup = "<body><div id=card class='card safe'><span id=leaf class='ready comma, paren)'><b class=inner></b></span><i id='comma,id' data-v='a,b' class=other></i></div><div id=second class=card></div></body>";
struct Case { const char* name; const char* css; int rules; float opacity; };
const Case cases[] = {
        {"valid-single", ".card:has(.ready){opacity:.8}", 1, .8f},
        {"valid-list", ".card:has(.missing,.ready){opacity:.8}", 1, .8f},
        {"unknown-last", ".card:has(.ready,:aether-unknown){opacity:.8}", 0, .2f},
        {"unknown-first", ".card:has(:aether-unknown,.ready){opacity:.8}", 0, .2f},
        {"empty-last", ".card:has(.ready,){opacity:.8}", 0, .2f},
        {"empty-first", ".card:has(,.ready){opacity:.8}", 0, .2f},
        {"empty-middle", ".card:has(.ready,,.other){opacity:.8}", 0, .2f},
        {"bad-nth", ".card:has(.ready,:nth-child(bogus)){opacity:.8}", 0, .2f},
        {"dangling-combinator", ".card:has(.ready,.other >){opacity:.8}", 0, .2f},
        {"inner-is-forgiving", ".card:has(:is(.ready,:aether-unknown)){opacity:.8}", 1, .8f},
        {"is-forgiving", ".card:is(.safe,:aether-unknown){opacity:.8}", 1, .8f},
        {"where-forgiving", ".card:where(.safe,:aether-unknown){opacity:.8}", 1, .8f},
        {"not-remains-strict", ".card:not(.missing,:aether-unknown){opacity:.8}", 0, .2f},
        {"continuation", ".card:has(.ready,:aether-unknown){opacity:.8}.card{opacity:.4}", 1, .4f},
        {"outer-is-drop-invalid", ".card:is(:has(.ready,:aether-unknown),.missing){opacity:.8}", 1, .2f},
        {"discarded-specificity", ".card:is(:has(#missing,:aether-unknown),.safe){opacity:.8}.card.safe{opacity:.4}", 2, .4f},
        {"comments-invalid-branch", ".card:has(.ready,/**/){opacity:.8}", 0, .2f},
        {"quoted-comma", ".card:has([data-v='a,b'],.missing){opacity:.8}", 1, .8f},

    {"valid-no-match", ".card:has(.absent){opacity:.8}", 1, .2f},
    {"valid-outside-anchor", ".card:has(body .ready){opacity:.8}", 1, .2f},
    {"inner-where-forgiving", ".card:has(:where(.ready,:unknown)){opacity:.8}", 1, .8f},
    {"inner-not-strict", ".card:has(:not(.missing,:unknown),.ready){opacity:.8}", 0, .2f},
    {"outer-not-strict", ".card:not(:has(.ready,:unknown),.missing){opacity:.8}", 0, .2f},
    {"outer-where-drop-invalid", ".card:where(:has(.ready,:unknown),.missing){opacity:.8}", 1, .2f},
    {"outer-is-fallback", ".card:is(:has(.ready,:unknown),.safe){opacity:.8}", 1, .8f},
    {"outer-where-fallback", ".card:where(:has(.ready,:unknown),.safe){opacity:.8}", 1, .8f},
    {"is-all-invalid", ".card:is(:has(.ready,:unknown)){opacity:.8}", 1, .2f},
    {"where-all-invalid", ".card:where(:has(.ready,:unknown)){opacity:.8}", 1, .2f},
    {"has-empty", ".card:has(){opacity:.8}", 0, .2f},
    {"inner-is-empty", ".card:has(:is()){opacity:.8}", 1, .2f},
    {"inner-where-empty", ".card:has(:where()){opacity:.8}", 1, .2f},
    {"nested-has-existing-subset", ".card:has(.ready:has(.inner)){opacity:.8}", 1, .8f},
    {"internal-child", ".card:has(.ready > .inner){opacity:.8}", 1, .8f},
    {"internal-adjacent", ".card:has(.ready + .other){opacity:.8}", 1, .8f},
    {"internal-sibling", ".card:has(.ready ~ .other){opacity:.8}", 1, .8f},
    {"top-level-recovery", ".card:has(.ready,:unknown),.safe{opacity:.8}", 1, .8f},
    {"max-nonmatching-specificity", ".card:has(.ready,#never){opacity:.8}.card.safe{opacity:.4}", 2, .8f},
    {"where-zero-specificity", ".card:where(:has(.ready,#never)){opacity:.8}.card.safe{opacity:.4}", 2, .4f},
    {"where-discards-specificity", ".card:where(:has(#missing:hover,:unknown),.safe){opacity:.8}.card.safe{opacity:.4}", 2, .4f},
    {"inner-discards-specificity", ".card:has(:is(#bad:hover:unknown,.ready)){opacity:.8}.card.safe{opacity:.4}", 2, .4f},
    {"tie-later-wins", ".card:has(.ready){opacity:.8}.card.safe{opacity:.4}", 2, .4f},
    {"tie-reverse-order", ".card.safe{opacity:.4}.card:has(.ready){opacity:.8}", 2, .8f},
    {"comment-delimiters", ".card:has(.ready/*, ) ] {*/,.absent){opacity:.8}", 1, .8f},
    {"escaped-comma", R"(.card:has(.comma\,,.absent){opacity:.8})", 1, .8f},
    {"escaped-id-comma", R"(.card:has(#comma\,id,.absent){opacity:.8})", 1, .8f},
    {"escaped-paren", R"(.card:has(.paren\),.absent){opacity:.8})", 1, .8f},
};
template<class Action> void run(const std::string& name, Action action) {
    try { action(); std::printf("HAS PASS %s\n", name.c_str()); }
    catch (const std::exception& e) { ++failures; std::fprintf(stderr, "HAS FAIL %s: %s\n", name.c_str(), e.what()); }
}
void one(const std::string& css, int rules, float opacity) {
    Fixture f(markup, css.c_str(), rules); f.resolve(); f.expect("card", opacity);
    check(f.resolve() == 0, "stable repeat resolution is empty");
    f.expect("card", opacity);
}
void matrices() {
    // All malformed branches are balanced at stylesheet level, so they test
    // selector-list acceptance rather than unrelated stylesheet recovery.
    const char* bad[] = {":unknown", ":nth-child(bogus)", ".other >", "", "  ", "/**/",
                         ".bad\\\n", ".bad\\\r", ".bad\\\f", ".bad\\\r\n",
                         "[data-v=]", ".ready !", "> .ready", "+ .ready", "~ .ready"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        for (int pos = 0; pos != 3; ++pos) {
            std::string args = pos == 0 ? std::string(bad[i]) + ",.ready,.other" :
                               pos == 1 ? std::string(".ready,") + bad[i] + ",.other" :
                                          std::string(".ready,.other,") + bad[i];
            run("strict-matrix-" + std::to_string(i) + "-" + std::to_string(pos), [&] {
                one(".card:has(" + args + "){opacity:.8}", 0, .2f);
            });
            for (const char* outer : {"is", "where"}) {
                run(std::string("inner-") + outer + "-recovery-" + std::to_string(i) + "-" + std::to_string(pos), [&] {
                    one(std::string(".card:has(:") + outer + "(" + args + ")){opacity:.8}", 1, .8f);
                });
                run(std::string(outer) + "-recovery-" + std::to_string(i) + "-" + std::to_string(pos), [&] {
                    one(std::string(".card:") + outer + "(:has(" + args + "),.safe){opacity:.8}", 1, .8f);
                });
            }
        }
    }
    for (const char* spelling : {"has", "HAS", "hAs", R"(\68 as)", R"(h\61 s)", R"(ha\73)"}) {
        run(std::string("name-valid-") + spelling, [&] { one(std::string(".card:") + spelling + "(.ready){opacity:.8}", 1, .8f); });
        run(std::string("name-invalid-") + spelling, [&] { one(std::string(".card:") + spelling + "(.ready,:unknown){opacity:.8}", 0, .2f); });
    }
}
void compiler() {
    const char* bad = ".card:has(.ready,:unknown){opacity:.99;width:999px;height:999px}";
    const std::string first = ".card{opacity:.3;width:30px;height:31px}";
    const std::string last = ".card{opacity:.4;width:40px;height:41px}";
    for (int position = 0; position < 3; ++position) {
        const auto css = position == 0 ? bad + first + last : position == 1 ? first + bad + last : first + last + bad;
        Fixture f(markup, css.c_str(), 2); f.resolve(); f.expect("card", .4f); f.dimensions("card", 40, 41);
    }
    // Rule counts include inactive-media rules; matching remains environment dependent.
    Fixture media(markup, (first + "@media (min-width: 700px){" + bad + last +
        "}@media (max-width: 600px){" + bad + ".card{opacity:.9;width:90px;height:91px}}" + bad).c_str(), 3);
    media.resolve(); media.expect("card", .4f); media.dimensions("card", 40, 41);
    Fixture inactive(markup, (first + "@media (max-width: 600px){" + bad + last + "}").c_str(), 2);
    inactive.resolve(); inactive.expect("card", .3f); inactive.dimensions("card", 30, 31);
    Fixture important(markup, (std::string(bad) + ".card{opacity:.4!important}.card:has(.ready){opacity:.8}").c_str(), 2);
    important.resolve(); important.expect("card", .4f);
}
void lifecycle() {
    Fixture f(markup, ".card:has(.ready){opacity:.6}", 1);
    f.resolve(); f.expect("card", .6f);
    const char* invalid = ".card:has(.ready,:unknown){opacity:.99}";
    auto empty = f.add(invalid, 0); auto valid = f.add(".card:has(.ready){opacity:.8}", 1);
    check(empty != valid, "distinct concurrent sheet handles");
    f.resolve(); f.expect("card", .8f); check(f.resolve() == 0, "stable added sheets");
    check(aether_style_runtime_remove_stylesheet(f.runtime, empty), "remove zero-rule sheet succeeds");
    check(!aether_style_runtime_remove_stylesheet(f.runtime, empty), "removed zero-rule handle no longer live");
    f.resolve(); f.expect("card", .8f); check(f.resolve() == 0, "stable after cache invalidation");
    check(aether_style_runtime_remove_stylesheet(f.runtime, valid), "remove valid sheet");
    f.resolve(); f.expect("card", .6f);
    for (int i = 0; i < 3; ++i) {
        empty = f.add(invalid, 0); f.resolve(); f.expect("card", .6f);
        valid = f.add(".card:has(.ready){opacity:.8}", 1); f.resolve(); f.expect("card", .8f);
        check(aether_style_runtime_remove_stylesheet(f.runtime, valid), "remove readded valid sheet");
        f.resolve(); f.expect("card", .6f);
        check(aether_style_runtime_remove_stylesheet(f.runtime, empty), "remove readded empty sheet");
        f.resolve(); f.expect("card", .6f); check(f.resolve() == 0, "stable after repeated removal");
    }
}
void mutations() {
    Fixture f(markup, ".card:has(.ready[data-live=yes]){opacity:.6}"
        ".card:has(.ready[data-live=yes]:hover){opacity:.8}"
        ".card:has(#leaf:hover,:unknown){opacity:.99;width:999px}", 2);
    f.resolve(); f.expect("card", .2f);
    auto leaf = f.find("leaf"); auto card = f.find("card"); auto second = f.find("second");
    check(leaf && card && second, "mutation targets exist");
    for (int i = 0; i < 3; ++i) {
        check(aether_element_set_attribute(leaf, "data-live", "yes") == AETHER_OK, "set matching attribute");
        f.resolve(); f.expect("card", .6f);
        check(aether_element_set_attribute(leaf, "class", "absent") == AETHER_OK, "remove matching class");
        f.resolve(); f.expect("card", .2f);
        check(aether_element_set_attribute(leaf, "class", "ready") == AETHER_OK, "restore matching class");
        f.resolve(); f.expect("card", .6f);
        check(aether_node_append_child(second, leaf) == AETHER_OK, "move descendant to second host");
        f.resolve(); f.expect("card", .2f); f.expect("second", .6f);
        check(aether_node_get_parent(leaf) == second, "reparent retains node identity");
        for (int on : {1, 0, 1, 0}) {
            aether_element_set_state(leaf, on ? AETHER_STATE_HOVER : AETHER_STATE_NONE);
            check(f.resolve(leaf) > 0, "hover publishes updates through state-change API");
            f.expect("card", .2f); f.expect("second", on ? .8f : .6f);
            check(f.changed.count(second) == 1, "descendant hover invalidates host");
            check(f.changed.at(second).impact == 0x0C, "opacity has composite/hittest impact only");
            check(f.resolve(leaf) == 0, "repeated hover state is no-op");
        }
        check(aether_node_append_child(card, leaf) == AETHER_OK, "return descendant");
        f.resolve(); f.expect("card", .6f); f.expect("second", .2f);
        check(aether_element_set_attribute(leaf, "data-live", "no") == AETHER_OK, "remove matching attribute");
        f.resolve(); f.expect("card", .2f); f.dimensions("card", 10, 11); f.dimensions("second", 10, 11);
    }
}
}
int main() {
    for (const auto& c : cases) run(c.name, [&] { one(c.css, c.rules, c.opacity); });
    matrices();
    run("compiler-declarations-order-media", compiler);
    run("zero-rule-sheet-lifecycle", lifecycle);
    run("class-attribute-reparent-hover", mutations);
    std::printf("HAS STATIC=%zu FIXTURES=%d RESOLUTIONS=%d ASSERTIONS=%d FAILURES=%d\n",
                sizeof(cases) / sizeof(cases[0]), fixtures, resolutions, assertions, failures);
    return failures ? 1 : 0;
}
