// Public C ABI regression. Checks remain active in Release/NDEBUG.
#include <aether/aether.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {
int cases = 0, failed_cases = 0, checks = 0, failures = 0;
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
Node element(std::string name, Attributes attributes = {}, std::vector<Node> children = {}) {
    return {AETHER_NODE_ELEMENT, std::move(name), "", std::move(attributes), std::move(children)};
}

// Compare literal types, names, bytes, complete attribute maps, child order and
// reciprocal parent links. No serialization, text coalescing or normalization
// can conceal an extra element, a lost comment or leaked delimiter text.
void tree_node(aether_doc_t doc, aether_node_t node, aether_node_t parent, const Node& expected,
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
        for (const auto& [key, value] : expected.attributes) {
            check(take(aether_element_get_attribute(node, key.c_str())) == value, path + " getter " + key);
            if (key == "id") check(aether_doc_get_element_by_id(doc, value.c_str()) == node, path + " ID index");
        }
    }
    const int32_t count = aether_node_get_child_count(node);
    check(count == static_cast<int32_t>(expected.children.size()), path + " exact child count");
    for (size_t i = 0; i < expected.children.size(); ++i)
        tree_node(doc, aether_node_get_child(node, static_cast<int32_t>(i)), node, expected.children[i],
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
    tree_node(doc, sentinel, nullptr, {AETHER_NODE_DOCUMENT, "", "", {}, roots}, visited, "document");
}
void run_on(Document& doc, const std::string& name, const std::string& html,
            const std::vector<Node>& roots, const std::vector<std::string>& absent = {}) {
    context = name;
    ++cases;
    const int before = failures;
    const auto version = aether_doc_get_version(doc.handle);
    if (check(aether_doc_parse_html(doc.handle, html.c_str()) == AETHER_OK, "tolerant parse succeeds")) {
        check(aether_doc_get_version(doc.handle) == version + 1, "one version advance");
        tree(doc.handle, roots);
        for (const auto& id : absent)
            check(!aether_doc_get_element_by_id(doc.handle, id.c_str()), "absent ID " + id);
    }
    if (failures != before) ++failed_cases;
}
void run(const std::string& name, const std::string& html, const std::vector<Node>& roots,
         const std::vector<std::string>& absent = {}) {
    Document doc;
    run_on(doc, name, html, roots, absent);
}


Node comment(std::string value) { return {AETHER_NODE_COMMENT, "", std::move(value), {}, {}}; }
Node after() { return element("p", {{"id", "after"}}, {text("tail")}); }
Node host(const std::vector<Node>& children) { return element("section", {{"id", "host"}}, children); }
Node body(const std::vector<Node>& children) { return element("body", {{"id", "body"}}, children); }

void closed(const std::string& label, const std::string& markup, const std::string& value,
            const std::string& trailing = "") {
    std::vector<Node> children = {text("before"), comment(value)};
    if (!trailing.empty()) children.push_back(text(trailing));
    children.push_back(after());
    run(label, "<body id=body><section id=host>before" + markup +
        "<p id=after>tail</p></section></body>", {body({host(children)})}, {"phantom"});
}
void eof(const std::string& label, const std::string& suffix, const std::string& value) {
    run(label, "<body id=body><section id=host>before<!--" + suffix,
        {body({host({text("before"), comment(value)})})}, {"after", "phantom"});
}
void fixed_cases() {
    struct Case { const char* markup; const char* value; const char* trailing = ""; };
    const Case cases[] = {
        {"<!-->", ""}, {"<!--->", ""}, {"<!---->", ""}, {"<!----!>", ""},
        {"<!--a-->", "a"}, {"<!--a--!>", "a"}, {"<!----->", "-"},
        {"<!------>", "--"}, {"<!-----!>", "-"}, {"<!--a---!>", "a-"},
        {"<!--a->b-->", "a->b"}, {"<!--a--!x-->", "a--!x"},
        {"<!--a--!-x-->", "a--!-x"}, {"<!--a--!-->", "a--!"},
        {"<!--a--!--!>", "a--!"}, {"<!--a--!!-->", "a--!!"},
        {"<!--a--!->b-->", "a--!->b"}, {"<!--a--x-->", "a--x"},
        {"<!--a-- --!>", "a-- "}, {"<!--a---x-->", "a---x"},
        {"<!--<b id=phantom>&amp;&#65;&unknown;</b>-->", "<b id=phantom>&amp;&#65;&unknown;</b>"},
        {"<!--</section><p id=phantom>fake</p>--!>", "</section><p id=phantom>fake</p>"},
        {"<!--A\r\nB\rC\nD--!>", "A\r\nB\rC\nD"},
        {"<!--&#45;&#45;&#33;&#62;-->", "&#45;&#45;&#33;&#62;"},
        {"<!--<!---->", "<!--"}, {"<!--<!--x-->", "<!--x"},
        {"<!--x<!--y-->", "x<!--y"}, {"<!--x<!-->later-->", "x<!", "later-->"}
    };
    for (size_t i = 0; i < std::size(cases); ++i)
        closed("literal/" + std::to_string(i), cases[i].markup, cases[i].value, cases[i].trailing);
    const std::pair<const char*, const char*> endings[] = {
        {"", ""}, {"-", ""}, {"--", ""}, {"--!", ""}, {"a", "a"},
        {"a-", "a"}, {"a--", "a"}, {"a--!", "a"}, {"a---", "a-"},
        {"a----", "a--"}, {"a---!", "a-"}, {"a--!-", "a--!"},
        {"a--!--", "a--!"}, {"a--!--!", "a--!"}, {"a--!!", "a--!!"},
        {"a--!x", "a--!x"}, {"a--!-x", "a--!-x"}, {"a->", "a->"},
        {"a--\r", "a--\r"}, {"a--!\r\n", "a--!\r\n"}
    };
    for (size_t i = 0; i < std::size(endings); ++i)
        eof("EOF-literal/" + std::to_string(i), endings[i].first, endings[i].second);
    run("root-comment", "<!-->text<!--x--!><p id=after>tail</p>",
        {comment(""), text("text"), comment("x"), after()});
    run("adjacent-comments", "<body id=body><!--><!---><!----!><!--a--!><!----></body>",
        {body({comment(""), comment(""), comment(""), comment("a"), comment("")})});
    run("tail-opener-reconsume", "<!--><!---><x id=kept>ok</x>",
        {comment(""), comment(""), element("x", {{"id", "kept"}}, {text("ok")})});
}

void state_byte_matrix() {
    // Literal prefixes reach each of the six reference states. For every ABI
    // byte, a normal close preserves prefix+byte unless that byte is already
    // an abrupt close. EOF expectations enumerate only the reserved bytes;
    // this is not a copy of the production tokenizer or an HTML serializer.
    const std::string prefixes[] = {"", "-", "a", "a-", "a--", "a--!"};
    const std::string pending_eof[] = {"", "", "a", "a", "a", "a"};
    for (size_t state = 0; state < std::size(prefixes); ++state) {
        eof("state-EOF/" + std::to_string(state), prefixes[state], pending_eof[state]);
        for (unsigned byte = 1; byte <= 255; ++byte) {
            const char c = static_cast<char>(byte);
            const std::string label = std::to_string(state) + "/" + std::to_string(byte);
            const std::string input = prefixes[state] + c;
            const bool early = c == '>' && (state == 0 || state == 1 || state == 4 || state == 5);
            const std::string early_data = state < 2 ? "" : "a";
            closed("state-byte-close/" + label, "<!--" + input + "-->",
                   early ? early_data : input, early ? "-->" : "");
            std::string value = input;
            if (early) value = early_data;
            else if (c == '-') {
                const std::string dash[] = {"", "", "a", "a", "a-", "a--!"};
                value = dash[state];
            } else if (state == 4 && c == '!') value = "a";
            eof("state-byte-EOF/" + label, input, value);
        }
    }
}

void boundaries_and_guards() {
    for (const std::string ending : {"", "-", "a", "a-", "a--", "a--!", "a--!-", "a---"}) {
        const std::string value = ending.empty() || ending == "-" ? "" :
            ending == "a--!-" ? "a--!" : ending == "a---" ? "a-" : "a";
        // ABI input ends at the embedded NUL; later bytes are inaccessible.
        const std::string input = "<body id=body><!--" + ending + '\0' + "--><p id=phantom>bad</p>";
        run("NUL-boundary/" + ending, input, {body({comment(value)})}, {"phantom"});
    }
    for (const std::string tag : {"script", "style", "textarea", "title"}) {
        const bool raw = tag == "script" || tag == "style";
        const std::string source = "<!--><!---><!--x--!>A&amp;\r\nB<!--unfinished--!";
        const std::string value = raw ? source : "<!--><!---><!--x--!>A&\nB<!--unfinished--!";
        run("text-mode/" + tag, "<body id=body><" + tag + " id=mode>" + source + "</" + tag +
            "><p id=after>tail</p></body>",
            {body({element(tag, {{"id", "mode"}}, {text(value)}), after()})});
    }
    run("attribute-literal", "<body id=body><i id=kept title='<!--><!---><!--x--!>'></i><p id=after>tail</p></body>",
        {body({element("i", {{"id", "kept"}, {"title", "<!--><!---><!--x--!>"}}), after()})});
    run("encoded-markup", "<body id=body>&lt;!--&gt;&lt;!--x--!&gt;<p id=after>tail</p></body>",
        {body({text("<!--><!--x--!>"), after()})});
    run("declaration-guard", "<body id=body><!-x><!DOCTYPE html><?pi?><p id=after>tail</p></body>", {body({after()})});
    for (const size_t n : {2u, 3u, 4u, 17u, 1024u, 65536u}) {
        const std::string dashes(n, '-');
        closed("long-dashes-close/" + std::to_string(n), "<!--a" + dashes + ">", "a" + std::string(n - 2, '-'));
        closed("long-dashes-bang/" + std::to_string(n), "<!--a" + dashes + "!>", "a" + std::string(n - 2, '-'));
        eof("long-dashes-EOF/" + std::to_string(n), "a" + dashes, "a" + std::string(n - 2, '-'));
    }
    const std::string data(262144, 'x');
    closed("large-literal", "<!--" + data + "--!>", data);
    Document doc;
    for (int pass = 0; pass < 8; ++pass) {
        run_on(doc, "reparse/" + std::to_string(pass), "<body id=body><!--x--!><p id=after>tail</p></body>",
               {body({comment("x"), after()})});
        run_on(doc, "replace-with-EOF/" + std::to_string(pass), "<!--a--!", {comment("a")}, {"body", "after"});
        run_on(doc, "replace-with-empty/" + std::to_string(pass), "", {}, {"body", "after"});
    }
}
} // namespace
int main() {
    fixed_cases();
    state_byte_matrix();
    boundaries_and_guards();
    std::printf("HTML comments: %d cases, %d failed cases, %d checks, %d failures\n", cases, failed_cases, checks, failures);
    return failures ? 1 : 0;
}
