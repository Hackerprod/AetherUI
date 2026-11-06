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

struct Document {
    aether_doc_t handle = nullptr;
    Document() { check(aether_doc_create(&handle) == AETHER_OK && handle, "create document"); }
    ~Document() { if (handle) aether_doc_destroy(handle); }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
};

std::string take(char* value, const std::string& detail) {
    check(value != nullptr, detail + " owned string");
    const std::string result = value ? value : "";
    aether_string_free(value);
    return result;
}

using Attributes = std::map<std::string, std::string>;
struct Node {
    aether_node_type_t type;
    std::string name, value;
    Attributes attributes;
    std::vector<Node> children;
};
Node text(std::string value) { return {AETHER_NODE_TEXT, "", std::move(value), {}, {}}; }
Node comment(std::string value) { return {AETHER_NODE_COMMENT, "", std::move(value), {}, {}}; }
Node element(std::string name, std::vector<Node> children = {}, Attributes attributes = {}) {
    return {AETHER_NODE_ELEMENT, std::move(name), "", std::move(attributes), std::move(children)};
}
Node span() { return element("span", {text("B")}, {{"id", "after"}}); }
Node body(std::vector<Node> children) {
    return element("body", std::move(children), {{"id", "fixture"}});
}

// Validate all raw relations before normalizing. A repeated handle, too much
// depth or a malformed count fails instead of allowing an unbounded traversal.
Node read_node(aether_node_t node, aether_node_t parent,
               std::set<aether_node_t>& visited, const std::string& path, int depth = 0) {
    if (!check(node && depth <= 64 && visited.size() < 10000, path + " bounded node")) return text("");
    if (!check(visited.insert(node).second, path + " unique node handle")) return text("");
    check(aether_node_get_parent(node) == parent, path + " reciprocal parent");
    Node result{aether_node_get_type(node), "", "", {}, {}};
    check(result.type == AETHER_NODE_DOCUMENT || result.type == AETHER_NODE_ELEMENT ||
          result.type == AETHER_NODE_TEXT || result.type == AETHER_NODE_COMMENT, path + " known type");
    if (result.type == AETHER_NODE_ELEMENT) {
        result.name = take(aether_element_get_tag_name(node), path + " tag");
        const int32_t count = aether_element_get_attribute_count(node);
        if (!check(count >= 0 && count <= 100, path + " bounded attribute count")) return result;
        for (int32_t i = 0; i < count; ++i) {
            auto key = take(aether_element_get_attribute_name(node, i), path + " attribute name");
            auto value = take(aether_element_get_attribute_value(node, i), path + " attribute value");
            check(result.attributes.emplace(std::move(key), std::move(value)).second,
                  path + " unique attribute name");
        }
    } else if (result.type == AETHER_NODE_TEXT || result.type == AETHER_NODE_COMMENT) {
        result.value = take(aether_node_get_text_content(node), path + " text");
    }
    const int32_t count = aether_node_get_child_count(node);
    if (!check(count >= 0 && count <= 10000, path + " bounded child count")) return result;
    for (int32_t i = 0; i < count; ++i)
        result.children.push_back(read_node(aether_node_get_child(node, i), node, visited,
                                           path + "/" + std::to_string(i), depth + 1));
    return result;
}

void normalize(Node& node) {
    std::vector<Node> children;
    for (auto& child : node.children) {
        normalize(child);
        // Only contiguous text siblings may merge, never across an element or
        // comment, or across parent boundaries. Do not drop empty text nodes.
        if (!children.empty() && children.back().type == AETHER_NODE_TEXT && child.type == AETHER_NODE_TEXT)
            children.back().value += child.value;
        else children.push_back(std::move(child));
    }
    node.children = std::move(children);
}

void compare(const Node& actual, const Node& expected, const std::string& path) {
    check(actual.type == expected.type, path + " type");
    check(actual.name == expected.name, path + " canonical ASCII tag name");
    check(actual.value == expected.value, path + " exact text bytes");
    check(actual.attributes == expected.attributes, path + " complete attributes");
    check(actual.children.size() == expected.children.size(), path + " child count/order");
    for (size_t i = 0; i < actual.children.size() && i < expected.children.size(); ++i)
        compare(actual.children[i], expected.children[i], path + "/" + std::to_string(i));
}

// This second pass only follows a previously bounded, validated tree. All IDs
// in these fixtures are unique. No private DOM headers or opaque-handle casts.
void verify_ids(aether_doc_t doc, aether_node_t node, const std::set<aether_node_t>& visited, int depth = 0) {
    if (!node || depth > 64 || !visited.count(node)) return;
    if (aether_node_get_type(node) == AETHER_NODE_ELEMENT) {
        char* owned_id = aether_element_get_attribute(node, "id");
        const std::string id = owned_id ? owned_id : "";
        aether_string_free(owned_id);
        if (!id.empty()) check(aether_doc_get_element_by_id(doc, id.c_str()) == node, "ID lookup " + id);
    }
    for (int32_t i = 0; i < aether_node_get_child_count(node); ++i)
        verify_ids(doc, aether_node_get_child(node, i), visited, depth + 1);
}

void verify_tree(aether_doc_t doc, const std::vector<Node>& roots, bool has_body) {
    auto root = aether_doc_get_document_element(doc);
    check((aether_doc_get_body(doc) != nullptr) == has_body, "body presence");
    if (roots.empty()) { check(!root, "empty document has no invented nodes"); return; }
    if (!check(root != nullptr, "document root exists")) return;
    auto document = aether_node_get_parent(root);
    if (!check(document != nullptr, "document sentinel exists")) return;
    check(aether_node_get_type(document) == AETHER_NODE_DOCUMENT, "document sentinel type");
    std::set<aether_node_t> visited;
    const int before = failures;
    Node actual = read_node(document, nullptr, visited, "document");
    if (before == failures) verify_ids(doc, document, visited);
    normalize(actual);
    compare(actual, {AETHER_NODE_DOCUMENT, "", "", {}, roots}, "document");
}

void verify_style(aether_doc_t doc) {
    aether_style_runtime_t runtime = nullptr;
    aether_style_update_t* updates = nullptr;
    int32_t count = 0;
    auto cleanup = [&] {
        if (updates) aether_style_updates_free(updates, count);
        if (runtime) aether_style_runtime_destroy(runtime);
    };
    aether_style_runtime_options_t options{};
    const aether_style_environment_t environment{640.f, 480.f, 1.f, 16.f, 0, 0, 0};
    if (!check(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK && runtime,
               "create style runtime")) { cleanup(); return; }
    int64_t sheet = 0;
    int32_t rules = 0;
    if (!check(aether_style_runtime_add_stylesheet(runtime,
               "#after { width:11px } #host > #after.kept { width:37px }", 0, "tag-open.css",
               &sheet, &rules) == AETHER_OK, "add stylesheet")) { cleanup(); return; }
    check(rules == 2, "two style rules");
    auto target = aether_doc_get_element_by_id(doc, "after");
    check(target != nullptr, "style target exists");
    count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    check(count > 0 && updates, "resolve from body");
    int target_updates = 0;
    for (int32_t i = 0; updates && i < count; ++i) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        const bool ok = check(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK,
                              "read style update");
        if (ok && target && node == target) {
            ++target_updates;
            if (check(handle != nullptr, "computed style handle")) {
                aether_computed_style_t style{};
                if (check(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy style"))
                    check(style.width.unit == AETHER_CSS_UNIT_PX && style.width.value == 37.f,
                          "direct child ID/class cascade width is 37px");
                aether_computed_style_free(&style);
            }
        }
        if (handle) aether_computed_style_handle_destroy(handle);
    }
    check(target_updates == 1, "exactly one update for target, not one total update");
    cleanup();
}

void run_on(Document& doc, const std::string& name, const std::string& html,
            const std::vector<Node>& roots, bool has_body = true, bool style = false,
            const std::vector<std::string>& absent_ids = {}) {
    context = name;
    ++cases;
    const int before = failures;
    const auto version = aether_doc_get_version(doc.handle);
    if (check(doc.handle && aether_doc_parse_html(doc.handle, html.c_str()) == AETHER_OK, "parse HTML")) {
        check(aether_doc_get_version(doc.handle) > version, "document version advances");
        verify_tree(doc.handle, roots, has_body);
        for (const auto& id : absent_ids)
            check(!aether_doc_get_element_by_id(doc.handle, id.c_str()), "absent ID " + id);
        if (style) verify_style(doc.handle);
    }
    if (failures != before) ++failed_cases;
    std::printf("%s %s\n", failures == before ? "PASS" : "FAIL", name.c_str());
}

void run(const std::string& name, const std::string& html, const std::vector<Node>& roots,
         bool has_body = true, bool style = false) {
    context = name;
    Document doc;
    run_on(doc, name, html, roots, has_body, style);
}
void fixture(const std::string& name, const std::string& input, const std::vector<Node>& children) {
    run(name, "<body id='fixture'>" + input + "</body>", {body(children)});
}

void originals() {
    // The 19 original reproducer inputs are preserved byte-for-byte, including
    // its closed wrapper. Dedicated cases below test genuine EOF separately.
    fixture("original/comparison with spaces", "1 < 2 and 3 > 1", {text("1 < 2 and 3 > 1")});
    fixture("original/comparison without spaces", "1<2 and 3>1", {text("1<2 and 3>1")});
    // The malformed end tag is still consumed: this is not a whole-browser oracle.
    fixture("original/spaced apparent tag", "< div>hello</ div>", {text("< div>hello")});
    fixture("original/underscore start", "<_x>hello", {text("<_x>hello")});
    fixture("original/hyphen start", "<-x>hello", {text("<-x>hello")});
    fixture("original/colon start", "<:x>hello", {text("<:x>hello")});
    fixture("original/dot start", "<.x>hello", {text("<.x>hello")});
    fixture("original/tab after angle", "a<\tspan>b", {text("a<\tspan>b")});
    fixture("original/line feed after angle", "a<\nspan>b", {text("a<\nspan>b")});
    fixture("original/form feed after angle", "a<\fspan>b", {text("a<\fspan>b")});
    fixture("original/carriage return after angle", "a<\rspan>b", {text("a<\rspan>b")});
    fixture("original/real following tag swallowed", "A < 2 <span id='after'>B</span>C",
            {text("A < 2 "), span(), text("C")});
    fixture("original/terminal angle", "angle <", {text("angle <")});
    fixture("original/invalid punctuation retained", "a<$50>b", {text("a<$50>b")});
    fixture("original/entity after literal angle", "a< &amp; b", {text("a< & b")});
    fixture("original/non-ASCII retained", "a<\xc3\xb1>b", {text("a<\xc3\xb1>b")});
    fixture("original/valid ASCII tag", "<span>hello</span>", {element("span", {text("hello")})});
    fixture("original/encoded angle", "1 &lt; 2 and 3 > 1", {text("1 < 2 and 3 > 1")});
    fixture("original/valid custom element", "<x-widget>hello</x-widget>", {element("x-widget", {text("hello")})});
}

void invalid_openings() {
    for (const auto& input : {"<7>", "<0x>", "<_x>", "<-x>", "<:x>", "<.x>"})
        fixture(std::string("invalid/") + input, input, {text(input)});
    for (const auto& whitespace : std::vector<std::pair<std::string, std::string>>{
             {"space", " "}, {"tab", "\t"}, {"LF", "\n"}, {"FF", "\f"},
             {"CR", "\r"}, {"CRLF", "\r\n"}, {"VT", "\v"}}) {
        const auto input = "a<" + whitespace.second + "span>b";
        fixture("whitespace/" + whitespace.first, input, {text(input)});
    }
    for (const char sign : std::string("$=>\"'()[]+*%&`")) {
        const std::string input = std::string("a<") + sign + "x>b";
        fixture("punctuation/" + std::to_string(static_cast<unsigned char>(sign)), input, {text(input)});
    }
    for (const auto& word : {"\xc3\xb1", "\xe4\xb8\xad", "\xf0\x9f\x98\x80"}) {
        const auto input = std::string("<") + word + ">";
        fixture("UTF8/" + std::string(word), input, {text(input)});
    }
    // Exhaust every nonzero ASCII byte except letters and the dispatcher's
    // separate ! / ? branches. Each byte is immediately before actual EOF.
    for (int byte = 1; byte < 128; ++byte) {
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            byte == '!' || byte == '/' || byte == '?') continue;
        const auto input = std::string("<") + static_cast<char>(byte);
        run("ASCII invalid EOF/" + std::to_string(byte), "<body id=fixture>" + input, {body({text(input)})});
    }
    for (const auto& input : {"<", "<<", "<<<", "< ", "<7", "<\r\n", "< <7 << 2"})
        run(std::string("real EOF/") + input, std::string("<body id=fixture>") + input, {body({text(input)})});
    const std::string repeated(4096, '<');
    run("4096 angles progress", "<body id=fixture>" + repeated, {body({text(repeated)})});
    run("document-level literal", "<7", {text("<7")}, false);
    run("document-level reconsume", "<<span id=after>B</span>", {text("<"), span()}, false);
    run("empty document", "", {}, false);
}

void reconsume_and_entities() {
    fixture("reconsume/tag", "<<span id=after>B</span>", {text("<"), span()});
    fixture("reconsume/repeated tag", "<<<span id=after>B</span>", {text("<<"), span()});
    fixture("reconsume/comment", "<<!--c-->Z", {text("<"), comment("c"), text("Z")});
    fixture("reconsume/declaration", "<<!DOCTYPE html>Y", {text("<Y")});
    fixture("reconsume/PI", "<<?probe?><span id=after>B</span>", {text("<"), span()});
    fixture("reconsume/matched close", "<div id=host>A<</div><span id=after>B</span>",
            {element("div", {text("A<")}, {{"id", "host"}}), span()});
    fixture("reconsume/unmatched close", "<div id=host>A<</x-missing>B<span id=after>B</span></div>",
            {element("div", {text("A<B"), span()}, {{"id", "host"}})});
    fixture("entity/not retokenized", "<&lt;span>", {text("<<span>")});
    fixture("entity/after space", "< &amp; b", {text("< & b")});
    fixture("entity/encoded tag", "&lt;span&gt;", {text("<span>")});
    fixture("entity/single decode", "&amp;lt;", {text("&lt;")});
    fixture("entity/numeric angle", "<&#60;span>", {text("<<span>")});
}

void compatibility() {
    for (const char initial : std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")) {
        const auto tag = std::string(1, initial) + "-tag";
        std::string close = tag;
        for (auto& c : close) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            else if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
        }
        fixture("ASCII initial/" + tag, "<" + tag + " id=valid>X</" + close + "><span id=after>B</span>",
                {element(std::string(1, initial >= 'A' && initial <= 'Z' ? static_cast<char>(initial + ('a' - 'A')) : initial) + "-tag",
                         {text("X")}, {{"id", "valid"}}), span()});
    }
    for (const auto& tag : {"h1", "x-widget", "a_b", "a:b", "a.b"})
        fixture(std::string("name tail/") + tag, std::string("<") + tag + ">X</" + tag + ">",
                {element(tag, {text("X")})});
    fixture("voids and simplified self close", "<BR><img src=x.png><x-child /><span id=after>B</span>",
            {element("br"), element("img", {}, {{"src", "x.png"}}), element("x-child"), span()});
    fixture("attributes preserve complete map",
            "<SpAn id=after class='kept' title='a>b &amp; c' data-x=https://x/y ID=Upper "
            "id=ignored class=lost hidden></sPaN>",
            {element("span", {}, {{"id", "after"}, {"class", "kept"}, {"title", "a>b & c"},
                                  {"data-x", "https://x/y"}, {"ID", "Upper"}, {"hidden", ""}})});
    fixture("comment content", "<!-- <2> --><span id=after>B</span>", {comment(" <2> "), span()});
    run("comment incomplete", "<body id=fixture><!-- <2>", {body({comment(" <2>")})});
    run("doctype before body", "<!DOCTYPE html><body id=fixture><span id=after>B</span>", {body({span()})});
    fixture("declarations retained behavior", "<!x><!><span id=after>B</span>", {span()});
    run("declaration incomplete", "<body id=fixture><!", {body({})});
    fixture("processing instruction", "<?probe?><span id=after>B</span>", {span()});
    run("processing instruction incomplete", "<body id=fixture><?", {body({})});
    for (const auto& tag : {std::string("script"), std::string("STYLE")}) {
        const auto close = tag == "script" ? "ScRiPt" : "sTyLe";
        const std::string raw = "A<2 < span> &lt; <!--x--> <b>X";
        fixture("raw/" + tag, "<" + tag + " id=raw>" + raw + "</" + close + " ><span id=after>B</span>",
                {element(tag == "script" ? "script" : "style", {text(raw)}, {{"id", "raw"}}), span()});
    }
    run("raw EOF close is not boundary", "<body id=fixture><script>x</script",
        {body({element("script", {text("x</script")})})});
    fixture("raw VT is not boundary", "<style>x</style\v>y</style><span id=after>B</span>",
            {element("style", {text("x</style\v>y")}), span()});
    // These diagnostics deliberately preserve simplified-parser differences.
    run("diagnostic/end tag EOF consumed", "<body id=fixture></", {body({})});
    fixture("diagnostic/space after end angle", "<div>A</ div>B", {element("div", {text("A")}), text("B")});
    // Historical diagnostic input: EOF now discards the unfinished opening.
    run("diagnostic/valid incomplete start tag", "<body id=fixture><span", {body({})});
    run("diagnostic/PI without terminator", "<body id=fixture><?probe><span>B</span>", {body({})});
}

void integration() {
    const std::string html = "<body><div id=host>A < 2 <span id=after class=kept>B</span>C</div><p id=tail>D</p></body>";
    const std::vector<Node> expected = {element("body", {
        element("div", {text("A < 2 "), element("span", {text("B")}, {{"id", "after"}, {"class", "kept"}}), text("C")}, {{"id", "host"}}),
        element("p", {text("D")}, {{"id", "tail"}})})};
    run("tree and real style consumer", html, expected, true, true);
    context = "reparse";
    Document doc;
    run_on(doc, "reparse/old ID", "<body id=fixture><i id=obsolete>old</i></body>",
           {body({element("i", {text("old")}, {{"id", "obsolete"}})})});
    run_on(doc, "reparse/corrected tree", html, expected, true, true, {"obsolete", "fixture"});
    // Every handle is reacquired after parse. Never inspect a stale pointer.
    run_on(doc, "reparse/new ID", "<body id=fixture><p id=new>C</p></body>",
           {body({element("p", {text("C")}, {{"id", "new"}})})}, true, false, {"obsolete", "after", "host", "tail"});
    run_on(doc, "reparse/empty", "", {}, false, false, {"fixture", "new", "after"});
}
} // namespace

int main() {
    context = "ABI";
    if (!check(AETHER_ABI_VERSION_GET_MAJOR(aether_abi_version()) == AETHER_ABI_VERSION_MAJOR, "ABI major") ||
        !check((aether_abi_capabilities() & (AETHER_CAPABILITY_DOM | AETHER_CAPABILITY_STYLE_RUNTIME)) ==
               (AETHER_CAPABILITY_DOM | AETHER_CAPABILITY_STYLE_RUNTIME), "required capabilities")) return 2;
    (void)aether_paint_command_size();
    originals();
    invalid_openings();
    reconsume_and_entities();
    compatibility();
    integration();
    std::printf("HTML tag-open: %d/%d cases passed; %d checks; %d failures\n",
                cases - failed_cases, cases, checks, failures);
    return failures ? 1 : 0;
}
