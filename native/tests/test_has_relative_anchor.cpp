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
                ".subject,.leaf{opacity:.25;width:10px;height:11px}", 2, "base", &sheet, &rules) == AETHER_OK, "base sheet");
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
    aether_node_t find(const char* name, aether_node_t node = nullptr) {
        if (!node) node = reinterpret_cast<aether_node_t>(doc);
        if (aether_node_get_type(node) == AETHER_NODE_ELEMENT) {
            char* id = aether_element_get_attribute(node, "id");
            bool hit = id && std::strcmp(id, name) == 0;
            aether_string_free(id);
            if (hit || aether_element_has_class(node, name)) return node;
        }
        for (int i = 0; i < aether_node_get_child_count(node); ++i)
            if (auto* found = find(name, aether_node_get_child(node, i))) return found;
        return nullptr;
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
        check(s.width == 10 && s.height == 11, "computed dimensions 10x11");
    }
};
const char* base_markup = "<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>";
const char* plus_markup = "<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid' id='mid' data-k='x'><i class='leaf'></i></div></div></section></body></html>";
struct Case { const char *name, *markup, *target, *css; int rules; float expected; };
const Case cases[] = {
    {"G01","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid' id='mid' data-k='x'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid#mid[data-k=x] > .leaf){opacity:.75}",1,0.75f},
    {"G02","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.subject > .mid){opacity:.75}",1,0.25f},
    {"G03","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid .leaf){opacity:.75}",1,0.75f},
    {"G04","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid .missing){opacity:.75}",1,0.25f},
    {"G05","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><span class='pre'></span> <i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.pre + .leaf){opacity:.75}",1,0.75f},
    {"G06","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><span class='pre'></span> <i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.pre ~ .leaf){opacity:.75}",1,0.75f},
    {"G07","<!DOCTYPE html><html><head></head><body><span class='pre'></span><section class='subject' id='subject'><i class='leaf' id='leaf'></i></section></body></html>","subject",".subject:has(.pre + .subject .leaf){opacity:.75}",1,0.25f},
    {"G08a","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(body .leaf, .mid > .leaf){opacity:.75}",1,0.75f},
    {"G08b","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid > .leaf, body .leaf){opacity:.75}",1,0.75f},
    {"G09","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(body .leaf, .subject .leaf){opacity:.75}",1,0.25f},
    {"G10","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid :is(body .leaf)){opacity:.75}",1,0.75f},
    {"G11","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.leaf:not(body .leaf)){opacity:.75}",1,0.25f},
    {"G13","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid:has(.leaf)){opacity:.75}",1,0.75f},
    {"G14","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid:has(.mid .leaf)){opacity:.75}",1,0.25f},
    {"G12","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:not(:has(body .leaf)){opacity:.75}",1,0.75f},
    {"G15","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.mid:has(.leaf)):has(.leaf){opacity:.75}",1,0.75f},
    {"G16a","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","leaf","body .leaf{opacity:.75}",1,0.75f},
    {"G16b","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","leaf",".subject .leaf{opacity:.75}",1,0.75f},
    {"G17a","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(> .leaf){opacity:.75}",0,0.25f},
    {"G17b","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(+ .leaf){opacity:.75}",0,0.25f},
    {"G17c","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(~ .leaf){opacity:.75}",0,0.25f},
    {"G17d","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(){opacity:.75}",0,0.25f},
    {"G17e","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(:unknown){opacity:.75}",0,0.25f},
    {"G17f","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(> .leaf, .leaf){opacity:.75}",1,0.75f},
    {"G17g","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(:unknown, .leaf){opacity:.75}",1,0.75f},
    {"G17h","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(:unknown, body .leaf){opacity:.75}",1,0.25f},
    {"G17i","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",":-internal-relative-anchor{opacity:.75}",0,0.25f},
    {"G18a","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject pick' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.leaf){opacity:.75}.subject.pick{opacity:.4}",2,0.4f},
    {"G18b","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject pick' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject.pick{opacity:.4}.subject:has(.leaf){opacity:.75}",2,0.75f},
    {"G18c","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject pick' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(:where(.leaf)){opacity:.75}.subject{opacity:.4}",2,0.4f},
    {"G18d","<!DOCTYPE html><html><head></head><body><section class='outer'><div class='subject pick' id='subject'><div class='mid'><i class='leaf'></i></div></div></section></body></html>","subject",".subject:has(.leaf,#never){opacity:.75}.subject.pick{opacity:.4}",2,0.75f},
};
template<class Action> void run(const char* name, Action action) {
    try { action(); std::printf("PASS %s\n", name); }
    catch (const std::exception& ex) { ++failures; std::fprintf(stderr, "FAIL %s: %s\n", name, ex.what()); }
}
void attributes(bool functional) {
    Fixture f(plus_markup, functional ? ".subject:has(:is([data-k=yes] .leaf)){opacity:.75}"
                                     : ".subject:has([data-k=yes] .leaf){opacity:.75}", 1);
    auto node = functional ? aether_doc_get_body(f.doc) : f.find("mid");
    check(node != nullptr, "attribute target");
    check(aether_element_set_attribute(node, "data-k", "x") == AETHER_OK, "initial attribute");
    f.resolve(); f.expect("subject", .25f);
    check(aether_element_set_attribute(node, "data-k", "yes") == AETHER_OK, "attribute yes");
    f.resolve(); f.expect("subject", .75f);
    check(aether_element_set_attribute(node, "data-k", "x") == AETHER_OK, "attribute x");
    f.resolve(); f.expect("subject", .25f);
}
void reparent() {
    Fixture f("<!DOCTYPE html><html><head></head><body><div class='subject' id='A'><i class='leaf'></i></div><div class='subject' id='B'></div></body></html>", ".subject:has(.leaf){opacity:.75}", 1);
    f.resolve(); f.expect("A", .75f); f.expect("B", .25f);
    auto leaf = f.find("leaf"); auto a = f.find("A"); auto b = f.find("B");
    check(leaf && a && b, "stable nodes exist");
    check(aether_node_append_child(b, leaf) == AETHER_OK, "move leaf to B");
    f.resolve();
    check(f.find("leaf") == leaf && aether_node_get_parent(leaf) == b, "same leaf in B");
    f.expect("A", .25f); f.expect("B", .75f);
    check(aether_node_append_child(a, leaf) == AETHER_OK, "return leaf to A");
    f.resolve();
    check(f.find("leaf") == leaf && aether_node_get_parent(leaf) == a, "same leaf in A");
    f.expect("A", .75f); f.expect("B", .25f);
}
void state() {
    Fixture f(base_markup, ".subject:has(.leaf:hover){opacity:.75}", 1);
    f.resolve(); f.expect("subject", .25f);
    for (int on : {1, 0}) {
        auto leaf = f.find("leaf"); check(leaf != nullptr, "fresh leaf");
        aether_element_set_state(leaf, on ? AETHER_STATE_HOVER : AETHER_STATE_NONE);
        check(f.resolve(leaf) > 0, "state change publishes updates");
        f.expect("subject", on ? .75f : .25f);
        auto subject = f.find("subject");
        check(f.changed.count(subject) == 1, "nested state updates subject with only leaf input");
        check(f.changed.at(subject).impact == 0x0C, "subject opacity impact COMPOSITE|HITTEST");
        for (const auto& [node, value] : f.changed)
            check((value.impact & (1u | 16u)) == 0, "state opacity has no layout/text impact");
    }
    auto leaf = f.find("leaf");
    aether_element_set_state(leaf, AETHER_STATE_NONE);
    check(f.resolve(leaf) == 0, "same off with nonempty changed leaf list is no-op");
    check(f.changed.empty(), "no-op has no updates");
    f.expect("subject", .25f);
}
}
int main() {
    static_assert(sizeof(cases) / sizeof(cases[0]) == 31);
    for (const auto& c : cases) run(c.name, [&] {
        Fixture f(c.markup, c.css, c.rules); f.resolve(); f.expect(c.target, c.expected);
    });
    run("G19a", [] { attributes(false); });
    run("G19b", [] { attributes(true); });
    run("G20", reparent); run("G21", state);
    std::printf("FAMILIES=21 STATIC=31 DYNAMIC=4 FIXTURES=%d RESOLUTIONS=%d ASSERTIONS=%d FAILURES=%d\n", fixtures, resolutions, assertions, failures);
    return failures ? 1 : 0;
}
