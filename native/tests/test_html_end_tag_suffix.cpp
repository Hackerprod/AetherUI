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
// can conceal an extra element or a fragment of the discarded end tag.
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

Node after() { return element("p", {{"id", "after"}}, {text("tail")}); }
Node host(const std::vector<Node>& children) { return element("section", {{"id", "host"}}, children); }
Node body(const std::vector<Node>& children) { return element("body", {{"id", "body"}}, children); }
std::string casing(std::string value, unsigned mask) {
    for (size_t i = 0; i < value.size(); ++i)
        if (value[i] >= 'a' && value[i] <= 'z' && (mask & (1u << i)))
            value[i] = static_cast<char>(value[i] - 'a' + 'A');
    return value;
}

const std::vector<std::string> suffixes = {
    ">", " >", "\t>", "\n>", "\f>", "\r>", "/>", " / >", "///>",
    " x>", " x y >", " x=y>", " x= >", " x=\"a>b\">", " x='a>b'>",
    " x = 'a>b' y = \"c>d\">", " x='a>b' y=z>", " x='a>b'y=\"c>d\">",
    " x='a>b'/>", " x='a>b'/ y=\"c>d\">", " /x='a>b'>", "/x='a>b'>",
    " =='a>b'>", " ==\"a>b\">", " 'x='a>b'>", " \"x=\"a>b\">",
    " x\v='a>b'>", " x\v=\"a>b\">", " x='a\r\nb>c'>", " x='a\v>b'>",
    " x='&quot;>&#39;>&#x22;><b id=phantom>'>", " x=\"&apos;>&#34;><b id=phantom>\">",
    " x='</section><p id=phantom>fake</p>' id=phantom>",
    " x=\"</body><p id=phantom>fake</p>\" id=phantom>",
    // Quotes and slashes are data in names and unquoted values.
    " x'y>", " x\"y>", " '=a>", " \"=a>", " =\"a>", " ='a>",
    " x=a'b>", " x=a\"b>", " x=`a>", " x=a/b>", " x=a/b/>",
    " x=\v'a>", " x=\v\"a>", " x=&quot;a>", " x=&#34;a>", " x=&#x27;a>",
    " x=&#62;>", " x=&gt;>", " x=&#x3e;>", " x=&#32;y='a>",
    " x=a'b y=\"c>d\">", " x=a\"b y='c>d'>", " x=`a y='c>d'>"
};

void matched_suffixes() {
    for (const std::string tag : {"div", "x-item", "span", "style", "script", "textarea", "title"}) {
        const bool raw = tag == "style" || tag == "script";
        const bool rcdata = tag == "textarea" || tag == "title";
        const std::string input = raw || rcdata ? "A<b id=phantom>&amp;\r\nB" : "A&amp;B";
        const std::string expected = raw ? input : rcdata ? "A<b id=phantom>&\nB" : "A&B";
        for (size_t i = 0; i < suffixes.size(); ++i) {
            for (const unsigned mask : {0u, 255u, 85u})
                run("matched/" + tag + "/" + std::to_string(i) + "/" + std::to_string(mask),
                    "<body id=body><section id=host><" + casing(tag, 170) + " id=target>" + input +
                    "</" + casing(tag, mask) + suffixes[i] + "<p id=after>tail</p></section></body>",
                    {body({host({element(tag, {{"id", "target"}}, {text(expected)}), after()})})}, {"phantom"});
        }
    }
}
void unknown_and_ancestors() {
    for (size_t i = 0; i < suffixes.size(); ++i) {
        const auto& suffix = suffixes[i];
        const std::string label = std::to_string(i);
        for (const std::string name : {"missing", "X-MiSsInG", "div-extra", "img"})
            run("unknown/" + name + "/" + label,
                "<body id=body><section id=host><div id=target>A</" + name + suffix +
                "<p id=after>tail</p></div></section></body>",
                {body({host({element("div", {{"id", "target"}}, {text("A"), after()})})})}, {"phantom"});
        run("ancestor/" + label,
            "<body id=body><section id=host><div id=target><x-inner id=inner>A</DiV" + suffix +
            "<p id=after>tail</p></section></body>",
            {body({host({element("div", {{"id", "target"}}, {element("x-inner", {{"id", "inner"}}, {text("A")})}), after()})})},
            {"phantom"});
        run("nearest/" + label,
            "<body id=body><div id=outer><div id=target>A</DiV" + suffix + "<p id=after>tail</p></div></body>",
            {body({element("div", {{"id", "outer"}}, {element("div", {{"id", "target"}}, {text("A")}), after()})})}, {"phantom"});
        run("empty-stack/" + label, "</missing" + suffix + "<body id=body><p id=after>tail</p></body>",
            {body({after()})}, {"phantom"});
        run("sentinel/" + label, "<body id=body><div id=target>A</BODY" + suffix + "<p id=after>tail</p>",
            {body({element("div", {{"id", "target"}}, {text("A")})}), after()}, {"phantom"});
    }
    std::string input = "<body id=body><section id=host><div id=target>A";
    for (int i = 0; i < 512; ++i) input += "</missing x='><b id=phantom>'>";
    run("repeated-unknown", input + "<p id=after>tail</p></div></section></body>",
        {body({host({element("div", {{"id", "target"}}, {text("A"), after()})})})}, {"phantom"});
}
void boundary_bytes() {
    // Every byte representable through the NUL-terminated ABI. The narrow
    // read_name alphabet is deliberately retained, including custom names.
    for (unsigned byte = 1; byte <= 255; ++byte) {
        const char c = static_cast<char>(byte);
        const bool delimiter = c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
        const bool name_byte = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':' || c == '.';
        const auto target = element("div", {{"id", "target"}}, {text("A")});
        const std::vector<Node> children = c == '>' ? std::vector<Node>{target, text(" x='a>b'>"), after()}
            : delimiter ? std::vector<Node>{target, after()}
            : name_byte ? std::vector<Node>{element("div", {{"id", "target"}}, {text("A"), after()})}
            : std::vector<Node>{target, text("b'>"), after()};
        run("boundary-byte/" + std::to_string(byte),
            std::string("<body id=body><div id=target>A</div") + c + " x='a>b'><p id=after>tail</p></div></body>",
            {body(children)});
    }
}

void legacy_guards() {
    // Out-of-scope malformed prefixes keep the first-'>' legacy behavior.
    // In particular, opener eligibility must be captured BEFORE whitespace.
    for (const std::string space : {" ", "\t", "\n", "\r", "\f", "\v"})
        run("legacy-opener-space/" + std::to_string(static_cast<unsigned char>(space[0])),
            "<body id=body><div id=target>A</" + space + "div x='a>b'><p id=after>tail</p></body>",
            {body({element("div", {{"id", "target"}}, {text("A")}), text("b'>"), after()})});
    for (const std::string boundary : {"+", "=", "'", "\"", "`", "!", "\v", "\xC2\xA0", "\xC3\xA9", "\xFF"})
        run("legacy-boundary/" + boundary,
            "<body id=body><div id=target>A</div" + boundary + " x='a>b'><p id=after>tail</p></body>",
            {body({element("div", {{"id", "target"}}, {text("A")}), text("b'>"), after()})});
    for (const std::string opener : {"7div", "_div", ":div", ".div", "-div", "", "!div", "\xC3\xA9"})
        run("legacy-nonalpha/" + opener,
            "<body id=body><div id=target>A</" + opener + " x='a>b'><p id=after>tail</p></div></body>",
            {body({element("div", {{"id", "target"}}, {text("A"), text("b'>"), after()})})});
    for (const std::string tail : {"x", "-x", "_x", ":x", ".x"})
        run("long-unknown-name/" + tail,
            "<body id=body><div id=target>A</div" + tail + " x='a>b'><p id=after>tail</p></div></body>",
            {body({element("div", {{"id", "target"}}, {text("A"), after()})})});
}
void eof_cases() {
    // Actual EOF in every suffix state, including quoted '>' and fake markup.
    // An unfinished accepted close is discarded. The insertion stack is local
    // and cannot be observed after EOF; its non-mutation is a control-flow rule.
    for (const std::string name : {"div", "DiV", "section", "missing", "STYLE", "SCRIPT"}) {
        const bool raw = name == "STYLE" || name == "SCRIPT";
        const std::string tag = raw ? (name == "STYLE" ? "style" : "script") : "div";
        for (size_t i = 0; i < suffixes.size(); ++i) {
            const auto& suffix = suffixes[i];
            for (size_t n = 0; n < suffix.size(); ++n)
                run("eof/" + name + "/" + std::to_string(i) + "/" + std::to_string(n),
                    "<body id=body><section id=host><" + tag + " id=target>A</" + name + suffix.substr(0, n),
                    {body({host({element(tag, {{"id", "target"}}, {text(raw && n == 0 ? "A</" + name : "A")})})})},
                    {"phantom", "after"});
        }
        for (const std::string suffix : {" x=&gt;", " x=&#62;", " x=&#x3e;", " x='&quot;>", " x=\"&#34;>", " x='><b id=phantom>"})
            run("eof-reference/" + name + suffix,
                "<body id=body><section id=host><" + tag + " id=target>A</" + name + suffix,
                {body({host({element(tag, {{"id", "target"}}, {text("A")})})})}, {"phantom", "after"});
    }
}
void lifecycle_and_adversarial() {
    Document doc;
    for (int i = 0; i < 64; ++i) {
        const bool odd = i % 2;
        const std::string id = odd ? "new" : "old";
        run_on(doc, "reparse/" + std::to_string(i),
            "<body id=body><div id=" + id + ">A</div x='><p id=phantom>'><p id=after>tail</p></body>",
            {body({element("div", {{"id", id}}, {text("A")}), after()})}, {odd ? "old" : "new", "phantom"});
    }
    run_on(doc, "reparse/empty", "", {}, {"old", "new", "phantom", "after", "body"});
    std::string suffix = " x='";
    for (int i = 0; i < 32768; ++i) suffix += "><b id=phantom>&quot;&#39;";
    suffix += "'>";
    run("large-quoted", "<body id=body><div id=target>A</div" + suffix + "<p id=after>tail</p></body>",
        {body({element("div", {{"id", "target"}}, {text("A")}), after()})}, {"phantom"});
    std::string names;
    for (int i = 0; i < 32768; ++i) names += " /x='><b id=phantom>'";
    run("large-many-attributes", "<body id=body><div id=target>A</div" + names + "><p id=after>tail</p></body>",
        {body({element("div", {{"id", "target"}}, {text("A")}), after()})}, {"phantom"});
}
} // namespace
int main() {
    context = "ABI";
    if (!check(AETHER_ABI_VERSION_GET_MAJOR(aether_abi_version()) == AETHER_ABI_VERSION_MAJOR, "compatible major") ||
        !check((aether_abi_capabilities() & AETHER_CAPABILITY_DOM) != 0, "DOM capability")) return 1;
    matched_suffixes();
    unknown_and_ancestors();
    boundary_bytes();
    legacy_guards();
    eof_cases();
    lifecycle_and_adversarial();
    std::printf("HTML end-tag suffixes: %d cases, %d failed cases, %d checks, %d failures\n", cases, failed_cases, checks, failures);
    return failures ? 1 : 0;
}
