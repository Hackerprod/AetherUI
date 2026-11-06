#include <aether/aether.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
using Attributes = std::map<std::string, std::string>;
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
    std::string tag, text;
    Attributes attrs;
    std::vector<Node> children;
};
Node text(const std::string& value) { return {"", value, {}, {}}; }
Node element(const std::string& tag, const Attributes& attrs, const std::vector<Node>& children = {}) {
    return {tag, "", attrs, children};
}
Node raw(const std::string& tag, const std::string& value, const std::string& id = "raw") {
    return element(tag, {{"id", id}}, value.empty() ? std::vector<Node>{} : std::vector<Node>{text(value)});
}
Node after() { return element("p", {{"id", "after"}}, {text("tail")}); }

// Every node handle below is borrowed from the document. Only ABI strings and
// the owning document are released, never the nodes or the parent handles.
void verify_node(aether_doc_t doc, aether_node_t actual, aether_node_t parent,
                 const Node& expected, const std::string& path) {
    if (!check(actual != nullptr, path + " exists")) return;
    const bool is_text = expected.tag.empty();
    check(aether_node_get_type(actual) == (is_text ? AETHER_NODE_TEXT : AETHER_NODE_ELEMENT), path + " type");
    check(aether_node_get_parent(actual) == parent, path + " parent");
    if (is_text) {
        String value(aether_node_get_text_content(actual));
        check(value.equals(expected.text), path + " exact text bytes");
    } else {
        String tag(aether_element_get_tag_name(actual));
        check(tag.equals(expected.tag), path + " canonical ASCII tag name");
        const int32_t count = aether_element_get_attribute_count(actual);
        check(count == static_cast<int32_t>(expected.attrs.size()), path + " attribute count");
        Attributes attrs;
        for (int32_t i = 0; i < count; ++i) {
            String key(aether_element_get_attribute_name(actual, i));
            String val(aether_element_get_attribute_value(actual, i));
            if (check(key.value && val.value, path + " attribute strings"))
                check(attrs.emplace(key.value, val.value).second, path + " unique attribute");
        }
        check(attrs == expected.attrs, path + " complete attributes");
        for (const auto& pair : expected.attrs) {
            String value(aether_element_get_attribute(actual, pair.first.c_str()));
            check(value.equals(pair.second), path + " lookup attribute " + pair.first);
            if (pair.first == "id")
                check(aether_doc_get_element_by_id(doc, pair.second.c_str()) == actual, path + " ID index");
        }
    }
    check(aether_node_get_child_count(actual) == static_cast<int32_t>(expected.children.size()), path + " child count");
    for (size_t i = 0; i < expected.children.size(); ++i)
        verify_node(doc, aether_node_get_child(actual, static_cast<int32_t>(i)), actual,
                    expected.children[i], path + "/" + std::to_string(i));
}
void run_on(Document& doc, const std::string& name, const std::string& html,
            const std::vector<Node>& children, const std::vector<std::string>& absent = {"phantom"}) {
    context = name;
    ++cases;
    const int before = failures;
    if (check(doc.handle && aether_doc_parse_html(doc.handle, html.c_str()) == AETHER_OK, "parse")) {
        auto body = aether_doc_get_body(doc.handle);
        if (check(body != nullptr, "body exists")) {
            check(aether_node_get_type(body) == AETHER_NODE_ELEMENT, "body element type");
            String tag(aether_element_get_tag_name(body));
            check(tag.equals("body"), "body spelling");
            check(aether_node_get_child_count(body) == static_cast<int32_t>(children.size()), "body child count");
            for (size_t i = 0; i < children.size(); ++i)
                verify_node(doc.handle, aether_node_get_child(body, static_cast<int32_t>(i)), body,
                            children[i], "body/" + std::to_string(i));
        }
        for (const auto& id : absent)
            check(!aether_doc_get_element_by_id(doc.handle, id.c_str()), "absent ID " + id);
    }
    if (failures != before) ++failed_cases;
}
void run(const std::string& name, const std::string& html, const std::vector<Node>& children) {
    context = name;
    Document doc;
    run_on(doc, name, html, children);
}
// The expected strings below are literal or built from known literal segments;
// none is produced by the parser or its entity decoder.
void fixture(const std::string& name, const std::string& opening, const std::string& tag,
             const std::string& source, const std::string& expected, const std::string& closing) {
    run(name, "<body><" + opening + " id=raw>" + source + closing +
              "<p id=after>tail</p></body>", {raw(tag, expected), after()});
}
std::string casing(std::string tag, unsigned mask) {
    for (size_t i = 0; i < tag.size(); ++i)
        if (mask & (1u << i)) tag[i] = static_cast<char>(tag[i] - 'a' + 'A');
    return tag;
}
void case_pairs() {
    for (const std::string tag : {"textarea", "title"})
        for (unsigned a = 0; a < (1u << tag.size()); ++a)
            for (unsigned b = 0; b < (1u << tag.size()); ++b)
                fixture("case/" + tag + "/" + std::to_string(a) + "/" + std::to_string(b),
                        casing(tag, a), tag, "A<b id=phantom>&amp;B", "A<b id=phantom>&B",
                        "</" + casing(tag, b) + ">");
}
void boundaries() {
    for (const std::string tag : {"textarea", "title"}) {
        const std::string upper = casing(tag, 255);
        // ABI input is NUL-terminated, so every representable boundary byte
        // except NUL is checked. CR normalization is observable in rejected text.
        for (unsigned byte = 1; byte <= 255; ++byte) {
            const char c = static_cast<char>(byte);
            const bool delimiter = c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
            const std::string candidate = "</" + upper + c + (c == '>' ? "" : ">");
            fixture("close-byte/" + tag + "/" + std::to_string(byte), upper, tag,
                    delimiter ? "A" : "A" + candidate, delimiter ? "A" : "A" + candidate,
                    delimiter ? candidate : "</" + tag + ">");
        }
        for (const std::string bad : {"</ ", "</\t", "</\n", "< /", "<\t/"})
            fixture("close-spacing/" + tag + bad, tag, tag, "A" + bad + upper + ">", "A" + bad + upper + ">", "</" + tag + ">");
        for (const std::string prefix : {"A<", "A</", "A</t", "A</text", "A<<<", "A</titleX><", "A</textareaX><"})
            fixture("overlap/" + tag + prefix, tag, tag, prefix, prefix, "</" + upper + ">");
        const std::string other = tag == "title" ? "textarea" : "title";
        fixture("other-name/" + tag, tag, tag, "A</" + other + ">", "A</" + other + ">", "</" + tag + ">");
        for (const std::string delim : {" ", "\t", "\n", "\f", "\r", "/"})
            run("opening-delimiter/" + tag + delim,
                "<body><" + upper + delim + " id=raw>A<b id=phantom>&amp;</" + tag + "><p id=after>tail</p></body>",
                {raw(tag, "A<b id=phantom>&"), after()});
        run("opening-immediate-gt/" + tag, "<body><" + upper + ">A<b id=phantom>&amp;</" + tag + "><p id=after>tail</p></body>",
            {element(tag, {}, {text("A<b id=phantom>&")}), after()});
        for (const std::string ending : {"/>", " />"})
            run("self-closing/" + tag + ending,
                "<body><" + upper + " id='raw'" + ending + "A<b id=phantom>&amp;</" + tag + "><p id=after>tail</p></body>",
                {raw(tag, "A<b id=phantom>&"), after()});
        // read_name's legacy truncation remains; these openings must NOT
        // activate RCDATA even though the resulting DOM tag is title/textarea.
        for (const std::string invalid : {"+", "=", "\"", "'", "`", "\v", "\xC2\xA0", "\xC3\xA9", "\xFF"}) {
            context = "opening-guard/" + tag + invalid;
            Document doc;
            run_on(doc, context, "<body><" + upper + invalid + " id=raw><b id=inside>X&amp;</b></" + tag + "><p id=after>tail</p></body>",
                   {element(tag, {{"id", "raw"}}, {element("b", {{"id", "inside"}}, {text("X&")})}), after()}, {"phantom"});
        }
        for (const std::string suffix : {"x", "-x", "_x", ":x", ".x"})
            run("long-name/" + tag + suffix,
                "<body><" + tag + suffix + " id=raw><b id=inside>X</b></" + tag + suffix + "><p id=after>tail</p></body>",
                {element(tag + suffix, {{"id", "raw"}}, {element("b", {{"id", "inside"}}, {text("X")})}), after()});
    }
}
void closing_suffixes() {
    for (const std::string tag : {"textarea", "title"}) {
        for (const std::string suffix : {
                 " >", "/>", "/ >", " \t\r\n\f>", " x=y>", " x='a>b'>", " x=\"a>b\">",
                 " x = 'a>b' y = \"c>d\">", " /x='a>b'>", " x='a>b'/>", " x='a>b'y=\"c>d\">",
                 " =='a>b'>", "/x='a>b'>", " x=a/b>", " x=a/b/>"})
            fixture("suffix-state/" + tag + suffix, tag, tag, "A&amp;", "A&", "</" + tag + suffix);
        // Quotes only open quoted-value state immediately after '=' (and
        // optional HTML whitespace). Else they are ordinary attribute bytes.
        for (const std::string suffix : {" x'y>", " x\"y>", " x=a'b>", " x=a\"b>", " '=a>", " \"=a>", " x=`a>", " x=\v'a>", " =\"a>"})
            fixture("suffix-literal-quote/" + tag + suffix, tag, tag, "A", "A", "</" + tag + suffix);
        fixture("suffix-vt-attribute-name/" + tag, tag, tag, "A", "A", "</" + tag + " x\v='a>b'>");
    }
}
void eof_cases() {
    for (const std::string tag : {"textarea", "title"}) {
        for (unsigned mask = 0; mask < (1u << tag.size()); ++mask) {
            const std::string close = "</" + casing(tag, mask);
            for (size_t n = 0; n <= close.size(); ++n)
                for (const std::string prefix : {"", "A&amp;<b id=phantom>"})
                    run("eof-prefix/" + tag + "/" + std::to_string(mask) + "/" + std::to_string(n) + "/" + std::to_string(prefix.size()),
                        "<body><" + tag + " id=raw>" + prefix + close.substr(0, n),
                        {raw(tag, (prefix.empty() ? "" : "A&<b id=phantom>") + close.substr(0, n))});
        }
        for (const std::string suffix : {" ", "\t", "\n", "\r", "\f", "/", " /", " x", " x=", " x='", " x=\"", " x='a>b", " x=\"a>b", " x='a>b'", " x=a'b"})
            for (const std::string prefix : {"", "A&amp;"})
                run("eof-delimited/" + tag + suffix + prefix,
                    "<body><" + tag + " id=raw>" + prefix + "</" + casing(tag, 255) + suffix,
                    {raw(tag, prefix.empty() ? "" : "A&")});
        for (const std::string opening : {"", " id=raw", " id='raw' x='a>", " id='raw' x=\"a>", " id='raw' /"}) {
            context = "opening-eof/" + tag + opening;
            Document doc;
            run_on(doc, context, "<body><" + tag + opening, {}, {"phantom", "raw", "after"});
        }
        for (const std::string suffix : {"+", "=", "\v", "\"", "'", "`", "\xC2\xA0"})
            run("eof-undelimited/" + tag + suffix,
                "<body><" + tag + " id=raw>A</" + tag + suffix,
                {raw(tag, "A</" + tag + suffix)});
    }
}
void text_processing() {
    struct Sample { const char* source; const char* title; const char* textarea; };
    const Sample samples[] = {
        {"", "", ""}, {"\n", "\n", ""}, {"\n\nA", "\n\nA", "\nA"},
        {"\r\nA\rB\r\nC", "\nA\nB\nC", "A\nB\nC"}, {"\r\r\nA", "\n\nA", "\nA"},
        {"&#10;A", "\nA", "A"}, {"&#xA;&#10;A", "\n\nA", "\nA"},
        {"&#13;\nA", "\r\nA", "\r\nA"}, {"\r&#13;A", "\n\rA", "\rA"},
        {" &#10;A", " \nA", " \nA"}, {"\t\nA", "\t\nA", "\t\nA"},
        {"&amp;amp; &amp;#10;", "&amp; &#10;", "&amp; &#10;"},
        {"&lt;b id=phantom&gt;X&lt;/b&gt;", "<b id=phantom>X</b>", "<b id=phantom>X</b>"},
        {"<!--x--><!doctype html><?x?><b id=phantom>X</b>", "<!--x--><!doctype html><?x?><b id=phantom>X</b>", "<!--x--><!doctype html><?x?><b id=phantom>X</b>"},
        // Deliberately preserve the existing semicolon-only HTML4 named table.
        {"&amp &copy &NotEqualTilde; &unknown; &copy;", "&amp &copy &NotEqualTilde; &unknown; \xC2\xA9", "&amp &copy &NotEqualTilde; &unknown; \xC2\xA9"},
        {"&#65 &#x41; &#13; &#0;", "A A \r \xEF\xBF\xBD", "A A \r \xEF\xBF\xBD"},
        {"CAF\xC3\x89 \xE6\x9D\xB1\xE4\xBA\xAC", "CAF\xC3\x89 \xE6\x9D\xB1\xE4\xBA\xAC", "CAF\xC3\x89 \xE6\x9D\xB1\xE4\xBA\xAC"},
    };
    for (const std::string tag : {"textarea", "title"}) {
        unsigned n = 0;
        for (const auto& s : samples)
            fixture("text/" + tag + "/" + std::to_string(n++), tag, tag, s.source,
                    tag == "title" ? s.title : s.textarea, "</" + tag + ">");
        fixture("encoded-end/" + tag, tag, tag, "&lt;/" + tag + "&gt;B", "</" + tag + ">B", "</" + tag + ">");
        std::string source, expected;
        for (int i = 0; i < 8192; ++i) {
            const auto bad = "</" + casing(tag, i % (1u << tag.size())) + "X><b id=phantom>";
            source += "A\r\n&amp;" + bad + "</t<";
            expected += "A\n&" + bad + "</t<";
        }
        fixture("large-adversarial/" + tag, tag, tag, source, expected, "</" + tag + ">");
    }
}
void ancestor_stack() {
    for (const std::string tag : {"textarea", "title"}) {
        const std::string payload = "A</section></body><b id=phantom>&amp;";
        run("ancestor-stack/" + tag,
            "<body><section id=wrap><" + tag + " id=raw>" + payload + "</" + casing(tag, 255) + "><p id=after>tail</p></section><div id=outside></div></body>",
            {element("section", {{"id", "wrap"}}, {raw(tag, "A</section></body><b id=phantom>&"), after()}),
             element("div", {{"id", "outside"}})});
    }
    context = "title-in-head";
    Document doc;
    ++cases;
    const int before = failures;
    if (check(doc.handle && aether_doc_parse_html(doc.handle,
            "<html id=root><head id=head><TITLE id=raw>\nA</head></html><b id=phantom>&amp;</title></head><body id=body><p id=after>tail</p></body></html>") == AETHER_OK, "parse")) {
        const auto root = aether_doc_get_document_element(doc.handle);
        if (check(root != nullptr, "document element exists")) {
            const auto owner = aether_node_get_parent(root);
            check(owner != nullptr, "document parent exists");
            if (owner) {
                check(aether_node_get_type(owner) == AETHER_NODE_DOCUMENT, "document parent type");
                check(aether_node_get_child_count(owner) == 1, "single document root");
                check(aether_node_get_child(owner, 0) == root, "document root child");
            }
            verify_node(doc.handle, root, owner,
                element("html", {{"id", "root"}}, {
                    element("head", {{"id", "head"}}, {raw("title", "\nA</head></html><b id=phantom>&")}),
                    element("body", {{"id", "body"}}, {after()})}), "html");
            check(aether_doc_get_body(doc.handle) == aether_doc_get_element_by_id(doc.handle, "body"), "body follows head");
        }
        check(!aether_doc_get_element_by_id(doc.handle, "phantom"), "absent ID phantom");
    }
    if (failures != before) ++failed_cases;
}
void lifecycle() {
    run("mixed-sequence", "<body><textarea id=a>\nA&amp;</TEXTAREA><title id=b>\nB<b id=phantom></TiTlE><textarea id=c></textarea><title id=d>D</title><p id=after>tail</p></body>",
        {raw("textarea", "A&", "a"), raw("title", "\nB<b id=phantom>", "b"), raw("textarea", "", "c"), raw("title", "D", "d"), after()});
    run("raw-and-ordinary-controls", "<body><style id=s>A&amp;\r\n</style><script id=j>B&amp;</script><div id=d><b id=inside>X&amp;</b></div><p id=after>tail</p></body>",
        {raw("style", "A&amp;\r\n", "s"), raw("script", "B&amp;", "j"), element("div", {{"id", "d"}}, {element("b", {{"id", "inside"}}, {text("X&")})}), after()});
    Document doc;
    for (int i = 0; i < 32; ++i) {
        const bool odd = i % 2;
        const std::string tag = odd ? "title" : "textarea", id = odd ? "new" : "old";
        run_on(doc, "reparse/" + std::to_string(i), "<body><" + tag + " id=" + id + ">\nA&amp;<b id=phantom></" + casing(tag, 255) + "><p id=after>tail</p></body>",
               {raw(tag, odd ? "\nA&<b id=phantom>" : "A&<b id=phantom>", id), after()}, {"phantom", odd ? "old" : "new"});
    }
    run_on(doc, "reparse/clear", "<body><p id=after>tail</p></body>", {after()}, {"phantom", "old", "new"});
}
} // namespace
int main() {
    context = "ABI";
    if (!check(AETHER_ABI_VERSION_GET_MAJOR(aether_abi_version()) == AETHER_ABI_VERSION_MAJOR, "compatible major version") ||
        !check((aether_abi_capabilities() & AETHER_CAPABILITY_DOM) != 0, "DOM capability")) return 1;
    case_pairs();
    boundaries();
    closing_suffixes();
    eof_cases();
    text_processing();
    ancestor_stack();
    lifecycle();
    // No assert(): all checks execute in Release builds with NDEBUG.
    std::printf("HTML RCDATA: %d/%d cases passed, %d failed cases, %d checks, %d failures\n",
                cases - failed_cases, cases, failed_cases, checks, failures);
    return failures ? 1 : 0;
}
