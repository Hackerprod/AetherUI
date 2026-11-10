// Same harness must be compiled against each revision's matcher and core.
// Timing is descriptive; deterministic visit counts are the complexity evidence.
#include <aether/aether.h>
#include "../src/dom/dom_types.h"
#include "../src/style_runtime/style_runtime_types.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace {
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool element(const aether_node_s* n) {
    return n && n->type == AETHER_NODE_ELEMENT && !n->is_generated && !n->anonymous_flex_owner;
}
// Models are used ONLY for untimed visit counts and equivalence. One visit is
// one examined entry in parent.children, including skipped text nodes.
bool original(aether_node_s* n, bool only, uint64_t& visits) {
    auto* p = n->parent;
    require(p != nullptr, "count model requires attached subject");
    int index = 0; bool found = false;
    for (auto* s : p->children) {
        ++visits;
        if (!element(s)) continue;
        if (s == n) { found = true; break; }
        ++index;
    }
    if (!found || index != 0) return false;
    if (!only) return true;
    int count = 0;
    for (auto* s : p->children) { ++visits; if (element(s)) ++count; }
    return count == 1;
}
bool candidate(aether_node_s* n, bool only, uint64_t& visits) {
    auto* p = n->parent;
    require(p != nullptr, "count model requires attached subject");
    bool first = false;
    for (auto* s : p->children) {
        ++visits;
        if (!element(s)) continue;
        first = s == n; break;
    }
    if (!first || !only) return first;
    for (auto it = p->children.rbegin(); it != p->children.rend(); ++it) {
        ++visits;
        if (element(*it)) return *it == n;
    }
    return false;
}
struct Fixture {
    aether_doc_t doc{};
    aether_node_t root{};
    aether_style_runtime_t runtime{};
    std::vector<aether_node_t> subjects;
    std::unordered_map<aether_node_t, size_t> indices;
    std::unordered_map<aether_node_t, size_t> controls;
    compiled_selector_s selector;
    size_t text_count = 0;
    Fixture(const std::string& shape, int n, bool only) {
        const int elements = shape == "singleton-text-margins" ? 1 : n;
        std::string html = "<body><section id='fixture'>";
        auto text = [&](int count) {
            // Skipped processing instructions separate distinct text tokens.
            for (int i = 0; i < count; ++i) html += "x<?separator?>";
            text_count += count;
        };
        if (shape == "constant-text-prefix") text(8);
        if (shape == "proportional-text-prefix" || shape == "singleton-text-margins") text(n);
        for (int i = 0; i < elements; ++i) {
            if (shape == "interleaved-text") text(1);
            html += "<i class='item'></i>";
        }
        if (shape == "singleton-text-margins") text(n);
        html += "</section></body>";
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse document");
        root = aether_doc_get_element_by_id(doc, "fixture");
        require(root != nullptr, "fixture root");
        size_t actual_text = 0;
        for (auto* child : root->children) {
            if (element(child)) { indices.emplace(child, subjects.size()); subjects.push_back(child); }
            else { require(child->type == AETHER_NODE_TEXT, "unexpected non-text child"); ++actual_text; }
        }
        require(subjects.size() == size_t(elements) && actual_text == text_count, "fixture shape/text tokens");
        std::vector<aether_node_t> todo{doc};
        while (!todo.empty()) {
            auto* node = todo.back(); todo.pop_back();
            if (element(node) && !indices.count(node)) controls.emplace(node, controls.size());
            for (auto* child : node->children) todo.push_back(child);
        }
        const std::string source = only ? ".item:only-child" : ".item:first-child";
        require(selector_parse(source.c_str(), selector), "parse selector");
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
        const auto css = ".item{opacity:.2;}" + source + "{opacity:.8;}";
        int64_t sheet = 0; int32_t rules = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 1, "edge-benchmark", &sheet, &rules) == AETHER_OK && rules == 2, "stylesheet");
    }
    ~Fixture() { aether_style_runtime_destroy(runtime); aether_doc_destroy(doc); }
    bool expected(size_t i, bool only) const { return i == 0 && (!only || subjects.size() == 1); }
    uint64_t validate_models(bool only, uint64_t& old_visits, uint64_t& new_visits) {
        uint64_t matches = 0;
        for (size_t i = 0; i < subjects.size(); ++i) {
            const bool a = original(subjects[i], only, old_visits);
            const bool b = candidate(subjects[i], only, new_visits);
            require(a == expected(i, only) && a == b, "count model equivalence");
            require(selector_match(selector, subjects[i]) == a, "real matcher equivalence");
            matches += a;
        }
        return matches;
    }
    void validate_styles(aether_style_update_t* updates, int32_t count, bool only) {
        require(count == int32_t(subjects.size() + controls.size()), "cold resolution update count");
        std::vector<bool> seen(subjects.size(), false), seen_controls(controls.size(), false);
        for (int32_t i = 0; i < count; ++i) {
            aether_node_t node = nullptr; aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "get update");
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy computed style");
            const auto it = indices.find(node);
            float expected_opacity = 1.f;
            if (it != indices.end()) {
                require(!seen[it->second], "duplicate element update"); seen[it->second] = true;
                expected_opacity = expected(it->second, only) ? .8f : .2f;
            } else {
                const auto control = controls.find(node);
                require(control != controls.end() && !seen_controls[control->second], "unexpected control update");
                seen_controls[control->second] = true;
            }
            require(style.opacity > expected_opacity - .0001f && style.opacity < expected_opacity + .0001f, "computed opacity");
            aether_computed_style_free(&style); aether_computed_style_handle_destroy(handle);
        }
        for (bool present : seen_controls) require(present, "missing control style");
        for (bool present : seen) require(present, "missing element style");
    }
};
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - begin).count();
}
}
int main(int argc, char** argv) {
    // Optional positive sample count allows a quick smoke run; default is 15.
    int samples = 15;
    if (argc > 1) { char* end = nullptr; const auto v = std::strtol(argv[1], &end, 10);
        require(end && *end == '\0' && v > 0 && v <= 1000, "samples must be 1..1000"); samples = int(v); }
    require(argc <= 2, "usage: benchmark [samples]");
    std::puts("shape,n,elements,text_nodes,selector,phase,sample,us,matches,original_visits,candidate_visits");
    for (const char* shape : {"all-elements", "constant-text-prefix", "proportional-text-prefix", "interleaved-text", "singleton-text-margins"})
    for (int n : {512, 1024, 2048, 4096}) for (bool only : {false, true}) {
        Fixture f(shape, n, only);
        uint64_t old_visits = 0, new_visits = 0;
        const auto matches = f.validate_models(only, old_visits, new_visits);
        const char* selector = only ? "only-child" : "first-child";
        std::printf("%s,%d,%zu,%zu,%s,visits,-1,0,%llu,%llu,%llu\n", shape,n,f.subjects.size(),f.text_count,selector,
                    (unsigned long long)matches,(unsigned long long)old_visits,(unsigned long long)new_visits);
        for (int sample = -2; sample < samples; ++sample) {
            constexpr int passes = 4;
            uint64_t observed = 0;
            const auto begin = Clock::now();
            for (int pass = 0; pass < passes; ++pass)
                for (auto* node : f.subjects) observed += selector_match(f.selector, node);
            const auto end = Clock::now();
            require(observed == matches * passes, "timed matcher checksum");
            if (sample >= 0) std::printf("%s,%d,%zu,%zu,%s,matcher,%d,%.6f,%llu,0,0\n",shape,n,f.subjects.size(),f.text_count,selector,sample,elapsed(begin,end)/passes,(unsigned long long)matches);
            // Excludes construction, parsing, cache invalidation, verification,
            // and freeing returned updates. Includes public resolution itself.
            aether_style_runtime_clear_cache(f.runtime);
            aether_style_update_t* updates = nullptr;
            const auto resolve_begin = Clock::now();
            const auto count = aether_style_runtime_resolve_subtree(f.runtime, f.root, &updates);
            const auto resolve_end = Clock::now();
            require(count >= 0 && updates != nullptr, "resolve subtree");
            f.validate_styles(updates, count, only);
            aether_style_updates_free(updates, count);
            if (sample >= 0) std::printf("%s,%d,%zu,%zu,%s,resolve-cold,%d,%.6f,%llu,0,0\n",shape,n,f.subjects.size(),f.text_count,selector,sample,elapsed(resolve_begin,resolve_end),(unsigned long long)matches);
        }
    }
}
