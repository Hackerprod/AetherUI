#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
int checks = 0;
std::string context;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL [%s]: %s\n", context.c_str(), message);
        std::exit(1);
    }
}
std::string owned(char* value) {
    require(value != nullptr, "read allocated string");
    const std::string result(value);
    aether_string_free(value);
    return result;
}
struct Document {
    aether_doc_t doc = nullptr;
    Document() { require(aether_doc_create(&doc) == AETHER_OK, "create document"); }
    ~Document() { aether_doc_destroy(doc); }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    void parse(const std::string& html) {
        const auto version = aether_doc_get_version(doc);
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "tolerant parse succeeds");
        require(aether_doc_get_version(doc) == version + 1, "parse increments version exactly once");
    }
    aether_node_t get(const char* id) const {
        const auto node = aether_doc_get_element_by_id(doc, id);
        require(node != nullptr, "required connected ID exists");
        return node;
    }
};
void element(aether_node_t node, aether_node_t parent, const char* tag, int children) {
    require(node != nullptr, "real node exists before querying consumers");
    require(aether_node_get_type(node) == AETHER_NODE_ELEMENT, "element type");
    require(aether_node_get_parent(node) == parent, "reciprocal parent");
    require(owned(aether_element_get_tag_name(node)) == tag, "element tag");
    require(aether_node_get_child_count(node) == children, "exact children, including nodes without IDs");
}
void walk(aether_node_t node, std::set<aether_node_t>& nodes, int& focusable, int& disabled) {
    require(node != nullptr && nodes.insert(node).second, "connected tree has unique live nodes");
    if (aether_node_get_type(node) == AETHER_NODE_ELEMENT) {
        focusable += aether_element_is_focusable(node) != AETHER_FALSE;
        disabled += aether_element_is_disabled(node) != AETHER_FALSE;
    }
    for (int32_t i = 0; i < aether_node_get_child_count(node); ++i) {
        const auto child = aether_node_get_child(node, i);
        require(child != nullptr && aether_node_get_parent(child) == node, "tree child/parent link");
        walk(child, nodes, focusable, disabled);
    }
}
void controls_and_ids() {
    const std::string prefix = "<body id=body><input id=dup><button id=dup disabled></button><div id=host>";
    const std::vector<std::string> tails = {
        "<input", "<input ", "<input/", "<input id=ghost", "<input id=ghost disabled",
        "<INPUT id=ghost disabled='yes'", "<button id=ghost tabindex=0",
        "<input id=dup", "<input id=ghost id=dup data-v='><input id=phantom>",
        "<div tabindex=0 disabled data-v='>"
    };
    for (const auto& tail : tails) {
        context = "controls/" + tail;
        Document d;
        d.parse(prefix + tail);
        const auto body = d.get("body");
        const auto root = aether_node_get_parent(body);
        require(root != nullptr && aether_node_get_type(root) == AETHER_NODE_DOCUMENT, "document root exists");
        require(aether_node_get_child_count(root) == 1 && aether_node_get_child(root, 0) == body, "exact document tree");
        element(body, root, "body", 3);
        const auto first = aether_node_get_child(body, 0);
        const auto second = aether_node_get_child(body, 1);
        const auto host = aether_node_get_child(body, 2);
        element(first, body, "input", 0);
        element(second, body, "button", 0);
        element(host, body, "div", 0);
        require(d.get("host") == host && d.get("dup") == first, "first connected duplicate ID wins");
        require(owned(aether_element_get_attribute(second, "id")) == "dup", "second duplicate is retained in DOM");
        require(aether_element_is_focusable(first) && !aether_element_is_disabled(first), "live enabled control");
        // The public focusability predicate classifies the element independently of disabled.
        require(aether_element_is_focusable(second) && aether_element_is_disabled(second), "live disabled control");
        std::set<aether_node_t> nodes;
        int focusable = 0, disabled = 0;
        walk(root, nodes, focusable, disabled);
        require(nodes.size() == 5 && focusable == 2 && disabled == 1, "no extra controls or disabled nodes");
        const auto version = aether_doc_get_version(d.doc);
        require(!aether_doc_get_element_by_id(d.doc, "ghost") && !aether_doc_get_element_by_id(d.doc, "phantom"), "no ghost IDs");
        require(d.get("dup") == first && aether_doc_get_version(d.doc) == version, "ID reads preserve version and first match");
    }
}
struct Runtime {
    aether_style_runtime_t runtime = nullptr;
    Runtime() {
        aether_style_runtime_options_t options{};
        const aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
    }
    ~Runtime() { aether_style_runtime_destroy(runtime); }
    void add(const char* css, int expected_rules) {
        int64_t sheet = 0;
        int32_t rules = -1;
        require(aether_style_runtime_add_stylesheet(runtime, css, 2, "eof-consumers.css", &sheet, &rules) == AETHER_OK, "compile stylesheet");
        require(rules == expected_rules, "all structural selector rules compile");
    }
    std::map<aether_node_t, float> resolve(aether_node_t root, const std::set<aether_node_t>& connected) {
        aether_style_update_t* updates = nullptr;
        const int32_t count = aether_style_runtime_resolve_subtree(runtime, root, &updates);
        require(count >= 0 && (count == 0 || updates != nullptr), "resolve styles");
        std::map<aether_node_t, float> result;
        for (int32_t i = 0; i < count; ++i) {
            aether_node_t node = nullptr;
            aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "read runtime update");
            require(node && handle && connected.count(node) == 1, "update references only a connected element");
            require(aether_node_get_type(node) == AETHER_NODE_ELEMENT, "runtime update element type");
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy style");
            require(result.emplace(node, style.opacity).second, "no duplicate node update");
            aether_computed_style_free(&style);
            aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates, count);
        return result;
    }
};
void expect_opacity(const std::map<aether_node_t, float>& values, aether_node_t node, float expected) {
    const auto found = values.find(node);
    require(found != values.end(), "host has actual style update");
    require(std::fabs(found->second - expected) < .0001f, "structural selector opacity");
}
void selectors() {
    struct Case { const char* tail; bool complete; };
    const Case cases[] = {
        {"<input id=ghost", false}, {"<input id=ghost disabled", false},
        {"<INPUT id=ghost data-v='>", false}, {"<input id='ghost'/", false},
        {"<input id=ghost>", true}, {"<input id='ghost' />", true}
    };
    for (const auto& test : cases) {
        context = std::string("selectors/") + test.tail;
        Document d;
        d.parse(std::string("<body id=body><div id=host>") + test.tail);
        const auto body = d.get("body"), host = d.get("host");
        element(body, aether_node_get_parent(body), "body", 1);
        require(aether_node_get_child(body, 0) == host, "host tree order");
        element(host, body, "div", test.complete ? 1 : 0);
        require((aether_doc_get_element_by_id(d.doc, "ghost") != nullptr) == test.complete, "ID matches actual terminator");
        std::set<aether_node_t> connected;
        int focusable = 0, disabled = 0;
        walk(body, connected, focusable, disabled);
        require(connected.size() == (test.complete ? 3u : 2u), "exact connected selector fixture");
        Runtime r;
        r.add("#host{opacity:.2}#host:empty{opacity:.8}#host:has(input){opacity:.4}", 3);
        const auto version = aether_doc_get_version(d.doc);
        auto values = r.resolve(body, connected);
        require(values.size() == connected.size(), "initial runtime updates cover exactly connected elements");
        expect_opacity(values, host, test.complete ? .4f : .8f);
        // A later stylesheet forces a real second update, not an empty-cache false positive.
        r.add("#host:empty{opacity:.7}#host:has(input){opacity:.3}", 2);
        values = r.resolve(body, connected);
        expect_opacity(values, host, test.complete ? .3f : .7f);
        require(aether_doc_get_version(d.doc) == version, "style resolution preserves document version");
        require(aether_node_get_child_count(host) == (test.complete ? 1 : 0), "style resolution preserves exact tree");
    }
}
void reparse(Document& d, const char* marker) {
    // Every node handle is acquired after parse and is used only until the next parse.
    for (int pass = 0; pass < 3; ++pass) {
        const bool truncated = pass == 1;
        context = std::string("reparse/") + marker + "/" + std::to_string(pass);
        const std::string id = std::string(marker) + (truncated ? "-middle" : pass == 0 ? "-before" : "-after");
        d.parse("<body id=body><div id=host><input id=" + id + ">" +
                (truncated ? "<input id=ghost disabled" : "<button id=complete disabled>"));
        const auto body = d.get("body"), host = d.get("host");
        element(body, aether_node_get_parent(body), "body", 1);
        element(host, body, "div", truncated ? 1 : 2);
        require(d.get(id.c_str()) == aether_node_get_child(host, 0), "current parse ID and order");
        require(!aether_doc_get_element_by_id(d.doc, "ghost"), "truncated ID never enters replacement index");
        require((aether_doc_get_element_by_id(d.doc, "complete") != nullptr) == !truncated, "complete ID removed and restored");
        for (const char* suffix : {"-before", "-middle", "-after"}) {
            const auto candidate = std::string(marker) + suffix;
            if (candidate != id) require(!aether_doc_get_element_by_id(d.doc, candidate.c_str()), "old or future parse ID absent");
        }
        std::set<aether_node_t> nodes;
        int focusable = 0, disabled = 0;
        walk(body, nodes, focusable, disabled);
        require(nodes.size() == (truncated ? 3u : 4u) && focusable == (truncated ? 1 : 2) && disabled == (truncated ? 0 : 1), "replacement tree control counts");
    }
}
void lifecycle() {
    Document a, b;
    for (int round = 0; round < 16; ++round) {
        reparse(a, "a");
        const auto a_version = aether_doc_get_version(a.doc);
        reparse(b, "b");
        require(aether_doc_get_version(a.doc) == a_version, "other document parse leaves version untouched");
        require(a.get("a-after") != nullptr && b.get("b-after") != nullptr, "independent documents retain current IDs");
        require(!aether_doc_get_element_by_id(a.doc, "b-after") && !aether_doc_get_element_by_id(b.doc, "a-after"), "indices do not cross documents");
    }
}
} // namespace
int main() {
    controls_and_ids();
    selectors();
    lifecycle();
    std::printf("HTML start-tag EOF consumers: %d active checks passed\n", checks);
    return 0;
}
