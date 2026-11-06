// Public C ABI regression. Checks remain active in Release/NDEBUG.
#include <aether/aether.h>

#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {
int cases = 0, checks = 0, failures = 0;
std::string context;
bool check(bool ok, const std::string& detail) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: %s\n", context.c_str(), detail.c_str());
    }
    return ok;
}
std::string take(char* value) {
    check(value != nullptr, "owned ABI string");
    std::string result = value ? value : "";
    aether_string_free(value);
    return result;
}
struct Document {
    aether_doc_t handle = nullptr;
    Document() { check(aether_doc_create(&handle) == AETHER_OK && handle, "create document"); }
    ~Document() { aether_doc_destroy(handle); }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
};
using Attributes = std::map<std::string, std::string>;
struct Node {
    aether_node_type_t type;
    std::string name, value;
    Attributes attributes;
    std::vector<Node> children;
};
Node text(std::string value) { return {AETHER_NODE_TEXT, "", std::move(value), {}, {}}; }
Node comment(std::string value) { return {AETHER_NODE_COMMENT, "", std::move(value), {}, {}}; }
Node element(std::string name, Attributes attributes = {}, std::vector<Node> children = {}) {
    return {AETHER_NODE_ELEMENT, std::move(name), "", std::move(attributes), std::move(children)};
}

// Compare literal types, names, bytes, complete attribute maps, child order and
// reciprocal parent links. No serialization, text coalescing or normalization
// can conceal an extra element or a fragment of the discarded start tag.
void tree_node(aether_node_t node, aether_node_t parent, const Node& expected,
               std::set<aether_node_t>& visited, const std::string& path, unsigned depth = 0) {
    if (!check(node && depth < 32 && visited.size() < 10000, path + " bounded node")) return;
    if (!check(visited.insert(node).second, path + " unique handle")) return;
    check(aether_node_get_parent(node) == parent, path + " parent");
    check(aether_node_get_type(node) == expected.type, path + " literal type");
    check(take(aether_node_get_text_content(node)) == expected.value, path + " literal node-local text");
    if (expected.type == AETHER_NODE_ELEMENT) {
        check(take(aether_element_get_tag_name(node)) == expected.name, path + " literal tag");
        const int32_t count = aether_element_get_attribute_count(node);
        check(count == static_cast<int32_t>(expected.attributes.size()), path + " attribute count");
        Attributes actual;
        if (check(count >= 0 && count <= 10000, path + " bounded attributes")) {
            for (int32_t i = 0; i < count; ++i) {
                auto key = take(aether_element_get_attribute_name(node, i));
                auto value = take(aether_element_get_attribute_value(node, i));
                check(actual.emplace(key, value).second, path + " unique attribute name");
            }
        }
        check(actual == expected.attributes, path + " complete attribute map");
        for (const auto& [key, value] : expected.attributes)
            check(take(aether_element_get_attribute(node, key.c_str())) == value, path + " getter " + key);
    }
    const int32_t count = aether_node_get_child_count(node);
    check(count == static_cast<int32_t>(expected.children.size()), path + " exact child count");
    for (size_t i = 0; i < expected.children.size(); ++i)
        tree_node(aether_node_get_child(node, static_cast<int32_t>(i)), node, expected.children[i],
                  visited, path + "/" + std::to_string(i), depth + 1);
    check(!aether_node_get_child(node, static_cast<int32_t>(expected.children.size())), path + " no extra child");
}
void tree(aether_doc_t doc, const std::vector<Node>& roots) {
    auto root = aether_doc_get_document_element(doc);
    if (roots.empty()) {
        // The public getter falls back to ANY non-generated document child,
        // including text/comment, so this excludes text recovery as well.
        check(root == nullptr && aether_doc_get_body(doc) == nullptr, "empty document, no invented nodes");
        return;
    }
    if (!check(root != nullptr, "document root exists")) return;
    auto sentinel = aether_node_get_parent(root);
    if (!check(sentinel != nullptr, "document sentinel exists")) return;
    std::set<aether_node_t> visited;
    tree_node(sentinel, nullptr, {AETHER_NODE_DOCUMENT, "", "", {}, roots}, visited, "document");
}
void run_on(Document& doc, const std::string& name, const std::string& html,
            const std::vector<Node>& roots, const std::vector<std::string>& absent = {}) {
    context = name;
    ++cases;
    const auto version = aether_doc_get_version(doc.handle);
    if (check(aether_doc_parse_html(doc.handle, html.c_str()) == AETHER_OK, "tolerant parse succeeds")) {
        check(aether_doc_get_version(doc.handle) == version + 1, "one version advance");
        tree(doc.handle, roots);
        for (const auto& id : absent)
            check(!aether_doc_get_element_by_id(doc.handle, id.c_str()), "absent ID " + id);
    }
}
void run(const std::string& name, const std::string& html, const std::vector<Node>& roots,
         const std::vector<std::string>& absent = {}) {
    Document doc;
    run_on(doc, name, html, roots, absent);
}

const std::string prefix = "<body id=body><div id=host>A<!--c--><span id=kept>B</span>";
std::vector<Node> prefix_tree(std::vector<Node> added = {}) {
    std::vector<Node> children = {text("A"), comment("c"), element("span", {{"id", "kept"}}, {text("B")})};
    for (auto& node : added) children.push_back(std::move(node));
    return {element("body", {{"id", "body"}}, {element("div", {{"id", "host"}}, std::move(children))})};
}
const std::vector<std::pair<std::string, std::string>> tags = {
    {"input", "input"}, {"div", "div"}, {"x-item", "x-item"}, {"style", "style"}, {"script", "script"},
    {"INPUT", "input"}, {"DIV", "div"}, {"X-ITEM", "x-item"}, {"STYLE", "style"}, {"SCRIPT", "script"},
    {"InPuT", "input"}, {"DiV", "div"}, {"X-ItEm", "x-item"}, {"StYlE", "style"}, {"ScRiPt", "script"}
};
void incomplete_matrix() {
    // Every string below reaches REAL EOF. Never append a closing wrapper.
    const std::vector<std::string> tails = {
        "", " ", "\t", "\n", "\r", "\f", "/",
        " id", " id ", " id=", " id= ", " id=ghost", " id=ghost ",
        " id='ghost", " id=\"ghost", " id='ghost'", " id=\"ghost\"",
        " id='ghost' ", " id='ghost'/", " id=ghost disabled", " id=ghost disabled=",
        " id=ghost disabled=''", " id=ghost data-v='' ",
        " id=ghost data-v='>", " id=ghost data-v=\">",
        " id=ghost data-v='><span id=phantom>",
        " id=ghost data-v=\"><span id=phantom>",
        " id=ghost data-v='/><input id=phantom />",
        " id=ghost data-v=\"'/><input id='phantom'>",
        " id=ghost data-v='\"/><input id=\"phantom\">",
        " id=ghost data-v='&gt;&#62;&quot;><span id=phantom>",
        " id=ghost data-v='&amp;gt;'", " id=ghost data-v=&#62;",
        " id=ghost data-v=first data-v", " id=ghost data-v=first data-v=",
        " id=ghost data-v=first data-v='><span id=phantom>",
        " id=ghost data-v=first data-v=\"/><input id=phantom>",
        " id=ghost id=phantom", " id=ghost id='phantom' "
    };
    for (const auto& [opening, canonical] : tags) {
        (void)canonical;
        for (size_t i = 0; i < tails.size(); ++i) {
            const auto token = "<" + opening + tails[i];
            const auto name = "incomplete/" + opening + "/" + std::to_string(i);
            run(name + "/empty", token, {}, {"ghost", "phantom"});
            run(name + "/prefix", prefix + token, prefix_tree(), {"ghost", "phantom"});
        }
    }
}
void complete_matrix() {
    struct Ending { std::string tail; Attributes attrs; };
    const std::vector<Ending> endings = {
        {">", {}}, {"/>", {}}, {" />", {}},
        {" id=>", {{"id", ""}}}, {" id=ghost>", {{"id", "ghost"}}},
        {" id='ghost'>", {{"id", "ghost"}}}, {" id=\"ghost\">", {{"id", "ghost"}}},
        {" id='ghost'/>", {{"id", "ghost"}}}, {" id='ghost' />", {{"id", "ghost"}}},
        {" data-v='>'>", {{"data-v", ">"}}}, {" data-v=\">\">", {{"data-v", ">"}}},
        {" data-v='&gt;'>", {{"data-v", ">"}}}, {" data-v=&#62;>", {{"data-v", ">"}}},
        {" data-v='&amp;gt;'>", {{"data-v", "&gt;"}}},
        {" data-v='&quot;'>", {{"data-v", "\""}}},
        {" data-v=first data-v='><span id=phantom>'>", {{"data-v", "first"}}},
        {" data-v=first data-v=\"/><span id=phantom>\"/>", {{"data-v", "first"}}},
        {" id=ghost disabled>", {{"id", "ghost"}, {"disabled", ""}}}
    };
    for (const auto& [opening, canonical] : tags) {
        for (size_t i = 0; i < endings.size(); ++i) {
            const auto& ending = endings[i];
            const auto token = "<" + opening + ending.tail;
            const auto expected = element(canonical, ending.attrs);
            const auto name = "complete/" + opening + "/" + std::to_string(i);
            run(name + "/empty", token, {expected}, {"phantom"});
            run(name + "/prefix", prefix + token, prefix_tree({expected}), {"phantom"});
        }
    }
    run("unquoted slash is data", "<div data-v=x/><span id=child>",
        {element("div", {{"data-v", "x/"}}, {element("span", {{"id", "child"}})})});
    run("separated slash retains simplified nonvoid selfclose", "<div data-v=x /><span id=child>",
        {element("div", {{"data-v", "x"}}), element("span", {{"id", "child"}})});
    run("bare nonvoid selfclose", "<div/><span id=child>",
        {element("div"), element("span", {{"id", "child"}})});
    run("bare void selfclose", "<input/><span id=child>",
        {element("input"), element("span", {{"id", "child"}})});
    for (const auto& [opening, canonical] : tags) {
        if (canonical != "style" && canonical != "script") continue;
        const std::string raw = "A&amp;B <input id=phantom> </not-the-tag> tail";
        run("complete raw text EOF/" + opening, prefix + "<" + opening + " id=raw>" + raw,
            prefix_tree({element(canonical, {{"id", "raw"}}, {text(raw)})}), {"phantom"});
    }
}
void preservation_controls() {
    for (const std::string suffix : {"", "3", " ", "\t"}) {
        auto expected = prefix_tree({text("<")});
        if (!suffix.empty()) expected[0].children[0].children.push_back(text(suffix));
        run("invalid tag-open suffix/" + suffix, prefix + "<" + suffix, expected, {"ghost"});
    }
    run("declaration and comment before EOF", "<!DOCTYPE html><!--lead--><input id=ghost",
        {comment("lead")}, {"ghost"});
    run("complete node survives incomplete sibling", "<input id=kept><input id=ghost",
        {element("input", {{"id", "kept"}})}, {"ghost"});
    run("complete node survives missing close", "<div id=kept>tail",
        {element("div", {{"id", "kept"}}, {text("tail")})});
    run("existing incomplete end tag", "<div id=kept>tail</div",
        {element("div", {{"id", "kept"}}, {text("tail")})});
}
void stress_and_lifetime() {
    Document reused;
    for (const size_t size : {4096u, 16384u, 65536u}) {
        std::string entities;
        entities.reserve(size * 5);
        for (size_t i = 0; i < size; ++i) entities += "&gt;";
        std::string duplicates;
        duplicates.reserve(size * 10);
        for (size_t i = 0; i < size / 4; ++i) duplicates += " data-v='>'";
        const std::vector<std::string> tails = {
            "<input id=ghost data-v=" + std::string(size, '/'),
            "<input id=ghost data-v='" + entities + "><span id=phantom>",
            "<div id=ghost data-v=first" + duplicates + " data-v='><span id=phantom>"
        };
        for (size_t kind = 0; kind < tails.size(); ++kind) {
            const auto start = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < 12; ++repeat) {
                const auto name = "stress/" + std::to_string(size) + "/" + std::to_string(kind) + "/" + std::to_string(repeat);
                run_on(reused, name + "/truncated", prefix + tails[kind], prefix_tree(), {"ghost", "phantom", "old"});
                run_on(reused, name + "/valid", "<input id=old>", {element("input", {{"id", "old"}})}, {"host", "kept", "ghost"});
                Document alternate;
                run_on(alternate, name + "/alternate", tails[kind], {}, {"old", "ghost", "phantom"});
            }
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::printf("STRESS size=%zu kind=%zu bytes=%zu repetitions=12 elapsed_ms=%.3f\n", size, kind, tails[kind].size(), elapsed);
        }
    }
}
} // namespace

int main() {
    context = "ABI";
    check(aether_abi_version() == AETHER_ABI_VERSION_CURRENT, "unchanged ABI version");
    incomplete_matrix();
    complete_matrix();
    preservation_controls();
    stress_and_lifetime();
    std::printf("HTML start-tag EOF: cases=%d checks=%d failures=%d NDEBUG=%s\n", cases, checks, failures,
#ifdef NDEBUG
                "yes"
#else
                "no"
#endif
    );
    return failures ? 1 : 0;
}
