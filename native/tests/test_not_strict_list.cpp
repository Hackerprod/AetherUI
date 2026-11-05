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
                ".item{opacity:.2}", 2, "base", &sheet, &rules) == AETHER_OK, "base sheet");
            check(aether_style_runtime_add_stylesheet(runtime, css, 2, "guard", &sheet, &rules) == AETHER_OK, "test sheet");
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
struct Case { const char *name, *markup, *css; int rules; float expected[3]; };
const Case cases[] = {
    {"valid-single","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip){opacity:.8}",1,{0.8f,0.2f,0.8f}},
    {"valid-list","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip,.omit){opacity:.8}",1,{0.8f,0.2f,0.2f}},
    {"unknown-last","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip,:aether-unknown){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"unknown-first","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(:aether-unknown,.skip){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"bad-nth","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip,:nth-child(bogus)){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"empty-last","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip,){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"empty-first","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(,.skip){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"empty-middle","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip,,.omit){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"dangling-combinator","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip,.omit >){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"is-forgiving","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:is(.keep,:aether-unknown){opacity:.8}",1,{0.8f,0.2f,0.2f}},
    {"where-forgiving","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:where(.keep,:aether-unknown){opacity:.8}",1,{0.8f,0.2f,0.2f}},
    {"valid-not-containing-forgiving-is","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(:is(.skip,:aether-unknown)){opacity:.8}",1,{0.8f,0.2f,0.8f}},
    {"invalid-not-inside-forgiving-is","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:is(:not(.skip,:aether-unknown),.keep){opacity:.8}",1,{0.8f,0.2f,0.2f}},
    {"D01","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:where(:not(.skip,:unknown),.keep){opacity:.8}",1,{0.8f,0.2f,0.2f}},
    {"D02","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:is(.keep,:not(:unknown,.skip)){opacity:.8}",1,{0.8f,0.2f,0.2f}},
    {"D03","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(:not(.skip,:unknown)){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"D04","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(:where(.skip,:unknown)){opacity:.8}",1,{0.8f,0.2f,0.8f}},
    {"D05","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip,:is(.omit,:unknown)){opacity:.8}",1,{0.8f,0.2f,0.2f}},
    {"D06","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip,   ){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"D07","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"D08","<!doctype html><html><head></head><body><div id='parent'><div id='a' data-k='a,b' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not([data-k=\"a,b\"],.skip){opacity:.8}",1,{0.2f,0.2f,0.8f}},
    {"D09","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip),.item:not(.omit){opacity:.8}",2,{0.8f,0.8f,0.8f}},
    {"D10","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip,:unknown),#b{opacity:.8}",1,{0.2f,0.8f,0.2f}},
    {"D11","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:is(#never:hover:not(.skip,:unknown),.keep){opacity:.8}.item.keep{opacity:.4}",2,{0.4f,0.2f,0.2f}},
    {"D12","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(#never,.skip){opacity:.8}.item.keep{opacity:.4}",2,{0.8f,0.2f,0.8f}},
    {"D13","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:where(:not(#never,.skip)){opacity:.8}.item.keep{opacity:.4}",2,{0.4f,0.2f,0.8f}},
    {"D14","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(:hover){opacity:.8}",1,{0.8f,0.8f,0.8f}},
    {"D15","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:is(:not(.skip,:unknown)){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"D16","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(:is(:not(.skip,:unknown))){opacity:.8}",0,{0.2f,0.2f,0.2f}},
    {"continuation","<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item keep'></div><div id='b' class='item skip'></div><div id='c' class='item omit'></div></div></body></html>",".item:not(.skip,:unknown){opacity:.8}#b{opacity:.6}",1,{0.2f,0.6f,0.2f}},
};
}
int main() {
    static_assert(sizeof(cases) / sizeof(cases[0]) == 30);
    for (const auto& c : cases) {
        try {
            Fixture f(c.markup, c.css, c.rules);
            f.resolve();
            f.expect("a", c.expected[0]); f.expect("b", c.expected[1]); f.expect("c", c.expected[2]);
            if (std::strcmp(c.name, "D14") == 0) {
                for (int on : {1, 0}) {
                    auto b = f.find("b"); check(b != nullptr, "fresh state target");
                    aether_element_set_state(b, on ? AETHER_STATE_HOVER : AETHER_STATE_NONE);
                    check(f.resolve(b) > 0, "state change produces updates");
                    f.expect("a", .8f); f.expect("b", on ? .2f : .8f); f.expect("c", .8f);
                }
            }
            std::printf("NOT PASS %s rules=%d opacity=[%.1f,%.1f,%.1f]\n", c.name, c.rules, c.expected[0], c.expected[1], c.expected[2]);
        } catch (const std::exception& e) { ++failures; std::fprintf(stderr, "NOT FAIL %s: %s\n", c.name, e.what()); }
    }
    std::printf("NOT CASES=30 FIXTURES=%d RESOLUTIONS=%d ASSERTIONS=%d FAILURES=%d\n", fixtures, resolutions, assertions, failures);
    return failures ? 1 : 0;
}
