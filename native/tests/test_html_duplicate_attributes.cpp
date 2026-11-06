// Public C ABI only. All checks and calls remain active with NDEBUG.
#include <aether/aether.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
using Attributes = std::map<std::string, std::string>;
int checks = 0, failures = 0, cases = 0, failed_cases = 0;
std::string current;

bool check(bool ok, const std::string& detail) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: %s\n", current.c_str(), detail.c_str());
    }
    return ok;
}
void require(bool ok, const char* detail) {
    if (!check(ok, detail)) std::exit(2);
}
struct Case {
    int before = failures;
    explicit Case(const char* name) { current = name; ++cases; }
    ~Case() {
        const bool ok = failures == before;
        if (!ok) ++failed_cases;
        std::printf("CASE %s: %s\n", current.c_str(), ok ? "PASS" : "FAIL");
    }
};
struct String {
    char* value;
    explicit String(char* text) : value(text) {}
    ~String() { aether_string_free(value); }
    String(const String&) = delete;
    String& operator=(const String&) = delete;
    bool equals(const std::string& expected) const { return value && value == expected; }
};
struct Document {
    aether_doc_t handle = nullptr;
    Document() { require(aether_doc_create(&handle) == AETHER_OK && handle, "create document"); }
    ~Document() { aether_doc_destroy(handle); }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    aether_node_t parse(const std::string& html) {
        require(aether_doc_parse_html(handle, html.c_str()) == AETHER_OK, "parse HTML");
        auto node = aether_node_get_child(body(), 0);
        require(node != nullptr, "target obtained by structure, not ID");
        return node;
    }
    aether_node_t body() const {
        auto node = aether_doc_get_body(handle);
        require(node != nullptr, "body exists");
        return node;
    }
    aether_node_t find(const char* id) const { return aether_doc_get_element_by_id(handle, id); }
};

void attributes(aether_node_t node, const Attributes& expected, const std::string& path) {
    require(node != nullptr, "attribute target exists");
    const int count = aether_element_get_attribute_count(node);
    check(count == static_cast<int>(expected.size()), path + ": attribute count");
    require(count >= 0 && count <= 1024, "bounded attribute enumeration");
    Attributes actual;
    for (int i = 0; i < count; ++i) {
        String key(aether_element_get_attribute_name(node, i));
        String value(aether_element_get_attribute_value(node, i));
        if (check(key.value && value.value, path + ": enumeration strings")) {
            check(actual.emplace(key.value, value.value).second, path + ": unique name");
        }
    }
    // Compare the complete map independently of enumeration order, including
    // every getter for the large fixture, not just the small examples.
    check(actual == expected, path + ": complete first-value map");
    for (const auto& [key, value] : expected) {
        check(aether_element_has_attribute(node, key.c_str()) != 0, path + ": present " + key);
        String got(aether_element_get_attribute(node, key.c_str()));
        check(got.equals(value), path + ": getter " + key + " expected '" + value + "'");
    }
    String absent(aether_element_get_attribute(node, "data-absent"));
    check(!absent.value && !aether_element_has_attribute(node, "data-absent"),
          path + ": absent differs from present empty");
    check((aether_element_is_disabled(node) != 0) == (expected.count("disabled") != 0),
          path + ": disabled presence");
}

struct Node {
    std::string tag;
    Attributes attrs;
    std::vector<Node> children;
    std::string text;
};
Node element(std::string tag, Attributes attrs = {}, std::vector<Node> children = {}) {
    return {std::move(tag), std::move(attrs), std::move(children), ""};
}
Node text(const char* value) { return {"", {}, {}, value}; }
void tree_node(aether_node_t node, aether_node_t parent, const Node& expected,
               const std::string& path, unsigned depth = 0) {
    require(depth < 16, "bounded expected-tree depth");
    if (!check(node != nullptr, path + ": node exists")) return;
    check(aether_node_get_parent(node) == parent, path + ": parent");
    const bool is_text = expected.tag.empty();
    check(aether_node_get_type(node) == (is_text ? AETHER_NODE_TEXT : AETHER_NODE_ELEMENT),
          path + ": node type");
    String content(aether_node_get_text_content(node));
    check(content.equals(expected.text), path + ": node-local text");
    if (!is_text) {
        String tag(aether_element_get_tag_name(node));
        check(tag.equals(expected.tag), path + ": tag");
        attributes(node, expected.attrs, path);
    }
    const int count = aether_node_get_child_count(node);
    check(count == static_cast<int>(expected.children.size()), path + ": child count");
    // Traverse only the finite expected tree, never unbounded actual links.
    for (size_t i = 0; i < expected.children.size(); ++i) {
        tree_node(aether_node_get_child(node, static_cast<int>(i)), node, expected.children[i],
                  path + "/" + std::to_string(i), depth + 1);
    }
    check(aether_node_get_child(node, static_cast<int>(expected.children.size())) == nullptr,
          path + ": no extra child");
}
void tree(const Document& doc, std::vector<Node> children) {
    auto body = doc.body();
    auto parent = aether_node_get_parent(body);
    require(parent != nullptr, "body has document parent");
    check(aether_node_get_type(parent) == AETHER_NODE_DOCUMENT, "root is document");
    check(aether_node_get_parent(parent) == nullptr, "document has no parent");
    check(aether_node_get_child_count(parent) == 1 && aether_node_get_child(parent, 0) == body,
          "exact document child");
    check(aether_doc_get_document_element(doc.handle) == body, "document element");
    tree_node(body, parent, element("body", {}, std::move(children)), "body");
}

void dimensions(const Document& doc, aether_node_t target, const char* css,
                int expected_rules, float width, float height = -1.f) {
    aether_style_runtime_t runtime = nullptr;
    aether_style_runtime_options_t options{};
    const aether_style_environment_t environment{640.f, 480.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime)
                == AETHER_OK && runtime, "create style runtime");
    int64_t sheet = 0;
    int32_t rules = 0;
    check(aether_style_runtime_add_stylesheet(runtime, css, 0, "duplicates.css", &sheet, &rules)
              == AETHER_OK, "add stylesheet");
    check(rules == expected_rules, "compiled rule count");
    aether_style_update_t* updates = nullptr;
    const int count = aether_style_runtime_resolve_subtree(runtime, doc.body(), &updates);
    require(count > 0 && count <= 64 && updates, "bounded style updates");
    int matches = 0;
    for (int i = 0; i < count; ++i) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        const bool ok = check(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr)
                                  == AETHER_OK, "read style update");
        if (ok && node == target) {
            ++matches;
            aether_computed_style_t style{};
            if (check(handle && aether_computed_style_copy(handle, &style) == AETHER_OK,
                      "copy computed style")) {
                check(style.width.unit == AETHER_CSS_UNIT_PX && style.width.value == width,
                      "computed width expected " + std::to_string(width));
                if (height >= 0.f) {
                    check(style.height.unit == AETHER_CSS_UNIT_PX && style.height.value == height,
                          "computed height expected " + std::to_string(height));
                }
            }
            aether_computed_style_free(&style);
        }
        if (handle) aether_computed_style_handle_destroy(handle);
    }
    check(matches == 1, "exactly one target style update");
    aether_style_updates_free(updates, count);
    aether_style_runtime_destroy(runtime);
}
void simple(const char* name, const std::string& fragment, const Attributes& expected) {
    Case test(name);
    Document doc;
    doc.parse("<body><div " + fragment + "></div></body>");
    tree(doc, {element("div", expected)});
}
void styled(const char* name, const std::string& fragment, const Attributes& expected,
            float width, const char* css = ".first{width:17px}.last{width:31px}",
            int rules = 2, float height = -1.f) {
    Case test(name);
    Document doc;
    auto node = doc.parse("<body><div " + fragment + "></div></body>");
    tree(doc, {element("div", expected)});
    dimensions(doc, node, css, rules, width, height);
}
void set(aether_node_t node, const char* name, const char* value) {
    check(aether_element_set_attribute(node, name, value) == AETHER_OK,
          std::string("set ") + name);
}
void classes(aether_node_t node, const std::vector<const char*>& present,
             const std::vector<const char*>& absent) {
    for (const char* name : present)
        check(aether_element_has_class(node, name) != 0, std::string("class present ") + name);
    for (const char* name : absent)
        check(!aether_element_has_class(node, name), std::string("class absent ") + name);
}

void original_matrix() {
    simple("duplicate_single_quotes", "id='first' id='last'", {{"id", "first"}});
    simple("duplicate_double_quotes", "id=\"first\" id=\"last\"", {{"id", "first"}});
    simple("duplicate_unquoted", "id=first id=last", {{"id", "first"}});
    simple("duplicate_three_interleaved", "data-x=first title=kept data-x=middle lang=en data-x=last",
           {{"data-x", "first"}, {"title", "kept"}, {"lang", "en"}});
    simple("duplicate_first_empty", "data-x='' data-x=later", {{"data-x", ""}});
    simple("duplicate_first_boolean", "disabled disabled=later", {{"disabled", ""}});
    simple("duplicate_last_empty", "data-x=first data-x=''", {{"data-x", "first"}});
    simple("duplicate_last_boolean", "data-x=first data-x", {{"data-x", "first"}});
    simple("duplicate_first_entity", "data-x='A&amp;B' data-x=C", {{"data-x", "A&B"}});
    simple("duplicate_unquoted_path", "data-path=/first/path data-path=/last/path", {{"data-path", "/first/path"}});
    simple("duplicate_line_break", "data-x=first\r\n\tdata-x=last", {{"data-x", "first"}});
    simple("control_no_duplicates", "id=first title=kept data-x='A&amp;B'",
           {{"id", "first"}, {"title", "kept"}, {"data-x", "A&B"}});
    simple("control_empty_present", "data-x=''", {{"data-x", ""}});
    simple("control_boolean_present", "disabled", {{"disabled", ""}});
    simple("control_same_values", "data-x=first data-x=first", {{"data-x", "first"}});
    simple("control_distinct_names", "data-x=first data-xx=last", {{"data-x", "first"}, {"data-xx", "last"}});
    {
        Case test("duplicate_id_lookup");
        Document doc;
        auto node = doc.parse("<body><div id=first id=last></div></body>");
        tree(doc, {element("div", {{"id", "first"}})});
        check(doc.find("first") == node, "first ID resolves to structural child");
        check(!doc.find("last"), "discarded ID does not resolve");
    }
    {
        Case test("duplicate_consumes_quoted_payload");
        Document doc;
        doc.parse("<body><div data-x=first data-x='><span id=phantom>' title=kept>"
                  "<i id=child>real</i></div><p id=sibling></p></body>");
        tree(doc, {element("div", {{"data-x", "first"}, {"title", "kept"}},
                           {element("i", {{"id", "child"}}, {text("real")})}),
                   element("p", {{"id", "sibling"}})});
        check(!doc.find("phantom"), "discarded value does not create phantom");
    }
    styled("duplicate_class_cascade", "class=first class=last", {{"class", "first"}}, 17.f);
    styled("duplicate_style_cascade", "style='width:17px' style='width:31px'", {{"style", "width:17px"}}, 17.f);
    styled("control_class_first_only", "class=first", {{"class", "first"}}, 17.f);
    styled("control_class_last_only", "class=last", {{"class", "last"}}, 31.f);
    styled("control_style_first_only", "style='width:17px'", {{"style", "width:17px"}}, 17.f);
    styled("control_style_last_only", "style='width:31px'", {{"style", "width:31px"}}, 31.f);
    {
        Case test("control_explicit_set_attribute_replaces");
        Document doc;
        auto node = doc.parse("<body><div id=first id=last class=first class=last "
                              "style='width:17px' style='width:31px'></div></body>");
        set(node, "id", "manual"); set(node, "class", "manual"); set(node, "style", "width:43px");
        tree(doc, {element("div", {{"id", "manual"}, {"class", "manual"}, {"style", "width:43px"}})});
        check(doc.find("manual") == node && !doc.find("first") && !doc.find("last"), "mutated ID lookups");
        classes(node, {"manual"}, {"first", "last"});
        dimensions(doc, node, ".manual{width:59px}", 1, 43.f);
    }
    {
        Case test("control_same_id_different_elements");
        Document doc;
        auto first = doc.parse("<body><div id=same></div><p id=same></p></body>");
        tree(doc, {element("div", {{"id", "same"}}), element("p", {{"id", "same"}})});
        check(doc.find("same") == first, "same ID across elements keeps preorder");
    }
    {
        Case test("control_reparse_replaces_document");
        Document doc;
        doc.parse("<body><div id=old data-x=old></div></body>");
        auto node = doc.parse("<body><div id=new data-y=new></div></body>");
        tree(doc, {element("div", {{"id", "new"}, {"data-y", "new"}})});
        check(!doc.find("old") && doc.find("new") == node, "reparse replaces IDs");
    }
    {
        Case test("duplicate_repeated_reparse");
        Document doc;
        for (int i = 0; i < 3; ++i) {
            doc.parse("<body><div data-x=first data-x=last></div></body>");
            tree(doc, {element("div", {{"data-x", "first"}})});
        }
    }
    std::string many;
    Attributes expected;
    for (int i = 0; i < 256; ++i) {
        auto key = "data-k" + std::to_string(i);
        many += " " + key + "=first";
        expected.emplace(key, "first");
    }
    simple("control_256_unique_attributes", many, expected);
    for (int i = 255; i >= 0; --i) many += " data-k" + std::to_string(i) + "=last";
    simple("duplicate_256_attributes", many, expected);
}

void entity_and_cursor_cases() {
    simple("duplicate_entities_decoded_once", "data-x='&amp;amp;' data-x='&amp;'", {{"data-x", "&amp;"}});
    simple("duplicate_decimal_entity", "data-x='&#65;' data-x=B", {{"data-x", "A"}});
    simple("duplicate_hex_entity", "data-x='&#x41;' data-x=B", {{"data-x", "A"}});
    simple("decoded_quotes_remain_data", "data-x='&quot;&lt;&amp;' data-x=last title=kept",
           {{"data-x", "\"<&"}, {"title", "kept"}});
    {
        Case test("duplicate_decoded_id_lookup");
        Document doc;
        auto node = doc.parse("<body><div id='A&amp;B' id=last></div></body>");
        tree(doc, {element("div", {{"id", "A&B"}})});
        check(doc.find("A&B") == node && !doc.find("A&amp;B") && !doc.find("last"), "decoded ID only");
    }
    for (char quote : {'\'', '"'}) {
        Case test(quote == '\'' ? "discarded_single_quoted_syntax" : "discarded_double_quoted_syntax");
        Document doc;
        const char opposite = quote == '\'' ? '"' : '\'';
        const std::string payload = std::string(1, quote) + "><span id=phantom>" + opposite +
                                    " &quot;&lt;&amp; />" + quote;
        auto node = doc.parse("<body><div data-x=first data-x=" + payload + " title=kept>"
                              "<i id=child>real</i></div><p id=sibling></p></body>");
        tree(doc, {element("div", {{"data-x", "first"}, {"title", "kept"}},
                           {element("i", {{"id", "child"}}, {text("real")})}),
                   element("p", {{"id", "sibling"}})});
        check(!doc.find("phantom"), "no phantom ID");
        check(doc.find("child") == aether_node_get_child(node, 0), "real child ID");
        check(doc.find("sibling") == aether_node_get_child(doc.body(), 1), "real sibling ID");
    }
    {
        Case test("duplicate_unquoted_slash_is_value");
        Document doc;
        doc.parse("<body><div data-path=/first data-path=/last/><i></i></div><p></p></body>");
        tree(doc, {element("div", {{"data-path", "/first"}}, {element("i")}), element("p")});
    }
    {
        // This is preservation of AetherUI's existing explicit self-close,
        // not a claim that self-closing a non-void div follows HTML browsers.
        Case test("control_separated_slash_self_close");
        Document doc;
        doc.parse("<body><div data-path=/first data-path=/last /><i></i><p></p></body>");
        tree(doc, {element("div", {{"data-path", "/first"}}), element("i"), element("p")});
    }
    {
        Case test("duplicate_scope_per_element");
        Document doc;
        doc.parse("<body><div data-x=parent data-x=discarded><input data-x=void data-x=last>"
                  "<i data-x=child data-x=last></i></div><p data-x=sibling data-x=last></p></body>");
        tree(doc, {element("div", {{"data-x", "parent"}},
                           {element("input", {{"data-x", "void"}}), element("i", {{"data-x", "child"}})}),
                   element("p", {{"data-x", "sibling"}})});
    }
    {
        Case test("duplicate_single_key_adversary");
        Document doc;
        std::string html = "<body><div data-x=first";
        for (int i = 0; i < 4096; ++i) html += " data-x='><span id=phantom>'";
        doc.parse(html + " title=tail><i>real</i></div><p></p></body>");
        tree(doc, {element("div", {{"data-x", "first"}, {"title", "tail"}},
                           {element("i", {}, {text("real")})}), element("p")});
        check(!doc.find("phantom"), "repeated discarded payload has no ID");
    }
    // Preserve the original EOF inputs, but discard the whole unfinished opening.
    // Parse directly so positive Document::parse() still requires its target.
    const std::pair<const char*, const char*> incomplete[] = {
        {"control_eof_duplicate_name", "data-x"}, {"control_eof_duplicate_equals", "data-x="},
        {"control_eof_duplicate_single_quote", "data-x='unfinished"},
        {"control_eof_duplicate_double_quote", "data-x=\"unfinished"}
    };
    for (const auto& [name, tail] : incomplete) {
        Case test(name);
        Document doc;
        const std::string html = std::string("<body><div data-x=first ") + tail;
        require(aether_doc_parse_html(doc.handle, html.c_str()) == AETHER_OK, "parse HTML");
        tree(doc, {});
    }
}

void consumer_and_mutation_cases() {
    for (const char* initial : {"id='' class='' style=''", "id class style"}) {
        Case test(initial[2] == '=' ? "duplicate_special_empty_values" : "duplicate_special_boolean_values");
        Document doc;
        auto node = doc.parse(std::string("<body><div ") + initial +
                              " id=later class=last style='width:31px'></div></body>");
        tree(doc, {element("div", {{"id", ""}, {"class", ""}, {"style", ""}})});
        check(!doc.find("") && !doc.find("later"), "empty ID is not indexed");
        classes(node, {}, {"last"});
        dimensions(doc, node, "div{width:11px}.last{width:31px}", 2, 11.f);
    }
    for (const char* first : {"first extra", "first&#32;extra"}) {
        Case test(std::string(first).find('&') == std::string::npos ? "duplicate_classes_not_unioned" : "duplicate_classes_decoded_space");
        Document doc;
        auto node = doc.parse(std::string("<body><div class='") + first + "' class='last ignored'></div></body>");
        tree(doc, {element("div", {{"class", "first extra"}})});
        classes(node, {"first", "extra"}, {"last", "ignored"});
        dimensions(doc, node, ".first.extra{width:17px}.last{width:31px}.ignored{width:43px}", 3, 17.f);
    }
    styled("duplicate_style_important_ignored", "style='width:17px' style='width:31px!important'",
           {{"style", "width:17px"}}, 17.f);
    styled("duplicate_style_not_merged", "style='width:17px' style='height:31px'",
           {{"style", "width:17px"}}, 17.f, "div{width:11px;height:13px}", 1, 13.f);
    styled("duplicate_invalid_style_no_fallback", "style='width:nope' style='width:31px'",
           {{"style", "width:nope"}}, 11.f, "div{width:11px}", 1);
    styled("control_css_duplicate_declarations", "style='width:17px;width:31px'",
           {{"style", "width:17px;width:31px"}}, 31.f);
    styled("duplicate_id_selector", "id=first id=last", {{"id", "first"}}, 17.f,
           "#first{width:17px}#last{width:31px}");
    styled("duplicate_attribute_selector", "data-x=first data-x=last", {{"data-x", "first"}}, 17.f,
           "[data-x=first]{width:17px}[data-x=last]{width:31px}");
    {
        Case test("duplicate_discarded_id_used_by_other_node");
        Document doc;
        auto first = doc.parse("<body><div id=first id=last></div><p id=last></p></body>");
        tree(doc, {element("div", {{"id", "first"}}), element("p", {{"id", "last"}})});
        check(doc.find("first") == first && doc.find("last") == aether_node_get_child(doc.body(), 1),
              "discarded ID remains available to another element");
    }
    {
        Case test("setter_class_isolated");
        Document doc;
        auto node = doc.parse("<body><div class=first class=last></div></body>");
        set(node, "class", "manual");
        classes(node, {"manual"}, {"first", "last"});
        dimensions(doc, node, ".first{width:17px}.last{width:31px}.manual{width:43px}div{width:11px}", 4, 43.f);
        set(node, "class", "manual"); set(node, "class", "");
        tree(doc, {element("div", {{"class", ""}})});
        classes(node, {}, {"first", "last", "manual"});
        dimensions(doc, node, ".manual{width:43px}div{width:11px}", 2, 11.f);
    }
    {
        Case test("setter_style_isolated");
        Document doc;
        auto node = doc.parse("<body><div style='width:17px' style='width:31px'></div></body>");
        set(node, "style", "width:43px");
        dimensions(doc, node, "div{width:11px}", 1, 43.f);
        set(node, "style", "width:43px"); set(node, "style", "");
        tree(doc, {element("div", {{"style", ""}})});
        dimensions(doc, node, "div{width:11px}", 1, 11.f);
    }
    {
        Case test("setter_id_isolated");
        Document doc;
        auto node = doc.parse("<body><div id=first id=last></div></body>");
        set(node, "id", "manual");
        check(doc.find("manual") == node && !doc.find("first") && !doc.find("last"), "ID replacement");
        dimensions(doc, node, "#first{width:17px}#last{width:31px}#manual{width:43px}", 3, 43.f);
        set(node, "id", "manual"); set(node, "id", "");
        tree(doc, {element("div", {{"id", ""}})});
        check(!doc.find("manual") && !doc.find(""), "empty replacement not indexed");
    }
    {
        Case test("setter_generic_literal_and_empty");
        Document doc;
        auto node = doc.parse("<body><div data-x=first data-x=last></div></body>");
        set(node, "data-x", "A&amp;B");
        tree(doc, {element("div", {{"data-x", "A&amp;B"}})});
        set(node, "data-x", "A&amp;B"); set(node, "data-x", "");
        tree(doc, {element("div", {{"data-x", ""}})});
        set(node, "data-x", "final");
        tree(doc, {element("div", {{"data-x", "final"}})});
    }
    {
        Case test("duplicate_alternating_reparse");
        Document doc;
        for (int i = 0; i < 6; ++i) {
            const bool odd = i % 2 != 0;
            const char* id = odd ? "second" : "first";
            const char* old = odd ? "first" : "second";
            const char* key = odd ? "data-y" : "data-x";
            auto node = doc.parse(std::string("<body><div id=") + id + " id=discarded " +
                                  key + "=" + id + " " + key + "=last></div></body>");
            tree(doc, {element("div", {{"id", id}, {key, id}})});
            check(doc.find(id) == node && !doc.find(old) && !doc.find("discarded"), "fresh reparse IDs");
            // No handle from an earlier generation is retained or inspected.
        }
    }
}

void case_diagnostics() {
    // Preserve the historical inputs as normative ASCII identity regressions.
    simple("ascii_identical_uppercase_names", "DATA-X=First DATA-X=last", {{"data-x", "First"}});
    simple("ascii_equivalent_keys_first_wins", "DATA-X=First data-x=first",
           {{"data-x", "First"}});
    simple("ascii_equivalent_duplicate_names", "DATA-X=First data-x=first DATA-X=last data-x=Last",
           {{"data-x", "First"}});
}
} // namespace

int main() {
    current = "ABI negotiation";
    require(AETHER_ABI_VERSION_GET_MAJOR(aether_abi_version()) == AETHER_ABI_VERSION_MAJOR,
            "ABI major version");
    constexpr auto capabilities = AETHER_CAPABILITY_DOM | AETHER_CAPABILITY_STYLE_RUNTIME;
    require((aether_abi_capabilities() & capabilities) == capabilities, "DOM and style capabilities");
    require(aether_computed_style_size() == sizeof(aether_computed_style_t), "computed style ABI size");
    std::printf("ABI=%u style_size=%zu NDEBUG=%s\n", aether_abi_version(), aether_computed_style_size(),
#ifdef NDEBUG
                "yes"
#else
                "no"
#endif
    );
    original_matrix();
    entity_and_cursor_cases();
    consumer_and_mutation_cases();
    case_diagnostics();
    std::printf("SUMMARY cases=%d failed_cases=%d checks=%d failures=%d\n", cases, failed_cases, checks, failures);
    return failures ? 1 : 0;
}
