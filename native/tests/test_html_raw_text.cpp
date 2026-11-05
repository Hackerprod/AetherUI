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
        check(tag.equals(expected.tag), path + " original tag spelling");
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
void fixture(const std::string& name, const std::string& opening, const std::string& payload,
             const std::string& closing) {
    run(name, "<body><" + opening + " id=raw>" + payload + closing +
              "<p id=after>tail</p></body>", {raw(opening, payload), after()});
}
std::string casing(std::string tag, unsigned mask) {
    for (size_t i = 0; i < tag.size(); ++i)
        if (mask & (1u << i)) tag[i] = static_cast<char>(tag[i] - 'a' + 'A');
    return tag;
}
void originals() {
    // Exact HTML and expected text from the original probe-results.json,
    // generated once into this source. No runtime dependency on research files.
    struct Case { const char* name; const char* html; const char* tag; const char* payload; };
    const Case corpus[] = {
        {"style_lower_control", "<body><style id=raw>a{content:\"<b id=phantom>&amp;\";}</style><p id=after>tail</p></body>", "style", "a{content:\"<b id=phantom>&amp;\";}"},
        {"style_upper_close", "<body><style id=raw>a{content:\"<b id=phantom>&amp;\";}</STYLE><p id=after>tail</p></body>", "style", "a{content:\"<b id=phantom>&amp;\";}"},
        {"style_mixed_close", "<body><style id=raw>a{content:\"<b id=phantom>&amp;\";}</StYlE><p id=after>tail</p></body>", "style", "a{content:\"<b id=phantom>&amp;\";}"},
        {"style_upper_open", "<body><STYLE id=raw>a{content:\"<b id=phantom>&amp;\";}</style><p id=after>tail</p></body>", "STYLE", "a{content:\"<b id=phantom>&amp;\";}"},
        {"style_mixed_both", "<body><StYlE id=raw>a{content:\"<b id=phantom>&amp;\";}</StYlE><p id=after>tail</p></body>", "StYlE", "a{content:\"<b id=phantom>&amp;\";}"},
        {"style_mixed_close_space", "<body><style id=raw>a{content:\"<b id=phantom>&amp;\";}</StYlE\t><p id=after>tail</p></body>", "style", "a{content:\"<b id=phantom>&amp;\";}"},
        {"style_suffix_control", "<body><style id=raw>a{content:\"<b id=phantom>&amp;\";}</STYLEX></style><p id=after>tail</p></body>", "style", "a{content:\"<b id=phantom>&amp;\";}</STYLEX>"},
        {"script_lower_control", "<body><script id=raw>const value=\"<b id=phantom>&amp;\";</script><p id=after>tail</p></body>", "script", "const value=\"<b id=phantom>&amp;\";"},
        {"script_upper_close", "<body><script id=raw>const value=\"<b id=phantom>&amp;\";</SCRIPT><p id=after>tail</p></body>", "script", "const value=\"<b id=phantom>&amp;\";"},
        {"script_mixed_close", "<body><script id=raw>const value=\"<b id=phantom>&amp;\";</ScRiPt><p id=after>tail</p></body>", "script", "const value=\"<b id=phantom>&amp;\";"},
        {"script_upper_open", "<body><SCRIPT id=raw>const value=\"<b id=phantom>&amp;\";</script><p id=after>tail</p></body>", "SCRIPT", "const value=\"<b id=phantom>&amp;\";"},
        {"script_mixed_both", "<body><ScRiPt id=raw>const value=\"<b id=phantom>&amp;\";</ScRiPt><p id=after>tail</p></body>", "ScRiPt", "const value=\"<b id=phantom>&amp;\";"},
        {"script_mixed_close_space", "<body><script id=raw>const value=\"<b id=phantom>&amp;\";</ScRiPt\t><p id=after>tail</p></body>", "script", "const value=\"<b id=phantom>&amp;\";"},
        {"script_suffix_control", "<body><script id=raw>const value=\"<b id=phantom>&amp;\";</SCRIPTX></script><p id=after>tail</p></body>", "script", "const value=\"<b id=phantom>&amp;\";</SCRIPTX>"},
    };
    for (const auto& c : corpus) run(c.name, c.html, {raw(c.tag, c.payload), after()});
}
void exhaustive_case_pairs() {
    for (const std::string tag : {"style", "script"}) {
        const unsigned count = 1u << tag.size();
        for (unsigned a = 0; a < count; ++a)
            for (unsigned b = 0; b < count; ++b)
                // The oracle is literal text and tree structure, not equality
                // with a potentially equally broken lower-case parse.
                fixture("case/" + tag + "/" + std::to_string(a) + "/" + std::to_string(b),
                        casing(tag, a), "A<b id=phantom>&amp;B", "</" + casing(tag, b) + ">");
    }
}
void boundaries() {
    for (const std::string tag : {"style", "script"}) {
        const auto upper = casing(tag, 255);
        for (const std::string ending : {">", "/>", "/ >", " >", "\t>", "\n>", "\f>", "\r>", " \t\r\n\f>", " data-x=value>", " data-x='value'>"})
            fixture("positive/" + tag + "/" + ending, upper, "A", "</" + upper + ending);
        // All possible non-NUL bytes immediately after the name. NUL is
        // excluded because the public parse ABI is a strlen-based C string.
        for (unsigned byte = 1; byte <= 255; ++byte) {
            const char c = static_cast<char>(byte);
            const bool delimiter = c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
            const std::string candidate = "</" + upper + c + (c == '>' ? "" : ">");
            fixture("byte/" + tag + "/" + std::to_string(byte), upper,
                    delimiter ? "A" : "A" + candidate, delimiter ? candidate : "</" + upper + ">");
        }
        for (const std::string suffix : {"X", "-name", ".foo", ":foo", "_foo", "=", "\v", "\xC2\xA0", "\xE2\x80\x83"})
            fixture("suffix/" + tag + "/" + suffix, upper, "A</" + upper + suffix + ">", "</" + upper + ">");
        for (const std::string bad : {"</ ", "< /", "<\t/", "</\n"})
            fixture("spacing/" + tag + "/" + bad, upper, "A" + bad + upper + ">", "</" + upper + ">");
        fixture("other-name/" + tag, upper, "A</" + std::string(tag == "style" ? "SCRIPT" : "STYLE") + ">", "</" + upper + ">");
        // Overlap: rejecting a candidate must not skip a later '<' inside it.
        for (const std::string prefix : {"A<", "A</", "A</s", "A</sty", "A</scr", "A</styleX><", "A</scriptX><", "A<<<", "A</st<"})
            fixture("overlap/" + tag + "/" + prefix, upper, prefix, "</" + upper + ">");
    }
}
void eof_cases() {
    for (const std::string tag : {"style", "script"}) {
        for (unsigned mask = 0; mask < (1u << tag.size()); ++mask) {
            const std::string close = "</" + casing(tag, mask);
            for (size_t n = 0; n <= close.size(); ++n)
                for (const std::string payload : {"", "A&amp;<b id=phantom>"}) {
                    const auto expected = payload + close.substr(0, n);
                    run("eof/" + tag + "/" + std::to_string(mask) + "/" + std::to_string(n) + "/" + std::to_string(payload.size()),
                        "<body><" + tag + " id=raw>" + expected, {raw(tag, expected)});
                }
        }
        for (const std::string delimiter : {" ", "\t", "\n", "\r", "\f", "/", " data=x", "/ "})
            for (const std::string payload : {"", "A&amp;"})
                run("eof-delimiter/" + tag + "/" + delimiter + "/" + payload,
                    "<body><" + tag + " id=raw>" + payload + "</" + casing(tag, 255) + delimiter,
                    {raw(tag, payload)});
    }
}
void preservation() {
    const std::string payload = " \t\r\nA<b id=phantom>Hi</b><!--COMMENT-->&amp;&#60;&#x3C;&unknown;\"'"
                                "CAF\xC3\x89 \xE6\x9D\xB1\xE4\xBA\xAC\r\n ";
    for (const std::string tag : {"style", "script"}) {
        fixture("preservation/" + tag, casing(tag, 255), payload, "</" + tag + ">");
        fixture("empty/" + tag, casing(tag, 255), "", "</" + tag + ">");
        // HTML end tags inside CSS/JS strings still terminate the raw text.
        fixture("quoted-content/" + tag, tag, tag == "style" ? "a{content:\"" : "const x='", "</" + casing(tag, 255) + ">");
        std::string many;
        for (int i = 0; i < 4096; ++i)
            many += "A</" + casing(tag, i % (1u << tag.size())) + "X><b id=phantom>&amp;</s<";
        fixture("many-false-candidates/" + tag, casing(tag, 255), many, "</" + tag + ">");
    }
}
void sequences_and_controls() {
    run("mixed-sequence", "<body><STYLE id=a>A&amp;</sTyLe><script id=b>B<b id=phantom></SCRIPT><StYlE id=c></style><ScRiPt id=d>D</script><p id=after>tail</p></body>",
        {raw("STYLE", "A&amp;", "a"), raw("script", "B<b id=phantom>", "b"), raw("StYlE", "", "c"), raw("ScRiPt", "D", "d"), after()});
    run("ordinary-controls", "<body><DiV id=normal DATA-X='UP&amp;' data-path=a/b>Hi &amp; &#60; &#x3C;</dIv><IMG id=image src=/a.png><STYLE id=empty /><script id=empty2 /><p id=after>tail</p></body>",
        {element("DiV", {{"id", "normal"}, {"DATA-X", "UP&"}, {"data-path", "a/b"}}, {text("Hi & < <")}),
         element("IMG", {{"id", "image"}, {"src", "/a.png"}}), raw("STYLE", "", "empty"), raw("script", "", "empty2"), after()});
    run("adjacent-slash-attribute", "<body><div id=wrap data-path=a/b/><span id=child></span></div><p id=after>tail</p></body>",
        {element("div", {{"id", "wrap"}, {"data-path", "a/b/"}}, {element("span", {{"id", "child"}})}), after()});
    Document doc;
    for (int iteration = 0; iteration < 20; ++iteration) {
        const bool odd = iteration % 2;
        const std::string id = odd ? "new" : "old";
        const std::string tag = odd ? "ScRiPt" : "STYLE";
        const std::string payload = odd ? "B&amp;<b id=phantom>" : "A&#60;";
        run_on(doc, "reparse/" + std::to_string(iteration),
               "<body><" + tag + " id=" + id + ">" + payload + "</" + tag + "><p id=after>tail</p></body>",
               {raw(tag, payload, id), after()}, {"phantom", odd ? "old" : "new"});
        // No borrowed handles are retained across parse_html calls.
    }
    run_on(doc, "reparse/clear", "<body><p id=after>tail</p></body>", {after()}, {"phantom", "old", "new"});
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--original")) {
        std::fprintf(stderr, "Usage: %s [--original]\n", argv[0]);
        return 2;
    }
    context = "ABI";
    if (!check(AETHER_ABI_VERSION_GET_MAJOR(aether_abi_version()) == AETHER_ABI_VERSION_MAJOR,
               "compatible major version") ||
        !check((aether_abi_capabilities() & AETHER_CAPABILITY_DOM) != 0, "DOM capability")) return 1;
    originals();
    if (argc == 1) {
        exhaustive_case_pairs();
        boundaries();
        eof_cases();
        preservation();
        sequences_and_controls();
    }
    // ScriptData double-escaped states and quoted '>' in end-tag attributes
    // are known separate limitations, deliberately not conformity assertions.
    std::printf("HTML raw text: %d/%d cases passed, %d failed cases, %d checks, %d failures\n",
                cases - failed_cases, cases, failed_cases, checks, failures);
    return failures ? 1 : 0;
}
