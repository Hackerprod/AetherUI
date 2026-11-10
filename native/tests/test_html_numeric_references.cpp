#include <aether/aether.h>

// Public ABI regression. Checks execute under NDEBUG; no private headers.
#include <cstdio>
#include <cstdint>
#include <sstream>
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

bool utf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        const unsigned c = static_cast<unsigned char>(s[i++]);
        if (c < 128) { if (!c) return false; continue; }
        unsigned n = 0, lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) n = 1;
        else if (c >= 0xE0 && c <= 0xEF) {
            n = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            n = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F;
        } else return false;
        if (s.size() - i < n) return false;
        unsigned first = static_cast<unsigned char>(s[i++]);
        if (first < lo || first > hi) return false;
        while (--n) {
            unsigned next = static_cast<unsigned char>(s[i++]);
            if (next < 0x80 || next > 0xBF) return false;
        }
    }
    return true;
}
bool validate_utf8 = true;
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
Node after() { return element("p", {{"id", "after"}}, {text("tail")}); }

// Every node handle below is borrowed from the document. Only ABI strings and
// the owning document are released, never the nodes or the parent handles.
void verify_node(aether_doc_t doc, aether_node_t actual, aether_node_t parent,
                 const Node& expected, const std::string& path) {
    if (!check(actual != nullptr, path + " exists")) return;
    const bool is_text = expected.tag.empty() || expected.tag == "#comment";
    check(aether_node_get_type(actual) == (expected.tag == "#comment" ? AETHER_NODE_COMMENT : is_text ? AETHER_NODE_TEXT : AETHER_NODE_ELEMENT), path + " type");
    check(aether_node_get_parent(actual) == parent, path + " parent");
    if (is_text) {
        String value(aether_node_get_text_content(actual));
        check(value.equals(expected.text), path + " exact text bytes and suffix");
        if (value.value) {
            check(std::string(value.value).size() == expected.text.size(), path + " text length");
            if (validate_utf8) check(utf8(value.value), path + " strict UTF-8");
        }
    } else {
        String tag(aether_element_get_tag_name(actual));
        check(tag.equals(expected.tag), path + " original tag spelling");
        const int32_t count = aether_element_get_attribute_count(actual);
        check(count == static_cast<int32_t>(expected.attrs.size()), path + " attribute count");
        check(count >= 0 && count <= 1024, path + " bounded enumeration");
        String missing(aether_element_get_attribute(actual, "data-absent"));
        check(missing.value == nullptr && !aether_element_has_attribute(actual, "data-absent"),
              path + " absent is null, distinct from empty");
        Attributes attrs;
        for (int32_t i = 0; i < count && i < 1024; ++i) {
            String key(aether_element_get_attribute_name(actual, i));
            String val(aether_element_get_attribute_value(actual, i));
            if (check(key.value && val.value, path + " attribute strings"))
                check(attrs.emplace(key.value, val.value).second, path + " unique attribute");
        }
        check(attrs == expected.attrs, path + " complete attributes");
        for (const auto& pair : expected.attrs) {
            String value(aether_element_get_attribute(actual, pair.first.c_str()));
            check(value.equals(pair.second), path + " lookup attribute " + pair.first);
            if (value.value) {
                check(std::string(value.value).size() == pair.second.size(), path + " attribute length");
                if (validate_utf8) check(utf8(value.value), path + " strict attribute UTF-8");
            }
            check(aether_element_has_attribute(actual, pair.first.c_str()) != 0, path + " attribute presence");
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

const std::string replacement = "\xEF\xBF\xBD";
void matrix(const std::string& name, const std::string& input, const std::string& expected,
            const std::string* data_expected = nullptr) {
    const std::string src = "P" + input + "Z", out = "P" + expected + "Z";
    run(name + "/text", "<body><div>" + src + "</div><p id=after>tail</p></body>",
        {element("div", {}, {text(data_expected ? "P" + *data_expected + "Z" : out)}), after()});
    if (input.find('\'') == std::string::npos)
        run(name + "/single", "<body><div data-v='" + src + "'></div><p id=after>tail</p></body>",
            {element("div", {{"data-v", out}}), after()});
    if (input.find('"') == std::string::npos)
        run(name + "/double", "<body><div data-v=\"" + src + "\"></div><p id=after>tail</p></body>",
            {element("div", {{"data-v", out}}), after()});
    if (input.find_first_of(" \t\n\r\f\v>") == std::string::npos)
        run(name + "/unquoted", "<body><div data-v=" + src + "></div><p id=after>tail</p></body>",
            {element("div", {{"data-v", out}}), after()});
}
struct Scalar { uint32_t input; const char* bytes; };
// Independent expected UTF-8 bytes generated from Python chr().encode("utf-8"),
// with CP1252 decoding for C1 (the five undefined positions preserved).
// No product encoder or numeric parser participates in the oracle.
const Scalar scalars[] = {
    {0x0u, "\xEF\xBF\xBD"},
    {0x1u, "\x01"},
    {0x9u, "\x09"},
    {0xAu, "\x0A"},
    {0xBu, "\x0B"},
    {0xCu, "\x0C"},
    {0xDu, "\x0D"},
    {0x1Fu, "\x1F"},
    {0x20u, "\x20"},
    {0x22u, "\x22"},
    {0x26u, "\x26"},
    {0x27u, "\x27"},
    {0x2Fu, "\x2F"},
    {0x3Cu, "\x3C"},
    {0x3Du, "\x3D"},
    {0x3Eu, "\x3E"},
    {0x41u, "\x41"},
    {0x7Fu, "\x7F"},
    {0x80u, "\xE2\x82\xAC"},
    {0x81u, "\xC2\x81"},
    {0x82u, "\xE2\x80\x9A"},
    {0x83u, "\xC6\x92"},
    {0x84u, "\xE2\x80\x9E"},
    {0x85u, "\xE2\x80\xA6"},
    {0x86u, "\xE2\x80\xA0"},
    {0x87u, "\xE2\x80\xA1"},
    {0x88u, "\xCB\x86"},
    {0x89u, "\xE2\x80\xB0"},
    {0x8Au, "\xC5\xA0"},
    {0x8Bu, "\xE2\x80\xB9"},
    {0x8Cu, "\xC5\x92"},
    {0x8Du, "\xC2\x8D"},
    {0x8Eu, "\xC5\xBD"},
    {0x8Fu, "\xC2\x8F"},
    {0x90u, "\xC2\x90"},
    {0x91u, "\xE2\x80\x98"},
    {0x92u, "\xE2\x80\x99"},
    {0x93u, "\xE2\x80\x9C"},
    {0x94u, "\xE2\x80\x9D"},
    {0x95u, "\xE2\x80\xA2"},
    {0x96u, "\xE2\x80\x93"},
    {0x97u, "\xE2\x80\x94"},
    {0x98u, "\xCB\x9C"},
    {0x99u, "\xE2\x84\xA2"},
    {0x9Au, "\xC5\xA1"},
    {0x9Bu, "\xE2\x80\xBA"},
    {0x9Cu, "\xC5\x93"},
    {0x9Du, "\xC2\x9D"},
    {0x9Eu, "\xC5\xBE"},
    {0x9Fu, "\xC5\xB8"},
    {0xA0u, "\xC2\xA0"},
    {0x7FFu, "\xDF\xBF"},
    {0x800u, "\xE0\xA0\x80"},
    {0xD7FFu, "\xED\x9F\xBF"},
    {0xD800u, "\xEF\xBF\xBD"},
    {0xDBFFu, "\xEF\xBF\xBD"},
    {0xDC00u, "\xEF\xBF\xBD"},
    {0xDFFFu, "\xEF\xBF\xBD"},
    {0xE000u, "\xEE\x80\x80"},
    {0xFDD0u, "\xEF\xB7\x90"},
    {0xFDEFu, "\xEF\xB7\xAF"},
    {0xFFFEu, "\xEF\xBF\xBE"},
    {0xFFFFu, "\xEF\xBF\xBF"},
    {0x10000u, "\xF0\x90\x80\x80"},
    {0x1FFFEu, "\xF0\x9F\xBF\xBE"},
    {0x1FFFFu, "\xF0\x9F\xBF\xBF"},
    {0x1F600u, "\xF0\x9F\x98\x80"},
    {0x10FFFFu, "\xF4\x8F\xBF\xBF"},
    {0x110000u, "\xEF\xBF\xBD"},
    {0x7FFFFFFFu, "\xEF\xBF\xBD"},
    {0x80000000u, "\xEF\xBF\xBD"},
    {0xFFFFFFFFu, "\xEF\xBF\xBD"},
    {0xC5AC8u, "\xF3\x85\xAB\x88"},
    {0x33F87u, "\xF0\xB3\xBE\x87"},
    {0xFF73Au, "\xF3\xBF\x9C\xBA"},
    {0x59D51u, "\xF1\x99\xB5\x91"},
    {0x5367Cu, "\xF1\x93\x99\xBC"},
    {0x41FABu, "\xF1\x81\xBE\xAB"},
    {0x5B10Eu, "\xF1\x9B\x84\x8E"},
    {0x88515u, "\xF2\x88\x94\x95"},
    {0x51370u, "\xF1\x91\x8D\xB0"},
    {0x1900Fu, "\xF0\x99\x80\x8F"},
    {0xA3E22u, "\xF2\xA3\xB8\xA2"},
    {0xFA719u, "\xF3\xBA\x9C\x99"},
    {0x365A4u, "\xF0\xB6\x96\xA4"},
    {0xC74B3u, "\xF3\x87\x92\xB3"},
    {0x53276u, "\xF1\x93\x89\xB6"},
    {0x1875Du, "\xF0\x98\x9D\x9D"},
    {0x10E118u, "\xF4\x8E\x84\x98"},
    {0x1F197u, "\xF0\x9F\x86\x97"},
    {0x7620Au, "\xF1\xB6\x88\x8A"},
    {0x8E9E1u, "\xF2\x8E\xA7\xA1"},
    {0x579CCu, "\xF1\x97\xA7\x8C"},
    {0xB6ABBu, "\xF2\xB6\xAA\xBB"},
    {0x10E0DEu, "\xF4\x8E\x83\x9E"},
    {0x8D2A5u, "\xF2\x8D\x8A\xA5"},
    {0x463C0u, "\xF1\x86\x8F\x80"},
    {0x6841Fu, "\xF1\xA8\x90\x9F"},
    {0x1102F2u, "\xEF\xBF\xBD"},
    {0xC85A9u, "\xF3\x88\x96\xA9"},
    {0xC12F4u, "\xF3\x81\x8B\xB4"},
    {0xB21C3u, "\xF2\xB2\x87\x83"},
    {0x25C46u, "\xF0\xA5\xB1\x86"},
    {0x986EDu, "\xF2\x98\x9B\xAD"},
    {0x83B68u, "\xF2\x83\xAD\xA8"},
    {0xF67A7u, "\xF3\xB6\x9E\xA7"},
    {0x6C0DAu, "\xF1\xAC\x83\x9A"},
    {0xF9A71u, "\xF3\xB9\xA9\xB1"},
    {0xDD11Cu, "\xF3\x9D\x84\x9C"},
    {0x9B9CBu, "\xF2\x9B\xA7\x8B"},
    {0x1144AEu, "\xEF\xBF\xBD"},
    {0x2C435u, "\xF0\xAC\x90\xB5"},
    {0xF0810u, "\xF3\xB0\xA0\x90"},
    {0xCBC2Fu, "\xF3\x8B\xB0\xAF"},
    {0x3BC2u, "\xE3\xAF\x82"},
    {0x54839u, "\xF1\x94\xA0\xB9"},
    {0x55444u, "\xF1\x95\x91\x84"},
    {0xF52D3u, "\xF3\xB5\x8B\x93"},
    {0x93A16u, "\xF2\x93\xA8\x96"},
    {0x5AA7Du, "\xF1\x9A\xA9\xBD"},
    {0x569B8u, "\xF1\x96\xA6\xB8"},
    {0x4A1B7u, "\xF1\x8A\x86\xB7"},
    {0x313AAu, "\xF0\xB1\x8E\xAA"},
    {0xEAF01u, "\xF3\xAA\xBC\x81"},
    {0xF3C6Cu, "\xF3\xB3\xB1\xAC"},
    {0x100CDBu, "\xF4\x80\xB3\x9B"},
    {0xBDC7Eu, "\xF2\xBD\xB1\xBE"},
    {0xF59C5u, "\xF3\xB5\xA7\x85"},
    {0xA0060u, "\xF2\xA0\x81\xA0"},
    {0x7383Fu, "\xF1\xB3\xA0\xBF"},
    {0x10E892u, "\xF4\x8E\xA2\x92"},
    {0xEEEC9u, "\xF3\xAE\xBB\x89"},
    {0xE2994u, "\xF3\xA2\xA6\x94"},
    {0x607E3u, "\xF1\xA0\x9F\xA3"},
    {0x10CBE6u, "\xF4\x8C\xAF\xA6"},
    {0xCF20Du, "\xF3\x8F\x88\x8D"},
    {0x96C08u, "\xF2\x96\xB0\x88"},
    {0x9FC7u, "\xE9\xBF\x87"},
    {0xF5A7Au, "\xF3\xB5\xA9\xBA"},
    {0xB2791u, "\xF2\xB2\x9E\x91"},
    {0x6BBBCu, "\xF1\xAB\xAE\xBC"},
    {0x1163EBu, "\xEF\xBF\xBD"},
    {0x5A84Eu, "\xF1\x9A\xA1\x8E"},
    {0x39355u, "\xF0\xB9\x8D\x95"},
    {0xE4CB0u, "\xF3\xA4\xB2\xB0"},
    {0x2F84Fu, "\xF0\xAF\xA1\x8F"},
    {0xC0962u, "\xF3\x80\xA5\xA2"},
    {0x27959u, "\xF0\xA7\xA5\x99"},
    {0x592E4u, "\xF1\x99\x8B\xA4"},
    {0x40F3u, "\xE4\x83\xB3"},
    {0xC11B6u, "\xF3\x81\x86\xB6"},
    {0x105D9Du, "\xF4\x85\xB6\x9D"},
    {0xD4258u, "\xF3\x94\x89\x98"},
    {0xC61D7u, "\xF3\x86\x87\x97"},
    {0x8954Au, "\xF2\x89\x95\x8A"},
    {0x60421u, "\xF1\xA0\x90\xA1"},
    {0xD4F0Cu, "\xF3\x94\xBC\x8C"},
    {0x6BEFBu, "\xF1\xAB\xBB\xBB"},
    {0x5A81Eu, "\xF1\x9A\xA0\x9E"},
    {0x270E5u, "\xF0\xA7\x83\xA5"},
    {0x2ED00u, "\xF0\xAE\xB4\x80"},
    {0xCFC5Fu, "\xF3\x8F\xB1\x9F"},
    {0x69E32u, "\xF1\xA9\xB8\xB2"},
    {0x6E7E9u, "\xF1\xAE\x9F\xA9"},
    {0x109034u, "\xF4\x89\x80\xB4"},
    {0xEFE03u, "\xF3\xAF\xB8\x83"},
    {0x80B86u, "\xF2\x80\xAE\x86"},
    {0xAED2Du, "\xF2\xAE\xB4\xAD"},
    {0xFECA8u, "\xF3\xBE\xB2\xA8"},
    {0x10E7E7u, "\xF4\x8E\x9F\xA7"},
    {0x1C41Au, "\xF0\x9C\x90\x9A"},
    {0xE44B1u, "\xF3\xA4\x92\xB1"},
    {0x11F65Cu, "\xEF\xBF\xBD"},
    {0x111E0Bu, "\xEF\xBF\xBD"},
    {0x4DBEEu, "\xF1\x8D\xAF\xAE"},
    {0xCF275u, "\xF3\x8F\x89\xB5"},
    {0xEE150u, "\xF3\xAE\x85\x90"},
    {0xE446Fu, "\xF3\xA4\x91\xAF"},
    {0x3A702u, "\xF0\xBA\x9C\x82"},
    {0xF3A79u, "\xF3\xB3\xA9\xB9"},
    {0xC2184u, "\xF3\x82\x86\x84"},
    {0x13F13u, "\xF0\x93\xBC\x93"},
    {0x7B956u, "\xF1\xBB\xA5\x96"},
    {0x7A0BDu, "\xF1\xBA\x82\xBD"},
    {0xE6AF8u, "\xF3\xA6\xAB\xB8"},
    {0xF31F7u, "\xF3\xB3\x87\xB7"},
    {0x7E6EAu, "\xF1\xBE\x9B\xAA"},
    {0x4E941u, "\xF1\x8E\xA5\x81"},
    {0x3B1ACu, "\xF0\xBB\x86\xAC"},
    {0x1811Bu, "\xF0\x98\x84\x9B"},
    {0xE43BEu, "\xF3\xA4\x8E\xBE"},
    {0x21805u, "\xF0\xA1\xA0\x85"},
    {0xB29A0u, "\xF2\xB2\xA6\xA0"},
    {0x9D07Fu, "\xF2\x9D\x81\xBF"},
    {0xA23D2u, "\xF2\xA2\x8F\x92"},
    {0x7109u, "\xE7\x84\x89"},
    {0xF46D4u, "\xF3\xB4\x9B\x94"},
    {0x40423u, "\xF1\x80\x90\xA3"},
    {0xE1B26u, "\xF3\xA1\xAC\xA6"},
    {0xD784Du, "\xF3\x97\xA1\x8D"},
    {0xBBD48u, "\xF2\xBB\xB5\x88"},
    {0x24007u, "\xF0\xA4\x80\x87"},
    {0x3FDBAu, "\xF0\xBF\xB6\xBA"},
    {0xEF1D1u, "\xF3\xAF\x87\x91"},
    {0xB80FCu, "\xF2\xB8\x83\xBC"},
    {0xCE82Bu, "\xF3\x8E\xA0\xAB"},
    {0x6DF8Eu, "\xF1\xAD\xBE\x8E"},
    {0x2E195u, "\xF0\xAE\x86\x95"},
    {0xCC5F0u, "\xF3\x8C\x97\xB0"},
    {0x6A08Fu, "\xF1\xAA\x82\x8F"},
    {0x714A2u, "\xF1\xB1\x92\xA2"},
    {0xB8B99u, "\xF2\xB8\xAE\x99"},
    {0x110024u, "\xEF\xBF\xBD"},
    {0x4D33u, "\xE4\xB4\xB3"},
    {0xE30F6u, "\xF3\xA3\x83\xB6"},
    {0x1173DDu, "\xEF\xBF\xBD"},
    {0x4E398u, "\xF1\x8E\x8E\x98"},
    {0x111217u, "\xEF\xBF\xBD"},
    {0x3088Au, "\xF0\xB0\xA2\x8A"},
    {0x55E61u, "\xF1\x95\xB9\xA1"},
    {0x6644Cu, "\xF1\xA6\x91\x8C"},
    {0x8533Bu, "\xF2\x85\x8C\xBB"},
    {0x3AF5Eu, "\xF0\xBA\xBD\x9E"},
    {0x24F25u, "\xF0\xA4\xBC\xA5"},
    {0xCB640u, "\xF3\x8B\x99\x80"},
    {0xFB49Fu, "\xF3\xBB\x92\x9F"},
    {0x77972u, "\xF1\xB7\xA5\xB2"},
    {0xB8A29u, "\xF2\xB8\xA8\xA9"},
    {0x104D74u, "\xF4\x84\xB5\xB4"},
    {0xD1A43u, "\xF3\x91\xA9\x83"},
    {0x6FAC6u, "\xF1\xAF\xAB\x86"},
    {0x4936Du, "\xF1\x89\x8D\xAD"},
    {0x2DDE8u, "\xF0\xAD\xB7\xA8"},
    {0x2A827u, "\xF0\xAA\xA0\xA7"},
    {0xA075Au, "\xF2\xA0\x9D\x9A"},
    {0x92EF1u, "\xF2\x92\xBB\xB1"},
    {0x15B9Cu, "\xF0\x95\xAE\x9C"},
    {0x8C24Bu, "\xF2\x8C\x89\x8B"},
    {0xDB32Eu, "\xF3\x9B\x8C\xAE"},
    {0xB60B5u, "\xF2\xB6\x82\xB5"},
    {0xDFA90u, "\xF3\x9F\xAA\x90"},
    {0x80CAFu, "\xF2\x80\xB2\xAF"},
    {0xA5242u, "\xF2\xA5\x89\x82"},
    {0x96CB9u, "\xF2\x96\xB2\xB9"},
    {0x62EC4u, "\xF1\xA2\xBB\x84"},
    {0xD6B53u, "\xF3\x96\xAD\x93"},
    {0x77896u, "\xF1\xB7\xA2\x96"},
    {0x3D6FDu, "\xF0\xBD\x9B\xBD"},
    {0xAC38u, "\xEA\xB0\xB8"},
    {0x80237u, "\xF2\x80\x88\xB7"},
    {0x7FA2Au, "\xF1\xBF\xA8\xAA"},
    {0x16381u, "\xF0\x96\x8E\x81"},
    {0x566ECu, "\xF1\x96\x9B\xAC"},
    {0x9355Bu, "\xF2\x93\x95\x9B"},
    {0xBEAFEu, "\xF2\xBE\xAB\xBE"},
    {0x111645u, "\xEF\xBF\xBD"},
    {0x392E0u, "\xF0\xB9\x8B\xA0"},
    {0x6A8BFu, "\xF1\xAA\xA2\xBF"},
    {0x9F12u, "\xE9\xBC\x92"},
    {0x43349u, "\xF1\x83\x8D\x89"},
    {0x9A414u, "\xF2\x9A\x90\x94"},
    {0x64063u, "\xF1\xA4\x81\xA3"},
    {0xCAA66u, "\xF3\x8A\xA9\xA6"},
    {0x3E8Du, "\xE3\xBA\x8D"},
    {0x114E88u, "\xEF\xBF\xBD"},
    {0x2047u, "\xE2\x81\x87"},
    {0xFE0FAu, "\xF3\xBE\x83\xBA"},
    {0xCFC11u, "\xF3\x8F\xB0\x91"},
    {0x5863Cu, "\xF1\x98\x98\xBC"},
    {0x6AC6Bu, "\xF1\xAA\xB1\xAB"},
    {0x1156CEu, "\xEF\xBF\xBD"},
    {0x106FD5u, "\xF4\x86\xBF\x95"},
    {0x7F30u, "\xE7\xBC\xB0"},
    {0x88CFu, "\xE8\xA3\x8F"},
    {0x115FE2u, "\xEF\xBF\xBD"},
    {0x2DDD9u, "\xF0\xAD\xB7\x99"},
    {0xBAD64u, "\xF2\xBA\xB5\xA4"},
    {0x9973u, "\xE9\xA5\xB3"},
    {0x99036u, "\xF2\x99\x80\xB6"},
    {0x6CA1Du, "\xF1\xAC\xA8\x9D"},
    {0x3C4D8u, "\xF0\xBC\x93\x98"},
    {0x257u, "\xC9\x97"},
    {0xEBBCAu, "\xF3\xAB\xAF\x8A"},
    {0xF8A1u, "\xEF\xA2\xA1"},
    {0xB98Cu, "\xEB\xA6\x8C"},
    {0x8277Bu, "\xF2\x82\x9D\xBB"},
    {0x6F69Eu, "\xF1\xAF\x9A\x9E"},
    {0x26D65u, "\xF0\xA6\xB5\xA5"},
    {0x7BF80u, "\xF1\xBB\xBE\x80"},
    {0x2ACDFu, "\xF0\xAA\xB3\x9F"},
    {0xD94B2u, "\xF3\x99\x92\xB2"},
    {0x46C69u, "\xF1\x86\xB1\xA9"},
    {0x114AB4u, "\xEF\xBF\xBD"},
    {0x17683u, "\xF0\x97\x9A\x83"},
    {0x52A06u, "\xF1\x92\xA8\x86"},
    {0x1079ADu, "\xF4\x87\xA6\xAD"},
    {0xF0F28u, "\xF3\xB0\xBC\xA8"},
    {0x6A867u, "\xF1\xAA\xA1\xA7"},
    {0x38A9Au, "\xF0\xB8\xAA\x9A"},
    {0x5931u, "\xE5\xA4\xB1"},
    {0xDCu, "\xC3\x9C"},
    {0x2A68Bu, "\xF0\xAA\x9A\x8B"},
    {0x5CA6Eu, "\xF1\x9C\xA9\xAE"},
    {0x100EF5u, "\xF4\x80\xBB\xB5"},
    {0x253D0u, "\xF0\xA5\x8F\x90"},
    {0x214EFu, "\xF0\xA1\x93\xAF"},
    {0xC3D82u, "\xF3\x83\xB6\x82"},
    {0x3DEF9u, "\xF0\xBD\xBB\xB9"},
    {0xD7C04u, "\xF3\x97\xB0\x84"},
    {0x7D793u, "\xF1\xBD\x9E\x93"},
    {0x277D6u, "\xF0\xA7\x9F\x96"},
    {0x4D3Du, "\xE4\xB4\xBD"},
    {0x42D78u, "\xF1\x82\xB5\xB8"},
    {0x51277u, "\xF1\x91\x89\xB7"},
    {0xD4D6Au, "\xF3\x94\xB5\xAA"},
    {0x61DC1u, "\xF1\xA1\xB7\x81"},
    {0xC5C2Cu, "\xF3\x85\xB0\xAC"},
    {0xF299Bu, "\xF3\xB2\xA6\x9B"},
    {0xAD23Eu, "\xF2\xAD\x88\xBE"},
    {0x45485u, "\xF1\x85\x92\x85"},
    {0xD3C20u, "\xF3\x93\xB0\xA0"},
    {0x11C0FFu, "\xEF\xBF\xBD"},
    {0x45A52u, "\xF1\x85\xA9\x92"},
    {0x63589u, "\xF1\xA3\x96\x89"},
    {0xF4154u, "\xF3\xB4\x85\x94"},
    {0xEBCA3u, "\xF3\xAB\xB2\xA3"},
    {0xC79A6u, "\xF3\x87\xA6\xA6"},
    {0xF44CDu, "\xF3\xB4\x93\x8D"},

};
std::string number(uint32_t value, int form) {
    if (!form) return std::to_string(value);
    std::ostringstream out;
    if (form == 2) out << std::uppercase;
    out << std::hex << value;
    return out.str();
}
void unicode_matrix() {
    for (const auto& row : scalars)
        for (int form = 0; form < 3; ++form)
            for (int semi = 0; semi < 2; ++semi) {
                const std::string prefix = form == 0 ? "&#" : form == 1 ? "&#x" : "&#X";
                const std::string input = prefix + "000" + number(row.input, form) + (semi ? ";" : "");
                matrix("scalar/" + input, input, row.bytes);
            }
}
void lexical() {
    const std::pair<std::string, std::string> rows[] = {
        {"&#65tail;", "Atail;"}, {"&#x41Z;", "AZ;"}, {"&#x41B;", "\xD0\x9B"},
        {"&#065;", "A"}, {"&#65;;", "A;"}, {"&#65=foo;", "A=foo;"},
        {"&#65+1;", "A+1;"}, {"&#65-1;", "A-1;"}, {"&#65.0;", "A.0;"},
        {"&#65:tail;", "A:tail;"}, {"&#0x41;", replacement + "x41;"},
        {"&#x0x41;", replacement + "x41;"}, {"&#65&copy;", "A\xC2\xA9"},
        {"&#65&#66;", "AB"}, {"&#0tail;", replacement + "tail;"},
        {"&#x&copy;", "&#x\xC2\xA9"}, {"&#;&#65;", "&#;A"},
        {"&#xD83D;&#xDE00;", replacement + replacement},
        {"&#38;#65;", "&#65;"}, {"&amp;#65;", "&#65;"}, {"&#38;copy;", "&copy;"},
        {"&#60;b&#62;", "<b>"}, {"&#60;/div&#62;", "</div>"}, {"&lt;b&gt;", "<b>"},
        {"&copy;&#65", "\xC2\xA9" "A"}, {"&amp;&#0;", "&" + replacement},
        {"&unknown;&#65;", "&unknown;A"},
        {"&#\xD9\xA1;", "&#\xD9\xA1;"}, {"&#\xEF\xBC\x91;", "&#\xEF\xBC\x91;"},
        {"&#65\xD9\xA1;", "A\xD9\xA1;"}, {"&#x41\xEF\xBC\x91;", "A\xEF\xBC\x91;"}
    };
    for (const auto& row : rows) matrix("lexical/" + row.first, row.first, row.second);
    for (const std::string literal : {"&", "&#", "&#x", "&#X", "&#;", "&#x;", "&#X;",
              "&#xG;", "&#+65;", "&#-1;", "&#x+41;", "&#x-41;"})
        matrix("invalid/" + literal, literal, literal);
    for (char ws : std::string(" \t\r\n\f\v")) {
        for (const std::string prefix : {"&#", "&#x", "&#X"}) {
            const auto input = prefix + ws + "65;";
            // Source CR changes in both text and attributes; reference-created CR does not.
            const auto data = prefix + (ws == '\r' ? '\n' : ws) + "65;";
            matrix("whitespace/" + std::to_string(int(ws)), input, data);
        }
        const auto data = "A" + std::string(1, ws == '\r' ? '\n' : ws) + "tail;";
        matrix("termination-space/" + std::to_string(int(ws)), "&#65" + std::string(1, ws) + "tail;",
               data);
    }
}
void scale() {
    for (const std::string digits : {"4294967296", "4294967361", "18446744073709551615",
             "18446744073709551616", "999999999999999999999999999999999999"})
        matrix("overflow/" + digits, "&#" + digits + ";", replacement);
    for (const std::string digits : {"100000000", "100000041", "FFFFFFFFFFFFFFFF",
             "10000000000000000", "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"})
        matrix("overflow-hex/" + digits, "&#x" + digits + ";", replacement);
    for (size_t count : {size_t(1024), size_t(100000)}) {
        for (const std::string prefix : {"&#", "&#x", "&#X"}) {
            const bool hex = prefix.size() == 3;
            for (const std::string end : {";", "", "tail;&#65;"}) {
                const auto expected = replacement + (end == "tail;&#65;" ? "tail;A" : "");
                matrix("long-overflow/" + prefix + "/" + std::to_string(count) + "/" + end,
                       prefix + std::string(count, hex ? 'F' : '9') + end, expected);
            }
            matrix("long-zero/" + prefix, prefix + std::string(count, '0') + ";", replacement);
            matrix("long-leading-zero/" + prefix, prefix + std::string(count, '0') + (hex ? "41" : "65"), "A");
        }
    }
    std::string input, out;
    for (int i = 0; i < 4096; ++i) { input += "&#65;&#xG&&#0;"; out += "A&#xG&" + replacement; }
    matrix("many-references", input, out);
}
void boundaries_and_structure() {
    for (const std::string input : {"&#65", "&#x41", "&#X41", "&#", "&#x", "&#X", "&"}) {
        const std::string out = "P" + (input == "&#65" || input == "&#x41" || input == "&#X41" ? "A" : input);
        run("EOF/" + input, "<body><div>P" + input, {element("div", {}, {text(out)})});
        run("text-boundary/" + input, "<body><div>P" + input + "</div><p id=after>tail</p></body>",
            {element("div", {}, {text(out)}), after()});
        for (const std::string quote : {"", "'", "\""}) {
            run("attribute-boundary/" + quote + input,
                "<body><div data-v=" + quote + "P" + input + quote + "></div><p id=after>tail</p></body>",
                {element("div", {{"data-v", out}}), after()});
            run("attribute-space/" + quote + input,
                "<body><div data-v=" + quote + "P" + input + quote + " title=kept></div><p id=after>tail</p></body>",
                {element("div", {{"data-v", out}, {"title", "kept"}}), after()});
        }
    }
    const std::pair<std::string, std::string> values[] = {
        {"\"&#34; x=bad\"", "\" x=bad"}, {"'&#39; x=bad'", "' x=bad"},
        {"&#32;Z", " Z"}, {"&#62;Z", ">Z"}
    };
    for (const auto& row : values)
        run("decoded-syntax/" + row.first,
            "<body><div data-v=" + row.first + " title=kept></div><p id=after>tail</p></body>",
            {element("div", {{"data-v", row.second}, {"title", "kept"}}), after()});
    run("slash-is-data", "<body><div data-v=&#65/><span></span></div><p id=after>tail</p></body>",
        {element("div", {{"data-v", "A/"}}, {element("span", {})}), after()});
    run("literal-space-delimits", "<body><div data-v=&#65 title=kept></div><p id=after>tail</p></body>",
        {element("div", {{"data-v", "A"}, {"title", "kept"}}), after()});
    // The existing name tokenizer stops at '&': this is a baseline control,
    // not a claim that arbitrary HTML name characters are supported.
    run("tag-name-not-decoded", "<body><d&#65;></d><p id=after>tail</p></body>",
        {element("d", {{"65", ""}}), after()});
}
void protected_contexts() {
    // HTML4 name2codepoint golden corpus, excluding unsupported sube.
    const std::pair<std::string, std::string> named[] = {
        {"&AElig;", "\xC3\x86"},
        {"&Aacute;", "\xC3\x81"},
        {"&Acirc;", "\xC3\x82"},
        {"&Agrave;", "\xC3\x80"},
        {"&Alpha;", "\xCE\x91"},
        {"&Aring;", "\xC3\x85"},
        {"&Atilde;", "\xC3\x83"},
        {"&Auml;", "\xC3\x84"},
        {"&Beta;", "\xCE\x92"},
        {"&Ccedil;", "\xC3\x87"},
        {"&Chi;", "\xCE\xA7"},
        {"&Dagger;", "\xE2\x80\xA1"},
        {"&Delta;", "\xCE\x94"},
        {"&ETH;", "\xC3\x90"},
        {"&Eacute;", "\xC3\x89"},
        {"&Ecirc;", "\xC3\x8A"},
        {"&Egrave;", "\xC3\x88"},
        {"&Epsilon;", "\xCE\x95"},
        {"&Eta;", "\xCE\x97"},
        {"&Euml;", "\xC3\x8B"},
        {"&Gamma;", "\xCE\x93"},
        {"&Iacute;", "\xC3\x8D"},
        {"&Icirc;", "\xC3\x8E"},
        {"&Igrave;", "\xC3\x8C"},
        {"&Iota;", "\xCE\x99"},
        {"&Iuml;", "\xC3\x8F"},
        {"&Kappa;", "\xCE\x9A"},
        {"&Lambda;", "\xCE\x9B"},
        {"&Mu;", "\xCE\x9C"},
        {"&Ntilde;", "\xC3\x91"},
        {"&Nu;", "\xCE\x9D"},
        {"&OElig;", "\xC5\x92"},
        {"&Oacute;", "\xC3\x93"},
        {"&Ocirc;", "\xC3\x94"},
        {"&Ograve;", "\xC3\x92"},
        {"&Omega;", "\xCE\xA9"},
        {"&Omicron;", "\xCE\x9F"},
        {"&Oslash;", "\xC3\x98"},
        {"&Otilde;", "\xC3\x95"},
        {"&Ouml;", "\xC3\x96"},
        {"&Phi;", "\xCE\xA6"},
        {"&Pi;", "\xCE\xA0"},
        {"&Prime;", "\xE2\x80\xB3"},
        {"&Psi;", "\xCE\xA8"},
        {"&Rho;", "\xCE\xA1"},
        {"&Scaron;", "\xC5\xA0"},
        {"&Sigma;", "\xCE\xA3"},
        {"&THORN;", "\xC3\x9E"},
        {"&Tau;", "\xCE\xA4"},
        {"&Theta;", "\xCE\x98"},
        {"&Uacute;", "\xC3\x9A"},
        {"&Ucirc;", "\xC3\x9B"},
        {"&Ugrave;", "\xC3\x99"},
        {"&Upsilon;", "\xCE\xA5"},
        {"&Uuml;", "\xC3\x9C"},
        {"&Xi;", "\xCE\x9E"},
        {"&Yacute;", "\xC3\x9D"},
        {"&Yuml;", "\xC5\xB8"},
        {"&Zeta;", "\xCE\x96"},
        {"&aacute;", "\xC3\xA1"},
        {"&acirc;", "\xC3\xA2"},
        {"&acute;", "\xC2\xB4"},
        {"&aelig;", "\xC3\xA6"},
        {"&agrave;", "\xC3\xA0"},
        {"&alefsym;", "\xE2\x84\xB5"},
        {"&alpha;", "\xCE\xB1"},
        {"&amp;", "\x26"},
        {"&and;", "\xE2\x88\xA7"},
        {"&ang;", "\xE2\x88\xA0"},
        {"&aring;", "\xC3\xA5"},
        {"&asymp;", "\xE2\x89\x88"},
        {"&atilde;", "\xC3\xA3"},
        {"&auml;", "\xC3\xA4"},
        {"&bdquo;", "\xE2\x80\x9E"},
        {"&beta;", "\xCE\xB2"},
        {"&brvbar;", "\xC2\xA6"},
        {"&bull;", "\xE2\x80\xA2"},
        {"&cap;", "\xE2\x88\xA9"},
        {"&ccedil;", "\xC3\xA7"},
        {"&cedil;", "\xC2\xB8"},
        {"&cent;", "\xC2\xA2"},
        {"&chi;", "\xCF\x87"},
        {"&circ;", "\xCB\x86"},
        {"&clubs;", "\xE2\x99\xA3"},
        {"&cong;", "\xE2\x89\x85"},
        {"&copy;", "\xC2\xA9"},
        {"&crarr;", "\xE2\x86\xB5"},
        {"&cup;", "\xE2\x88\xAA"},
        {"&curren;", "\xC2\xA4"},
        {"&dArr;", "\xE2\x87\x93"},
        {"&dagger;", "\xE2\x80\xA0"},
        {"&darr;", "\xE2\x86\x93"},
        {"&deg;", "\xC2\xB0"},
        {"&delta;", "\xCE\xB4"},
        {"&diams;", "\xE2\x99\xA6"},
        {"&divide;", "\xC3\xB7"},
        {"&eacute;", "\xC3\xA9"},
        {"&ecirc;", "\xC3\xAA"},
        {"&egrave;", "\xC3\xA8"},
        {"&empty;", "\xE2\x88\x85"},
        {"&emsp;", "\xE2\x80\x83"},
        {"&ensp;", "\xE2\x80\x82"},
        {"&epsilon;", "\xCE\xB5"},
        {"&equiv;", "\xE2\x89\xA1"},
        {"&eta;", "\xCE\xB7"},
        {"&eth;", "\xC3\xB0"},
        {"&euml;", "\xC3\xAB"},
        {"&euro;", "\xE2\x82\xAC"},
        {"&exist;", "\xE2\x88\x83"},
        {"&fnof;", "\xC6\x92"},
        {"&forall;", "\xE2\x88\x80"},
        {"&frac12;", "\xC2\xBD"},
        {"&frac14;", "\xC2\xBC"},
        {"&frac34;", "\xC2\xBE"},
        {"&frasl;", "\xE2\x81\x84"},
        {"&gamma;", "\xCE\xB3"},
        {"&ge;", "\xE2\x89\xA5"},
        {"&gt;", "\x3E"},
        {"&hArr;", "\xE2\x87\x94"},
        {"&harr;", "\xE2\x86\x94"},
        {"&hearts;", "\xE2\x99\xA5"},
        {"&hellip;", "\xE2\x80\xA6"},
        {"&iacute;", "\xC3\xAD"},
        {"&icirc;", "\xC3\xAE"},
        {"&iexcl;", "\xC2\xA1"},
        {"&igrave;", "\xC3\xAC"},
        {"&image;", "\xE2\x84\x91"},
        {"&infin;", "\xE2\x88\x9E"},
        {"&int;", "\xE2\x88\xAB"},
        {"&iota;", "\xCE\xB9"},
        {"&iquest;", "\xC2\xBF"},
        {"&isin;", "\xE2\x88\x88"},
        {"&iuml;", "\xC3\xAF"},
        {"&kappa;", "\xCE\xBA"},
        {"&lArr;", "\xE2\x87\x90"},
        {"&lambda;", "\xCE\xBB"},
        {"&lang;", "\xE2\x8C\xA9"},
        {"&laquo;", "\xC2\xAB"},
        {"&larr;", "\xE2\x86\x90"},
        {"&lceil;", "\xE2\x8C\x88"},
        {"&ldquo;", "\xE2\x80\x9C"},
        {"&le;", "\xE2\x89\xA4"},
        {"&lfloor;", "\xE2\x8C\x8A"},
        {"&lowast;", "\xE2\x88\x97"},
        {"&loz;", "\xE2\x97\x8A"},
        {"&lrm;", "\xE2\x80\x8E"},
        {"&lsaquo;", "\xE2\x80\xB9"},
        {"&lsquo;", "\xE2\x80\x98"},
        {"&lt;", "\x3C"},
        {"&macr;", "\xC2\xAF"},
        {"&mdash;", "\xE2\x80\x94"},
        {"&micro;", "\xC2\xB5"},
        {"&middot;", "\xC2\xB7"},
        {"&minus;", "\xE2\x88\x92"},
        {"&mu;", "\xCE\xBC"},
        {"&nabla;", "\xE2\x88\x87"},
        {"&nbsp;", "\xC2\xA0"},
        {"&ndash;", "\xE2\x80\x93"},
        {"&ne;", "\xE2\x89\xA0"},
        {"&ni;", "\xE2\x88\x8B"},
        {"&not;", "\xC2\xAC"},
        {"&notin;", "\xE2\x88\x89"},
        {"&nsub;", "\xE2\x8A\x84"},
        {"&ntilde;", "\xC3\xB1"},
        {"&nu;", "\xCE\xBD"},
        {"&oacute;", "\xC3\xB3"},
        {"&ocirc;", "\xC3\xB4"},
        {"&oelig;", "\xC5\x93"},
        {"&ograve;", "\xC3\xB2"},
        {"&oline;", "\xE2\x80\xBE"},
        {"&omega;", "\xCF\x89"},
        {"&omicron;", "\xCE\xBF"},
        {"&oplus;", "\xE2\x8A\x95"},
        {"&or;", "\xE2\x88\xA8"},
        {"&ordf;", "\xC2\xAA"},
        {"&ordm;", "\xC2\xBA"},
        {"&oslash;", "\xC3\xB8"},
        {"&otilde;", "\xC3\xB5"},
        {"&otimes;", "\xE2\x8A\x97"},
        {"&ouml;", "\xC3\xB6"},
        {"&para;", "\xC2\xB6"},
        {"&part;", "\xE2\x88\x82"},
        {"&permil;", "\xE2\x80\xB0"},
        {"&perp;", "\xE2\x8A\xA5"},
        {"&phi;", "\xCF\x86"},
        {"&pi;", "\xCF\x80"},
        {"&piv;", "\xCF\x96"},
        {"&plusmn;", "\xC2\xB1"},
        {"&pound;", "\xC2\xA3"},
        {"&prime;", "\xE2\x80\xB2"},
        {"&prod;", "\xE2\x88\x8F"},
        {"&prop;", "\xE2\x88\x9D"},
        {"&psi;", "\xCF\x88"},
        {"&quot;", "\x22"},
        {"&rArr;", "\xE2\x87\x92"},
        {"&radic;", "\xE2\x88\x9A"},
        {"&rang;", "\xE2\x8C\xAA"},
        {"&raquo;", "\xC2\xBB"},
        {"&rarr;", "\xE2\x86\x92"},
        {"&rceil;", "\xE2\x8C\x89"},
        {"&rdquo;", "\xE2\x80\x9D"},
        {"&real;", "\xE2\x84\x9C"},
        {"&reg;", "\xC2\xAE"},
        {"&rfloor;", "\xE2\x8C\x8B"},
        {"&rho;", "\xCF\x81"},
        {"&rlm;", "\xE2\x80\x8F"},
        {"&rsaquo;", "\xE2\x80\xBA"},
        {"&rsquo;", "\xE2\x80\x99"},
        {"&sbquo;", "\xE2\x80\x9A"},
        {"&scaron;", "\xC5\xA1"},
        {"&sdot;", "\xE2\x8B\x85"},
        {"&sect;", "\xC2\xA7"},
        {"&shy;", "\xC2\xAD"},
        {"&sigma;", "\xCF\x83"},
        {"&sigmaf;", "\xCF\x82"},
        {"&sim;", "\xE2\x88\xBC"},
        {"&spades;", "\xE2\x99\xA0"},
        {"&sub;", "\xE2\x8A\x82"},
        {"&sum;", "\xE2\x88\x91"},
        {"&sup;", "\xE2\x8A\x83"},
        {"&sup1;", "\xC2\xB9"},
        {"&sup2;", "\xC2\xB2"},
        {"&sup3;", "\xC2\xB3"},
        {"&supe;", "\xE2\x8A\x87"},
        {"&szlig;", "\xC3\x9F"},
        {"&tau;", "\xCF\x84"},
        {"&there4;", "\xE2\x88\xB4"},
        {"&theta;", "\xCE\xB8"},
        {"&thetasym;", "\xCF\x91"},
        {"&thinsp;", "\xE2\x80\x89"},
        {"&thorn;", "\xC3\xBE"},
        {"&tilde;", "\xCB\x9C"},
        {"&times;", "\xC3\x97"},
        {"&trade;", "\xE2\x84\xA2"},
        {"&uArr;", "\xE2\x87\x91"},
        {"&uacute;", "\xC3\xBA"},
        {"&uarr;", "\xE2\x86\x91"},
        {"&ucirc;", "\xC3\xBB"},
        {"&ugrave;", "\xC3\xB9"},
        {"&uml;", "\xC2\xA8"},
        {"&upsih;", "\xCF\x92"},
        {"&upsilon;", "\xCF\x85"},
        {"&uuml;", "\xC3\xBC"},
        {"&weierp;", "\xE2\x84\x98"},
        {"&xi;", "\xCE\xBE"},
        {"&yacute;", "\xC3\xBD"},
        {"&yen;", "\xC2\xA5"},
        {"&yuml;", "\xC3\xBF"},
        {"&zeta;", "\xCE\xB6"},
        {"&zwj;", "\xE2\x80\x8D"},
        {"&zwnj;", "\xE2\x80\x8C"},
    };
    for (const auto& row : named) matrix("named/" + row.first, row.first, row.second);
    for (const std::string literal : {"&unknown;", "&apos;", "&Tab;", "&NewLine;", "&AMP;", "&amp", "&copy=", "&notit;"})
        matrix("named-literal/" + literal, literal, literal);
    const std::string payload = "&#0;&#xD800;&#65tail;";
    for (const std::string tag : {"script", "style"})
        run("raw/" + tag, "<body><" + tag + " id=raw data-v='&#0;'>" + payload + "</" + tag +
            "><p id=after>tail</p></body>",
            {element(tag, {{"id", "raw"}, {"data-v", replacement}}, {text(payload)}), after()});
    run("comment", "<body><!--" + payload + "--><p id=after>tail</p></body>",
        {{"#comment", payload, {}, {}}, after()});
    // Existing literal bytes are not sanitized globally by this numeric fix.
    validate_utf8 = false;
    matrix("literal-invalid-utf8-preserved", std::string("\xC0\xAF\xED\xA0\x80"), std::string("\xC0\xAF\xED\xA0\x80"));
    validate_utf8 = true;
}
void identity_and_lifecycle() {
    Document first, second;
    const std::pair<std::string, std::string> rows[] = {
        {"&#128;", "\xE2\x82\xAC"}, {"P&#0;Z", "P" + replacement + "Z"},
        {"&#65tail;", "Atail;"}, {"&#65", "A"}, {"&#xG;", "&#xG;"},
        {"&#4294967361;", replacement}
    };
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (const auto& row : rows) {
            run_on(first, "id/reparse/" + row.first,
                "<body><div id='" + row.first + "' data-v='&#0;'>P" + row.first +
                "Z</div><p id=after>tail</p></body>",
                {element("div", {{"id", row.second}, {"data-v", replacement}}, {text("P" + row.second + "Z")}), after()},
                {"&#65;", "P", "old"});
            auto body = aether_doc_get_body(first.handle);
            auto node = aether_node_get_child(body, 0);
            check(aether_doc_get_element_by_id(first.handle, row.second.c_str()) == node, "exact decoded ID");
            if (row.first != row.second)
                check(!aether_doc_get_element_by_id(first.handle, row.first.c_str()), "original ID absent");
            if (row.second != "A") check(!aether_doc_get_element_by_id(first.handle, "A"), "truncated/previous A absent");
            check(!aether_doc_get_element_by_id(first.handle, "\xC2\x80"), "unmapped C1 absent");
            check(aether_element_set_attribute(node, "data-v", "&#0;") == AETHER_OK, "literal setter data");
            check(aether_element_set_attribute(node, "id", "&#65;") == AETHER_OK, "literal setter ID");
            verify_node(first.handle, node, body,
                element("div", {{"id", "&#65;"}, {"data-v", "&#0;"}}, {text("P" + row.second + "Z")}), "mutated");
            check(aether_doc_get_element_by_id(first.handle, "&#65;") == node, "literal ID indexed");
            check(!aether_doc_get_element_by_id(first.handle, row.second.c_str()), "decoded ID removed");
            // The second document must not inherit a saturated accumulator.
            run_on(second, "alternate/document", "<body><div id='&#66;'>P&#x41Z</div><p id=after>tail</p></body>",
                {element("div", {{"id", "B"}}, {text("PAZ")}), after()}, {"&#65;", row.second});
            check(aether_doc_get_element_by_id(first.handle, "&#65;") == node, "other document unchanged");
            // node is never used after the next reparse of first.
        }
    }
    for (const bool reverse : {false, true}) {
        const std::string attrs = reverse ? "id='B' id='&#65;'" : "id='&#65;' id='B'";
        const std::string id = reverse ? "B" : "A";
        run_on(first, "duplicate-attribute", "<body><div " + attrs + "></div><p id=after>tail</p></body>",
            {element("div", {{"id", id}}), after()}, {reverse ? "A" : "B"});
        check(aether_doc_get_element_by_id(first.handle, id.c_str()) ==
              aether_node_get_child(aether_doc_get_body(first.handle), 0), "first attribute wins");
    }
    run_on(first, "duplicate-decoded-IDs", "<body><div id='&#65;'></div><span id=A></span><p id=after>tail</p></body>",
        {element("div", {{"id", "A"}}), element("span", {{"id", "A"}}), after()});
    check(aether_doc_get_element_by_id(first.handle, "A") ==
          aether_node_get_child(aether_doc_get_body(first.handle), 0), "first connected ID wins");
    run_on(first, "reparse-clear", "<body><p id=after>tail</p></body>", {after()}, {"A", "B", "&#65;"});
}
void composition_fuzz() {
    // Construction oracle, not a second implementation of the decoder:
    // choose independently specified input/output atoms, and separate each by
    // 'Z' so incomplete numeric tokens cannot consume the next atom.
    // A fixed LCG makes sequence/case labels reproducible across C++ libraries.
    const std::pair<std::string, std::string> atoms[] = {
        {"&#65", "A"}, {"&#x41;", "A"}, {"&#X1F600", "\xF0\x9F\x98\x80"},
        {"&#0", replacement}, {"&#xD800", replacement}, {"&#4294967361;", replacement},
        {"&#xG;", "&#xG;"}, {"&#", "&#"}, {"&#X", "&#X"},
        {"&", "&"}, {"&&", "&&"}, {"&#65tail;", "Atail;"},
        {"&#65&copy;", "A\xC2\xA9"}, {"&#38;#65;", "&#65;"},
        {"&amp;#0;", "&#0;"}, {"&#x41B;", "\xD0\x9B"},
        {"&#128;", "\xE2\x82\xAC"}, {"&#x81", "\xC2\x81"},
        {"&#x10FFFF", "\xF4\x8F\xBF\xBF"}, {"&#x110000", replacement},
        {"&#65\xD9\xA1;", "A\xD9\xA1;"}, {"&unknown;", "&unknown;"}
    };
    uint32_t state = UINT32_C(0xC001D00D);
    for (int iteration = 0; iteration < 1000; ++iteration) {
        std::string input, expected;
        for (int j = 0; j < 1 + iteration % 31; ++j) {
            state = state * UINT32_C(1664525) + UINT32_C(1013904223);
            const auto& atom = atoms[state % (sizeof(atoms) / sizeof(atoms[0]))];
            input += atom.first + "Z"; expected += atom.second + "Z";
        }
        matrix("fuzz/seed-C001D00D/" + std::to_string(iteration), input, expected);
    }
}
} // namespace
int main() {
    context = "ABI negotiation";
    const auto version = aether_abi_version();
    if (!check(AETHER_ABI_VERSION_GET_MAJOR(version) == AETHER_ABI_VERSION_MAJOR, "ABI major") ||
        !check(AETHER_ABI_VERSION_MAJOR != 0 ||
               AETHER_ABI_VERSION_GET_MINOR(version) == AETHER_ABI_VERSION_MINOR, "pre-1.0 ABI minor") ||
        !check((aether_abi_capabilities() & AETHER_CAPABILITY_DOM) != 0, "DOM capability")) return 1;
    // Guard the output validator itself against accepting malformed UTF-8.
    check(utf8("\x01\x0B\x7F\xC2\x81\xEF\xB7\x90\xF4\x8F\xBF\xBF"),
          "validator accepts controls and noncharacters");
    for (const std::string bad : {"\xC0\xAF", "\xE0\x80\x80", "\xED\xA0\x80",
                                 "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\x80", "\xE2\x82"})
        check(!utf8(bad), "validator rejects malformed bytes");
    check(!utf8(std::string(1, '\0')), "validator rejects generated NUL");
    unicode_matrix(); lexical(); scale();
    boundaries_and_structure(); protected_contexts(); identity_and_lifecycle(); composition_fuzz();
    std::printf("HTML numeric references: cases=%d failed_cases=%d checks=%d failures=%d\n",
                cases, failed_cases, checks, failures);
    return failures ? 1 : 0;
}
