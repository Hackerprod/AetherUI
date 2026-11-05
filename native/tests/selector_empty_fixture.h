#pragma once

#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace empty_test {

inline int checks = 0;

// These checks and the actions passed to them must also execute under NDEBUG.
inline void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        std::exit(1);
    }
}

inline bool same(float a, float b) { return std::fabs(a - b) < .0001f; }

inline std::string text(aether_node_t node) {
    char* value = aether_node_get_text_content(node);
    require(value != nullptr, "node text must be readable");
    std::string result(value);
    aether_string_free(value);
    return result;
}

inline void append(aether_node_t parent, aether_node_t child) {
    require(aether_node_append_child(parent, child) == AETHER_OK, "append/move must succeed");
    require(aether_node_get_parent(child) == parent, "append must set the new parent");
}

inline void remove(aether_node_t parent, aether_node_t child) {
    require(aether_node_remove_child(parent, child) == AETHER_OK, "remove must succeed");
    require(aether_node_get_parent(child) == nullptr, "remove must detach the child");
}

struct DomSnapshot {
    aether_node_t node;
    aether_node_t parent;
    aether_node_type_t type;
    int64_t id;
    std::string content;
    std::vector<aether_node_t> children;

    explicit DomSnapshot(aether_node_t value)
        : node(value), parent(aether_node_get_parent(value)), type(aether_node_get_type(value)),
          id(aether_node_get_style_node_id(value)), content(text(value)) {
        for (int32_t i = 0; i < aether_node_get_child_count(node); ++i)
            children.push_back(aether_node_get_child(node, i));
    }

    void verify(const std::string& label) const {
        require(aether_node_get_parent(node) == parent, label + ": parent preserved");
        require(aether_node_get_type(node) == type, label + ": type preserved");
        require(aether_node_get_style_node_id(node) == id, label + ": ID preserved");
        require(text(node) == content, label + ": text preserved");
        require(aether_node_get_child_count(node) == static_cast<int32_t>(children.size()),
                label + ": public child count preserved");
        for (size_t i = 0; i < children.size(); ++i)
            require(aether_node_get_child(node, static_cast<int32_t>(i)) == children[i],
                    label + ": public child identity/order preserved");
    }
};

struct PseudoObservation {
    aether_node_t host;
    float font_size;
    std::string content;
};

struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    std::unordered_map<aether_node_t, float> opacity;
    std::vector<PseudoObservation> pseudos;

    Fixture(const std::string& html, const std::string& css, int expected_rules) {
        require(aether_doc_create(&doc) == AETHER_OK, "document creation");
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "HTML parsing");
        require(aether_doc_get_body(doc) != nullptr, "body exists");
        aether_style_runtime_options_t options{};
        const aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "style runtime creation");
        int64_t sheet = 0;
        int32_t count = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 2, "selector-empty.css",
                                                   &sheet, &count) == AETHER_OK,
                "stylesheet compilation");
        require(count == expected_rules, "all expected rules must compile: " + css);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    ~Fixture() {
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }

    aether_node_t get(const char* id) const {
        const auto node = aether_doc_get_element_by_id(doc, id);
        require(node != nullptr, std::string("ID lookup: ") + id);
        return node;
    }

    static void collect_dom(aether_node_t node, std::unordered_set<aether_node_t>& nodes) {
        nodes.insert(node);
        for (int32_t i = 0; i < aether_node_get_child_count(node); ++i)
            collect_dom(aether_node_get_child(node, i), nodes);
    }

    void resolve() {
        std::unordered_set<aether_node_t> dom;
        const auto body = aether_doc_get_body(doc);
        collect_dom(body, dom);
        // Generated node addresses may be destroyed/reused on every pass. Only
        // retain copied observations and original DOM host handles across passes.
        pseudos.clear();
        aether_style_update_t* updates = nullptr;
        const int32_t count = aether_style_runtime_resolve_subtree(runtime, body, &updates);
        require(count >= 0 && (count == 0 || updates != nullptr), "style resolution succeeds");
        for (int32_t i = 0; i < count; ++i) {
            aether_node_t node = nullptr;
            aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK,
                    "read style update");
            require(node != nullptr && handle != nullptr, "update node/style exists");
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy computed style");
            if (dom.count(node)) {
                // No update on a later pass means unchanged, not a default value.
                opacity[node] = style.opacity;
            } else {
                const auto host = aether_node_get_parent(node);
                require(dom.count(host) == 1, "generated update belongs to a DOM host");
                std::string content;
                for (int32_t j = 0; j < aether_node_get_child_count(node); ++j) {
                    const auto child = aether_node_get_child(node, j);
                    require(aether_node_get_type(child) == AETHER_NODE_TEXT, "pseudo has only text content");
                    content += text(child);
                }
                pseudos.push_back({host, style.font_size, content});
            }
            aether_computed_style_free(&style);
            aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates, count);
    }

    void expect(aether_node_t node, bool matched, const std::string& label) const {
        const auto found = opacity.find(node);
        require(found != opacity.end(), label + ": initial style must have been observed");
        require(same(found->second, matched ? .8f : .2f), label + ": match result");
    }

    void expect_pseudos(aether_node_t host, const std::vector<std::string>& expected,
                        const std::string& label) const {
        size_t count = 0;
        for (const auto& pseudo : pseudos) {
            if (pseudo.host != host) continue;
            require(count < expected.size(), label + ": no unexpected pseudo");
            require(pseudo.content == expected[count], label + ": pseudo text/order");
            // Fixtures mark before/after distinctly; empty text alone must not
            // make a missing generated node look like a successfully empty one.
            require(same(pseudo.font_size, count == 0 ? 17.f : 19.f), label + ": pseudo style/order");
            ++count;
        }
        require(count == expected.size(), label + ": both pseudo existence and count");
    }
};

} // namespace empty_test
