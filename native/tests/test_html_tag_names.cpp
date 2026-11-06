#include <aether/aether.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
int checks = 0, failures = 0;
std::string context;
bool check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: %s\n", context.c_str(), message.c_str());
    }
    return ok;
}
std::string take(char* value) {
    check(value != nullptr, "owned ABI string exists");
    const std::string result = value ? value : "";
    aether_string_free(value);
    return result;
}
struct Document {
    aether_doc_t doc = nullptr;
    Document() { check(aether_doc_create(&doc) == AETHER_OK && doc, "create document"); }
    ~Document() { aether_doc_destroy(doc); }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    bool parse(const std::string& html) {
        const auto version = aether_doc_get_version(doc);
        const bool ok = check(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse HTML");
        check(aether_doc_get_version(doc) > version, "version advances");
        return ok;
    }
    aether_node_t id(const char* name) { return aether_doc_get_element_by_id(doc, name); }
};
void tag(aether_node_t node, const std::string& expected) {
    if (!check(node != nullptr, "element exists: " + expected)) return;
    check(aether_node_get_type(node) == AETHER_NODE_ELEMENT, "element type");
    // Never normalize the observed getter: this is the public semantic contract.
    check(take(aether_element_get_tag_name(node)) == expected, "literal tag name " + expected);
}
void originals() {
    // Preserve the four original defect reproducers byte-for-byte.
    const char* inputs[] = {
        "<html><body><input id='entry'></body></html>",
        "<HTML><BODY><INPUT id='entry'></BODY></HTML>",
        "<HtMl><BoDy><InPuT id='entry'></bOdY></hTmL>",
        "<html><body><INPUT id='entry'></body></html>"
    };
    for (unsigned i = 0; i < 4; ++i) {
        context = "original/" + std::to_string(i);
        Document d;
        d.parse(inputs[i]);
        auto root = aether_doc_get_document_element(d.doc);
        auto body = aether_doc_get_body(d.doc);
        auto input = d.id("entry");
        tag(root, "html"); tag(body, "body"); tag(input, "input");
        check(aether_node_get_parent(body) == root, "body parent");
        check(aether_node_get_parent(input) == body, "input parent");
        check(aether_element_is_focusable(input) == AETHER_TRUE, "input is focusable");
    }
}
void roots_and_focus() {
    context = "nonfirst HTML root and focus";
    Document d;
    d.parse("LEAD<!--Keep CASE--><DIV id=before></DIV><HTML id=root><BODY id=body>"
            "<INPUT id=input><BuTtOn id=button>Go</BUTTON><TEXTAREA id=textarea>Value</textarea>"
            "<SeLeCt id=select></SELECT><DIV id=ordinary></DIV><X-TAB id=tab tabindex=0></x-tab>"
            "<X-NOTAB id=notab TABINDEX=0></x-notab></BODY></html>");
    auto root = d.id("root");
    check(aether_doc_get_document_element(d.doc) == root, "select HTML, not first-root fallback");
    check(aether_doc_get_body(d.doc) == d.id("body"), "find uppercase BODY");
    tag(root, "html");
    auto sentinel = aether_node_get_parent(root);
    check(aether_node_get_type(sentinel) == AETHER_NODE_DOCUMENT, "document sentinel");
    check(aether_node_get_child_count(sentinel) == 4, "all four preceding/succeeding roots retained");
    check(take(aether_node_get_text_content(aether_node_get_child(sentinel, 0))) == "LEAD", "text case retained");
    check(aether_node_get_type(aether_node_get_child(sentinel, 1)) == AETHER_NODE_COMMENT, "comment retained");
    check(take(aether_node_get_text_content(aether_node_get_child(sentinel, 1))) == "Keep CASE", "comment case retained");
    check(aether_node_get_child(sentinel, 2) == d.id("before"), "earlier element retained");
    check(aether_node_get_child(sentinel, 3) == root, "HTML is fourth root");
    for (const char* name : {"input", "button", "textarea", "select"}) {
        tag(d.id(name), name);
        check(aether_element_is_focusable(d.id(name)) == AETHER_TRUE, std::string(name) + " focus");
    }
    check(aether_element_is_focusable(d.id("ordinary")) == AETHER_FALSE, "ordinary element is not focusable");
    check(aether_element_is_focusable(d.id("tab")) == AETHER_TRUE, "lowercase tabindex retained");
    check(aether_element_is_focusable(d.id("notab")) == AETHER_FALSE, "attribute names not normalized");
}
void names() {
    context = "complete name normalization";
    Document d;
    const std::vector<std::pair<std::string, std::string>> pairs = {
        {"DiV", "div"}, {"X-CuStOm", "x-custom"}, {"A_B:C.D9", "a_b:c.d9"},
        {"ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz"},
        {"x-ABCDEFGHIJKLMNOPQRSTUVWXYZ-09_:.", "x-abcdefghijklmnopqrstuvwxyz-09_:."},
        {"already-lower", "already-lower"},
        {"X" + std::string(8192, 'A') + "Z", "x" + std::string(8192, 'a') + "z"}
    };
    for (size_t i = 0; i < pairs.size(); ++i) {
        context = "complete name/" + std::to_string(i);
        d.parse("<" + pairs[i].first + " id=target>TEXT</" + pairs[i].second + ">");
        tag(d.id("target"), pairs[i].second);
        check(aether_node_get_child_count(d.id("target")) == 1, "one text child");
        const auto child = aether_node_get_child(d.id("target"), 0);
        check(aether_node_get_type(child) == AETHER_NODE_TEXT, "text child type");
        check(take(aether_node_get_text_content(child)) == "TEXT", "text unaffected");
    }
    for (int i = 0; i < 26; ++i) {
        context = "all ASCII initial letters/" + std::to_string(i);
        const std::string upper(1, static_cast<char>('A' + i));
        d.parse("<" + upper + " id=target />");
        tag(d.id("target"), std::string(1, static_cast<char>('a' + i)));
    }
}
void attributes() {
    context = "attributes values and IDs remain case sensitive";
    Document d;
    d.parse("<BODY><DiV id='AbC' ID='Other' class='Foo foo' DATA-X='UP&amp;' data-x='Down' "
            "DATA-X='Discard' data-url='https://Host/Path?Q=ABC' title='CAF\xC3\x89'>"
            "Text &lt;UPPER&gt;</dIv><SPAN id=abc></span></BODY>");
    auto node = d.id("AbC");
    tag(node, "div"); tag(d.id("abc"), "span");
    check(node != d.id("abc"), "IDs differing only in case stay distinct");
    check(!d.id("Other") && !d.id("ABC"), "uppercase attribute and ID value not folded");
    const std::map<std::string, std::string> expected = {
        {"id", "AbC"}, {"ID", "Other"}, {"class", "Foo foo"}, {"DATA-X", "UP&"},
        {"data-x", "Down"}, {"data-url", "https://Host/Path?Q=ABC"}, {"title", "CAF\xC3\x89"}
    };
    std::map<std::string, std::string> actual;
    const auto count = aether_element_get_attribute_count(node);
    check(count == static_cast<int32_t>(expected.size()), "attribute count and exact duplicate first-wins");
    for (int32_t i = 0; i < count; ++i)
        actual.emplace(take(aether_element_get_attribute_name(node, i)), take(aether_element_get_attribute_value(node, i)));
    check(actual == expected, "complete literal attribute map");
    for (const auto& [key, value] : expected)
        check(take(aether_element_get_attribute(node, key.c_str())) == value, "literal attribute lookup " + key);
    check(aether_node_get_child_count(node) == 1, "decoded markup not retokenized");
    const auto child = aether_node_get_child(node, 0);
    check(aether_node_get_type(child) == AETHER_NODE_TEXT, "decoded markup remains text");
    check(take(aether_node_get_text_content(child)) == "Text <UPPER>", "entities decoded once, text not case-folded");
}
void structure() {
    context = "void and self-closing siblings";
    Document d;
    const std::vector<std::pair<std::string, std::string>> voids = {
        {"AREA", "area"}, {"BaSe", "base"}, {"BR", "br"}, {"CoL", "col"},
        {"EMBED", "embed"}, {"Hr", "hr"}, {"IMG", "img"}, {"InPuT", "input"},
        {"LINK", "link"}, {"MeTa", "meta"}, {"PARAM", "param"}, {"SoUrCe", "source"},
        {"TRACK", "track"}, {"WbR", "wbr"}
    };
    std::string html = "<BODY id=body>";
    for (const auto& [opening, expected] : voids) html += "<" + opening + " id=" + expected + ">";
    html += "<X-SeLf id=self /><SPAN id=after></sPaN></body>";
    d.parse(html);
    auto body = d.id("body");
    check(aether_node_get_child_count(body) == 16, "all voids and self-close retain siblings");
    size_t index = 0;
    for (const auto& [opening, expected] : voids) {
        (void)opening;
        auto node = d.id(expected.c_str());
        tag(node, expected);
        check(aether_node_get_parent(node) == body, "void parent " + expected);
        check(aether_node_get_child(body, static_cast<int32_t>(index++)) == node, "void order " + expected);
        check(aether_node_get_child_count(node) == 0, "void has no children " + expected);
    }
    tag(d.id("self"), "x-self");
    check(aether_node_get_parent(d.id("after")) == body, "self-closed custom element retains sibling");
    context = "mixed nearest and stray end tags";
    d.parse("<BODY id=body><DiV id=outer><DIV id=inner></dIv></UNKNOWN><SpAn id=inside></SPAN>"
            "</DIV><P id=after></p></bOdY>");
    check(aether_node_get_parent(d.id("inner")) == d.id("outer"), "nested DIV parent");
    check(aether_node_get_parent(d.id("inside")) == d.id("outer"), "nearest mixed close and ignored stray preserve outer");
    check(aether_node_get_parent(d.id("after")) == d.id("body"), "outer mixed close preserves following sibling");
    tag(d.id("inner"), "div"); tag(d.id("inside"), "span");
}
void reparse() {
    context = "replacement lifecycle";
    Document d;
    for (int i = 0; i < 32; ++i) {
        context = "reparse/" + std::to_string(i);
        const bool upper = i % 2 == 0;
        d.parse(upper ? "<HTML id=new><BODY><INPUT id=UP></BODY></HTML>"
                      : "<html id=old><body><input id=down></body></html>");
        tag(aether_doc_get_document_element(d.doc), "html");
        tag(aether_doc_get_body(d.doc), "body");
        tag(d.id(upper ? "UP" : "down"), "input");
        check(!d.id(upper ? "old" : "new"), "old root ID removed");
        check(!d.id(upper ? "down" : "UP"), "old control ID removed");
        // Borrowed handles never cross parse calls; addresses may be reused.
    }
    d.parse("");
    check(!aether_doc_get_document_element(d.doc) && !aether_doc_get_body(d.doc), "empty reparse clears roots");
    check(!d.id("old") && !d.id("down"), "empty reparse clears IDs");
}
} // namespace

int main() {
    context = "ABI";
    check(aether_abi_version() == AETHER_ABI_VERSION_CURRENT, "binary ABI version unchanged");
    originals(); roots_and_focus(); names(); attributes(); structure(); reparse();
    std::printf("HTML tag names: %d checks; %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
