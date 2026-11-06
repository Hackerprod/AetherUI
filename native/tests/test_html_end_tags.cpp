#include <aether/aether.h>

#include <cstdio>
#include <map>
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

struct String {
    char* value;
    explicit String(char* p) : value(p) {}
    ~String() { aether_string_free(value); }
    String(const String&) = delete;
    String& operator=(const String&) = delete;
    bool equals(const std::string& expected) const { return value && expected == value; }
};

struct Node {
    aether_node_type_t type;
    std::string tag, id, text;
    std::vector<Node> children;
};
Node text(const std::string& value) { return {AETHER_NODE_TEXT, "", "", value, {}}; }
Node comment(const std::string& value) { return {AETHER_NODE_COMMENT, "", "", value, {}}; }
Node element(const std::string& tag, const std::string& id = "",
             const std::vector<Node>& children = {}) {
    return {AETHER_NODE_ELEMENT, tag, id, "", children};
}
Node span(const std::string& id, const std::string& value) {
    return element("span", id, {text(value)});
}
Node body(const std::vector<Node>& children, const std::string& id = "body") {
    return element("body", id, children);
}

// Every node is borrowed. Obtain the document node through a root's parent,
// never by casting the opaque document handle. Reparse invalidates all nodes.
void verify_node(aether_doc_t doc, aether_node_t actual, aether_node_t parent,
                 aether_node_t actual_body, bool in_body, const Node& expected,
                 const std::string& path) {
    if (!check(actual != nullptr, path + " exists")) return;
    check(aether_node_get_type(actual) == expected.type, path + " type");
    check(aether_node_get_parent(actual) == parent, path + " parent");
    if (expected.type == AETHER_NODE_ELEMENT) {
        String tag(aether_element_get_tag_name(actual));
        check(tag.equals(expected.tag), path + " canonical ASCII tag name");
        std::map<std::string, std::string> attrs;
        const int32_t count = aether_element_get_attribute_count(actual);
        check(count == (expected.id.empty() ? 0 : 1), path + " attribute count");
        for (int32_t i = 0; i < count; ++i) {
            String key(aether_element_get_attribute_name(actual, i));
            String value(aether_element_get_attribute_value(actual, i));
            if (check(key.value && value.value, path + " attribute strings"))
                check(attrs.emplace(key.value, value.value).second, path + " unique attribute");
        }
        const std::map<std::string, std::string> wanted = expected.id.empty()
            ? std::map<std::string, std::string>{}
            : std::map<std::string, std::string>{{"id", expected.id}};
        check(attrs == wanted, path + " complete attributes");
        if (!expected.id.empty())
            check(aether_doc_get_element_by_id(doc, expected.id.c_str()) == actual, path + " ID lookup");
    } else {
        String value(aether_node_get_text_content(actual));
        check(value.equals(expected.text), path + " exact text bytes");
    }
    in_body = in_body || expected.tag == "body";
    if (in_body) {
        auto ancestor = actual;
        // Bound traversal so a broken parent relation cannot hang the test.
        for (int i = 0; ancestor && ancestor != actual_body && i < 512; ++i)
            ancestor = aether_node_get_parent(ancestor);
        check(actual_body && ancestor == actual_body, path + " belongs to body");
    }
    check(aether_node_get_child_count(actual) == static_cast<int32_t>(expected.children.size()),
          path + " child count/order");
    for (size_t i = 0; i < expected.children.size(); ++i)
        verify_node(doc, aether_node_get_child(actual, static_cast<int32_t>(i)), actual,
                    actual_body, in_body, expected.children[i], path + "/" + std::to_string(i));
}

void verify_tree(aether_doc_t doc, const std::vector<Node>& roots, bool has_body) {
    auto actual_body = aether_doc_get_body(doc);
    check((actual_body != nullptr) == has_body, "body presence (no implicit wrapper)");
    auto root = aether_doc_get_document_element(doc);
    if (roots.empty()) {
        check(root == nullptr, "empty document has no invented nodes");
        return;
    }
    if (!check(root != nullptr, "document element exists")) return;
    auto document = aether_node_get_parent(root);
    if (!check(document != nullptr, "document parent exists")) return;
    check(aether_node_get_type(document) == AETHER_NODE_DOCUMENT, "document sentinel type");
    check(aether_node_get_parent(document) == nullptr, "document sentinel parent");
    check(aether_node_get_child_count(document) == static_cast<int32_t>(roots.size()), "document root count/order");
    for (size_t i = 0; i < roots.size(); ++i)
        verify_node(doc, aether_node_get_child(document, static_cast<int32_t>(i)), document,
                    actual_body, false, roots[i], "document/" + std::to_string(i));
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
    if (!check(aether_style_runtime_create(&options, &environment, nullptr, nullptr,
                                          &runtime) == AETHER_OK && runtime,
               "create fresh style runtime")) { cleanup(); return; }
    int64_t sheet = 0;
    int32_t rules = 0;
    if (!check(aether_style_runtime_add_stylesheet(runtime,
               "#after { width:11px } #host #after { width:37px }", 0, "end-tags.css",
               &sheet, &rules) == AETHER_OK, "add stylesheet")) { cleanup(); return; }
    check(rules == 2, "two style rules");
    auto target = aether_doc_get_element_by_id(doc, "after");
    check(target != nullptr, "style target exists");
    count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    check(count > 0 && updates, "resolve exclusively from body");
    int matched_updates = 0;
    for (int32_t i = 0; updates && i < count; ++i) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        const bool ok = check(aether_style_runtime_get_update(updates[i], &node, &handle,
                                                             nullptr) == AETHER_OK, "read style update");
        if (ok && target && node == target) {
            ++matched_updates;
            if (check(handle != nullptr, "computed style handle")) {
                aether_computed_style_t style{};
                if (check(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy style"))
                    check(style.width.unit == AETHER_CSS_UNIT_PX && style.width.value == 37.f,
                          "host descendant cascade width is 37px");
                aether_computed_style_free(&style);
            }
        }
        if (handle) aether_computed_style_handle_destroy(handle);
    }
    check(matched_updates == 1, "exactly one update for after (not one total update)");
    cleanup();
}

void run_on(Document& doc, const std::string& name, const std::string& html,
            const std::vector<Node>& roots, bool has_body = true,
            const std::vector<std::string>& absent = {}, bool style = false) {
    context = name;
    ++cases;
    const int before = failures;
    const auto version = aether_doc_get_version(doc.handle);
    if (check(doc.handle && aether_doc_parse_html(doc.handle, html.c_str()) == AETHER_OK, "parse HTML")) {
        check(aether_doc_get_version(doc.handle) > version, "parse advances document version");
        verify_tree(doc.handle, roots, has_body);
        for (const auto& id : absent)
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
    run_on(doc, name, html, roots, has_body, {}, style);
}

void fixture(const std::string& name, const std::string& html, const std::vector<Node>& children) {
    run(name, "<body id=body>" + html + "</body>", {body(children)});
}

void originals() {
    // These eight inputs are byte-for-byte the original public reproducer.
    // Expected trees are literal, not derived by reparsing corrected markup.
    const auto host = element("div", "host", {span("before", "A"), span("after", "B")});
    run("original/control", "<body><div id=host><span id=before>A</span><span id=after>B</span></div></body>", {body({host}, "")});
    run("original/stray-custom", "<body><div id=host><span id=before>A</span></x-stray><span id=after>B</span></div></body>", {body({host}, "")});
    run("original/stray-uppercase", "<body><div id=host><span id=before>A</span></X-STRAY><span id=after>B</span></div></body>", {body({host}, "")});
    run("original/stray-standard", "<body><div id=host><span id=before>A</span></section><span id=after>B</span></div></body>", {body({host}, "")});
    run("original/repeated-stray", "<body><div id=host></x-stray></x-other><span id=after>B</span></div></body>",
        {body({element("div", "host", {span("after", "B")})}, "")});
    run("original/stray-inside-custom", "<body><x-host id=host><x-inner></x-stray><span id=after>B</span></x-inner></x-host></body>",
        {body({element("x-host", "host", {element("x-inner", "", {span("after", "B")})})}, "")});
    run("original/matched-current", "<body id=host><div></div><span id=after>B</span></body>",
        {body({element("div"), span("after", "B")}, "host")});
    run("original/matched-ancestor", "<body id=host><div><x-inner></div><span id=after>B</span></body>",
        {body({element("div", "", {element("x-inner")}), span("after", "B")}, "host")});
}

void unmatched() {
    fixture("text-after-close", "<div id=host>A</x-missing>B<span id=after>C</span></div>",
            {element("div", "host", {text("A"), text("B"), span("after", "C")})});
    fixture("comment-after-close", "<div id=host></x-missing><!--c--><span id=after>A</span></div>",
            {element("div", "host", {comment("c"), span("after", "A")})});
    for (const auto& token : std::vector<std::pair<std::string, std::string>>{
            {"standard", "</section>"}, {"multiple", "</x-one></x-two></SECTION>"},
            {"suffix", "</div-extra>"}, {"prefix", "</di>"}, {"empty-invalid", "</></ >"}}) {
        fixture("unmatched/" + token.first, "<div id=host>" + token.second + "<span id=after>A</span></div>",
                {element("div", "host", {span("after", "A")})});
    }
    fixture("duplicate-closed", "<div id=host><x-child id=closed></x-child></x-child><span id=after>A</span></div>",
            {element("div", "host", {element("x-child", "closed"), span("after", "A")})});
    fixture("late-descendant-close", "<div id=closed><x-inner id=inner></div></x-inner><span id=after>A</span>",
            {element("div", "closed", {element("x-inner", "inner")}), span("after", "A")});
    fixture("void-extra-close", "<div id=host><img id=image></img><span id=after>A</span></div>",
            {element("div", "host", {element("img", "image"), span("after", "A")})});
    // Non-void self-closing syntax preserves AetherUI compatibility, not HTML parity.
    fixture("simplified-self-closing", "<div id=host><x-child id=closed /></x-child><span id=after>A</span></div>",
            {element("div", "host", {element("x-child", "closed"), span("after", "A")})});
}

void matched() {
    fixture("matched-top", "<div id=closed></div><span id=after>A</span>",
            {element("div", "closed"), span("after", "A")});
    fixture("matched-ancestor", "<div id=closed><x-inner id=inner></div><span id=after>A</span>",
            {element("div", "closed", {element("x-inner", "inner")}), span("after", "A")});
    fixture("matched-mixed-case", "<DiV id=closed><x-inner></dIv><span id=after>A</span>",
            {element("div", "closed", {element("x-inner")}), span("after", "A")});
    fixture("matched-custom", "<x-host id=closed><x-inner></X-HOST><span id=after>A</span>",
            {element("x-host", "closed", {element("x-inner")}), span("after", "A")});
    fixture("nearest-identical-name", "<div id=outer><div id=inner></div><span id=after>A</span></div><span id=tail>B</span>",
            {element("div", "outer", {element("div", "inner"), span("after", "A")}), span("tail", "B")});
    // A generic special-element barrier would break these existing matched
    // closes. These are simplified-parser compatibility, NOT HTML conformance.
    fixture("simplified-no-special-barrier", "<x-host id=closed><div id=inner></x-host><span id=after>A</span>",
            {element("x-host", "closed", {element("div", "inner")}), span("after", "A")});
    fixture("simplified-no-table-scope", "<div id=closed><table><tr><td></div><span id=after>A</span>",
            {element("div", "closed", {element("table", "", {element("tr", "", {element("td")})})}), span("after", "A")});
}

void document_boundaries() {
    run("initial-close", "</x-missing><body id=body><span id=after>A</span></body>", {body({span("after", "A")})});
    run("only-closes", "</x-missing></section>", {}, false);
    run("empty-input", "", {}, false);
    run("without-body", "<x-root id=host></x-missing><span id=after>A</span></x-root>",
        {element("x-root", "host", {span("after", "A")})}, false);
    run("between-roots", "<div id=first></div></x-missing><div id=after></div>",
        {element("div", "first"), element("div", "after")}, false);
    run("eof-unfinished-close", "<div id=host></x-missing", {element("div", "host")}, false);
    run("close-root-keeps-sentinel", "<html><body><div></HTML></x-missing><span id=after>A</span>",
        {element("html", "", {body({element("div")}, "")}), span("after", "A")});
    for (const auto& tag : {std::string("script"), std::string("style")}) {
        const auto close = tag == "script" ? "SCRIPT" : "STYLE";
        fixture("raw-text/" + tag,
                "<div id=host><" + tag + " id=raw>\"</x-missing>\"</" + close + "><span id=after>A</span></div>",
                {element("div", "host", {element(tag, "raw", {text("\"</x-missing>\"")}), span("after", "A")})});
    }
}

void deep_stack() {
    std::string html = "<body id=body>";
    for (int i = 0; i < 128; ++i) html += "<x-level id=l" + std::to_string(i) + ">";
    for (int i = 0; i < 256; ++i) html += "</x-missing>";
    html += "<span id=after>A</span>";
    for (int i = 0; i < 128; ++i) html += "</x-level>";
    html += "<span id=tail>B</span></body>";
    Node nested = span("after", "A");
    for (int i = 127; i >= 0; --i)
        nested = element("x-level", "l" + std::to_string(i), {nested});
    run("128-levels-256-unmatched", html, {body({nested, span("tail", "B")})});
}

void reparse() {
    context = "reparse";
    Document doc;
    run_on(doc, "reparse/first", "<body id=old-body><div id=old-host></x-missing><span id=old-after>A</span></div></body>",
           {body({element("div", "old-host", {span("old-after", "A")})}, "old-body")});
    // No saved borrowed handles or pointer comparisons cross parse calls.
    run_on(doc, "reparse/matched", "<body id=new-body><div id=new-closed><x-inner></div><span id=new-after>B</span></body>",
           {body({element("div", "new-closed", {element("x-inner")}), span("new-after", "B")}, "new-body")}, true,
           {"old-body", "old-host", "old-after"});
    run_on(doc, "reparse/empty", "", {}, false,
           {"old-body", "old-host", "old-after", "new-body", "new-closed", "new-after"});
    run_on(doc, "reparse/again", "<body id=body><div id=host></section><span id=after>C</span></div></body>",
           {body({element("div", "host", {span("after", "C")})})}, true,
           {"old-body", "old-host", "old-after", "new-body", "new-closed", "new-after"}, true);
}

void cascade() {
    for (const auto& token : std::vector<std::pair<std::string, std::string>>{
            {"control", ""}, {"custom", "</x-missing>"}, {"standard", "</section>"}}) {
        run("cascade/" + token.first,
            "<body id=body><div id=host><span id=before>A</span>" + token.second + "<span id=after>B</span></div></body>",
            {body({element("div", "host", {span("before", "A"), span("after", "B")})})}, true, true);
    }
}
} // namespace

int main() {
    if (!check(AETHER_ABI_VERSION_GET_MAJOR(aether_abi_version()) == AETHER_ABI_VERSION_MAJOR,
               "compatible ABI major")) return 1;
    const auto required = AETHER_CAPABILITY_DOM | AETHER_CAPABILITY_STYLE_RUNTIME;
    if (!check((aether_abi_capabilities() & required) == required, "DOM and style runtime capabilities")) return 1;
    originals();
    unmatched();
    matched();
    document_boundaries();
    deep_stack();
    reparse();
    cascade();
    std::printf("HTML end tags: %d cases, %d failed cases, %d checks, %d failures\n",
                cases, failed_cases, checks, failures);
    return failures ? 1 : 0;
}
