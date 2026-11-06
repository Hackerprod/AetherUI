// Public ABI only. Exercise real class-anchor extraction and explicit subtree
// resolution after mutations; state-change resolution is not attribute invalidation.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
namespace {
unsigned checks = 0, failures = 0;
void need(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "HARNESS %s\n", why); std::exit(2); }
}
void check(bool ok, const char* why) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", why); }
}
bool near(float a, float b) { return std::fabs(a - b) < .001f; }
std::string take(char* text) { std::string result = text ? text : ""; aether_string_free(text); return result; }
struct Style { float width, height, opacity; };
Style copy(aether_computed_style_handle_t handle) {
    aether_computed_style_t s{};
    need(aether_computed_style_copy(handle, &s) == AETHER_OK, "copy style");
    Style result{s.width.value, s.height.value, s.opacity};
    aether_computed_style_free(&s); return result;
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_node_t root = nullptr;
    int64_t sheet = 0;
    int32_t rules = 0;
    std::map<aether_node_t, aether_computed_style_handle_t> latest;
    std::vector<aether_computed_style_handle_t> retained;
    std::vector<std::string> generated;
    Fixture(const std::string& html, const std::string& css) {
        need(aether_doc_create(&doc) == AETHER_OK, "document");
        need(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "markup");
        root = aether_doc_get_document_element(doc);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        need(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "runtime");
        add(css);
    }
    ~Fixture() {
        for (auto handle : retained) aether_computed_style_handle_destroy(handle);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    void add(const std::string& css) {
        need(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 1, "class-tokens", &sheet, &rules) == AETHER_OK, "stylesheet");
    }
    aether_node_t id(const char* name) {
        auto node = aether_doc_get_element_by_id(doc, name); need(node != nullptr, "ID"); return node;
    }
    void set(aether_node_t node, const std::string& value) {
        need(aether_element_set_attribute(node, "class", value.c_str()) == AETHER_OK, "set class");
        check(take(aether_element_get_attribute(node, "class")) == value, "set preserves source bytes");
    }
    void resolve(aether_node_t subtree = nullptr) {
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime, subtree ? subtree : root, &updates);
        need(count >= 0, "resolve subtree"); generated.clear();
        for (int i = 0; i < count; ++i) {
            aether_node_t node = nullptr; aether_computed_style_handle_t handle = nullptr;
            need(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "update");
            const auto parent = aether_node_get_parent(node);
            bool authored = true;
            if (parent) {
                authored = false;
                for (int j = 0; j < aether_node_get_child_count(parent); ++j)
                    authored |= aether_node_get_child(parent, j) == node;
            }
            if (authored) latest[node] = handle;
            else generated.push_back(take(aether_node_get_text_content(aether_node_get_child(node, 0))));
            retained.push_back(handle);
        }
        aether_style_updates_free(updates, count);
    }
    Style value(aether_node_t node) { need(latest.count(node) != 0, "latest snapshot"); return copy(latest.at(node)); }
};
void probe(const std::string& value, const std::string& selector, bool expected) {
    Fixture f("<body><div id='t'></div></body>", "*{opacity:.2}" + selector + "{opacity:.8}");
    check(f.rules == 2, "both probe rules accepted");
    auto target = f.id("t"); f.set(target, value); f.resolve();
    check(near(f.value(target).opacity, expected ? .8f : .2f), selector.c_str());
    f.resolve();
    check(near(f.value(target).opacity, expected ? .8f : .2f), "repeated candidate extraction stable");
    check(take(aether_element_get_attribute(target, "class")) == value, "resolution preserves class bytes");
}
void boundaries() {
    for (char sep : std::string(" \t\n\f\r")) {
        const std::string classes = std::string(2, sep) + "a" + sep + sep + "B" + sep;
        for (const char* selector : {".a", ".B", "#t.a", "#t.B", "[class~=a]", "[class~=B]"}) probe(classes, selector, true);
        probe(classes, ".b", false);
    }
    struct Row { const char* bytes; const char* escape; };
    for (const auto& row : {Row{"\v", "b"}, Row{"\xc2\xa0", "a0"}, Row{"\xc2\x85", "85"}, Row{"\xe2\x80\x83", "2003"}}) {
        const std::string value = std::string("a") + row.bytes + "B";
        const std::string token = std::string("a\\") + row.escape + " B";
        for (const auto& selector : {"." + token, "#t." + token, "[class~=\"" + token + "\"]", ":is(." + token + ")"}) probe(value, selector, true);
        for (const char* selector : {".a", ".B", "#t.a", "#t.B", "[class~=a]", "[class~=B]"}) probe(value, selector, false);
    }
    for (const auto& value : {std::string(""), std::string(" \t\n\f\r")})
        for (const char* selector : {".a", "#t.a", "[class~=a]", "[class~=\"\"]", "[class~=\" \" i]"}) probe(value, selector, false);
    probe("a\vB a\vB", R"(.a\b B)", true);
    probe("I", ".i", false); probe("I", "[class~=i i]", true); probe("I", "[class~=i s]", false);
    const std::string large(8192, 'x');
    probe("\t" + large + "\r", "." + large, true);
}
void lifecycle() {
    const std::string css =
        "div{width:10px;height:10px;opacity:.1}#outer{opacity:.1}#next{width:10px;height:10px}"
        ".a\\b B{opacity:.8}#t.a\\b B{width:31px}#t:is(.a\\b B):where(.a\\b B):not(.a){height:41px}"
        "#outer:has(.a\\b B){opacity:.7}#outer > .a\\b B + #next{width:51px}#outer .a\\b B ~ #next{height:61px}"
        "#t.a\\b B::before{content:'BEFORE'}#t:is(.a\\b B)::after{content:'AFTER'}";
    Fixture f("<body><main id='outer'><div id='t' class='a B'>Original</div><aside id='next'></aside></main></body>", css);
    check(f.rules == 11, "lifecycle rules accepted");
    auto host = f.id("t"), outer = f.id("outer"), next = f.id("next");
    f.resolve(); const auto initial = f.latest.at(host);
    for (const auto& value : {std::string("a\vB"), std::string(""), std::string("a B"), std::string("a\vB"), std::string("a\vB a\vB"), std::string("a B")}) {
        f.set(host, value);
        // Include the :has ancestor and affected siblings, not just the host.
        f.resolve(outer);
        const bool token = value.find('\v') != std::string::npos;
        const auto style = f.value(host);
        check(near(style.width, token ? 31 : 10) && near(style.height, token ? 41 : 10) && near(style.opacity, token ? .8f : .1f), "mutated class bucket, ID anchor, nested pseudos");
        check(near(f.value(outer).opacity, token ? .7f : .1f), "descendant mutation updates :has ancestor");
        check(near(f.value(next).width, token ? 51 : 10) && near(f.value(next).height, token ? 61 : 10), "child/descendant/adjacent/general sibling matching");
        check(f.generated.size() == (token ? 2u : 0u), "before/after lifecycle reads current host classes");
        for (const auto& text : f.generated) check(text == "BEFORE" || text == "AFTER", "generated content preserved");
        check(aether_node_get_child_count(host) == 1 && take(aether_node_get_text_content(aether_node_get_child(host, 0))) == "Original", "generated content preserves logical DOM");
        check(take(aether_element_get_attribute(host, "class")) == value, "mutation resolution preserves all class bytes");
        check(near(copy(initial).width, 10) && near(copy(initial).opacity, .1f), "old owned snapshot is unchanged");
        f.resolve(outer);
        check(near(f.value(host).width, style.width) && near(f.value(host).opacity, style.opacity), "stable re-resolution");
    }
    f.set(host, "a\vB"); f.resolve();
    const auto matched = f.latest.at(host);
    need(aether_style_runtime_remove_stylesheet(f.runtime, f.sheet), "remove sheet"); f.resolve();
    check(near(f.value(host).opacity, 1) && f.generated.empty(), "sheet removal clears class rule and pseudos");
    // Replace storage while old computed snapshots remain alive; no class view may survive a resolve.
    f.set(host, std::string(16384, 'z')); f.resolve();
    f.set(host, "a\vB"); f.add(css); f.resolve();
    check(f.rules == 11 && near(f.value(host).width, 31) && near(f.value(host).opacity, .8f) && f.generated.size() == 2, "sheet readd restores class candidates and pseudos");
    check(near(copy(matched).width, 31) && near(copy(matched).opacity, .8f), "matched snapshot survives storage replacement and index rebuild");
}
}
int main() {
    boundaries(); lifecycle();
    std::printf("class_token_runtime: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
