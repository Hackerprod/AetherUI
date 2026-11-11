// Public ABI regression. Every check remains active with Release/NDEBUG.
#include <aether/aether.h>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
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
std::string take(char* text) {
    check(text != nullptr, "owned public string");
    std::string result = text ? text : "";
    aether_string_free(text);
    return result;
}
struct Document {
    aether_doc_t handle{};
    Document() { check(aether_doc_create(&handle) == AETHER_OK && handle, "create document"); }
    ~Document() { aether_doc_destroy(handle); }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
};
std::string hex(const std::string& value) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (unsigned char c : value) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
void bytes(const std::string& actual, const std::string& expected) {
    // Keep long sanity-check diagnostics bounded, without truncating comparison.
    check(actual == expected, "literal text bytes: got " + hex(actual.substr(0, 80)) +
          " expected " + hex(expected.substr(0, 80)));
}
std::string text_only(aether_node_t node, unsigned depth = 0) {
    if (!check(node && depth < 32, "bounded text walk")) return {};
    if (aether_node_get_type(node) == AETHER_NODE_COMMENT) return {};
    if (aether_node_get_type(node) == AETHER_NODE_TEXT)
        return take(aether_node_get_text_content(node));
    const int count = aether_node_get_child_count(node);
    if (!check(count >= 0 && count < 64, "bounded text children")) return {};
    std::string result;
    for (int i = 0; i < count; ++i) result += text_only(aether_node_get_child(node, i), depth + 1);
    return result;
}
void integrity_node(aether_doc_t doc, aether_node_t node, aether_node_t parent,
                    std::set<aether_node_t>& nodes, std::set<int64_t>& ids, unsigned depth) {
    if (!check(node && depth < 32 && nodes.size() < 256, "bounded topology")) return;
    if (!check(nodes.insert(node).second, "unique node handle")) return;
    check(aether_node_get_parent(node) == parent, "reciprocal parent link");
    const auto type = aether_node_get_type(node);
    if (type != AETHER_NODE_DOCUMENT) {
        const auto id = aether_node_get_style_node_id(node);
        check(id > 0 && ids.insert(id).second, "positive unique style ID within parse");
    }
    if (type == AETHER_NODE_TEXT)
        check(!take(aether_node_get_text_content(node)).empty(), "no residual empty text node");
    if (type == AETHER_NODE_ELEMENT) {
        const int attrs = aether_element_get_attribute_count(node);
        if (check(attrs >= 0 && attrs < 32, "bounded attributes")) {
            for (int i = 0; i < attrs; ++i) {
                const auto key = take(aether_element_get_attribute_name(node, i));
                const auto value = take(aether_element_get_attribute_value(node, i));
                check(take(aether_element_get_attribute(node, key.c_str())) == value, "attribute getters agree");
                if (key == "id")
                    check(aether_doc_get_element_by_id(doc, value.c_str()) == node, "public ID lookup identity");
            }
        }
    }
    const int count = aether_node_get_child_count(node);
    if (!check(count >= 0 && count < 64, "bounded child count")) return;
    for (int i = 0; i < count; ++i)
        integrity_node(doc, aether_node_get_child(node, i), node, nodes, ids, depth + 1);
    check(!aether_node_get_child(node, count), "no extra child");
}
void integrity(aether_doc_t doc) {
    auto root = aether_doc_get_document_element(doc);
    if (!root) return;
    auto sentinel = aether_node_get_parent(root);
    if (!check(sentinel != nullptr, "document parent exists")) return;
    std::set<aether_node_t> nodes;
    std::set<int64_t> ids;
    integrity_node(doc, sentinel, nullptr, nodes, ids, 0);
}
bool parse(aether_doc_t doc, const std::string& html) {
    const auto old_version = aether_doc_get_version(doc);
    if (!check(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse succeeds")) return false;
    check(aether_doc_get_version(doc) == old_version + 1, "one version advance");
    integrity(doc);
    return true;
}
aether_node_t target(aether_doc_t doc, const char* id = "target") {
    auto node = aether_doc_get_element_by_id(doc, id);
    check(node != nullptr, std::string("element exists: ") + id);
    if (node) check(take(aether_element_get_attribute(node, "id")) == id, "ID attribute preserved");
    return node;
}
void child_text(aether_node_t parent, int index, const std::string& expected) {
    auto child = aether_node_get_child(parent, index);
    if (!check(child && aether_node_get_type(child) == AETHER_NODE_TEXT, "exact text child")) return;
    check(aether_node_get_parent(child) == parent, "text parent");
    bytes(take(aether_node_get_text_content(child)), expected);
}
void dom_case(aether_doc_t doc, const std::string& tag, const char* name,
              const std::string& source, const std::string& expected, const std::string& attrs = "") {
    context = tag + "/" + name;
    ++cases;
    const std::string html = "<html><body><" + tag + " id='target' " + attrs + ">" + source +
        "</" + tag + "><i id='after'>tail</i></body></html>";
    if (!parse(doc, html)) return;
    auto node = target(doc);
    auto after = target(doc, "after");
    if (!node || !after) return;
    bytes(text_only(node), expected);
    bytes(text_only(after), "tail");
    check(aether_node_get_parent(node) == aether_node_get_parent(after), "sentinel remains sibling");
    if (expected.empty()) check(aether_node_get_child_count(node) == 0, "no empty text allocation");
}
void proposal_cases(Document& doc) {
    struct Sample { const char* name; const char* source; const char* expected; };
    const Sample samples[]={
        {"LF","\nA","A"},{"CRLF","\r\nA","A"},{"CR","\rA","A"},
        {"double-LF","\n\nA","\nA"},{"decimal-LF","&#10;A","A"},
        {"hex-LF","&#xA;A","A"},{"decimal-LF-no-semicolon","&#10A","A"},
        {"decoded-CR","&#13;A","\rA"},{"space-LF"," \nA"," \nA"},
        {"tab-LF","\t\nA","\t\nA"},{"ordinary-text","A\nB","A\nB"},
        {"comment-barrier","<!--x-->\nA","\nA"},
        {"element-barrier","<span></span>\nA","\nA"},
        {"unmatched-end-barrier","</aside>\nA","\nA"},
        {"empty-end-no-token","</>\nA","A"},
        {"empty-end-no-rearm","A</>\nB","A\nB"},
        {"repeated-empty-end-no-token","</></>&#10;A","A"},
        {"empty-end-then-comment","</><!--x-->\nA","\nA"},
        {"space-end-bogus-comment","</ >\nA","\nA"},
        {"tab-end-bogus-comment","</\t>\nA","\nA"},
        {"declaration-barrier","<!DOCTYPE html>\nA","\nA"},
        {"invalid-less-than","<\nA","<\nA"},
        {"decoded-less-than","&lt;\nA","<\nA"},
        {"only-LF","\n",""},{"empty","",""},
        {"unknown-reference","&unknown;\nA","&unknown;\nA"},
        {"LF-then-markup","\n<span>A</span>","A"}
    };
    for (const char* tag:{"pre","PRE","listing"}) for (const auto& s:samples) dom_case(doc.handle,tag,s.name,s.source,s.expected);
    dom_case(doc.handle,"pRe","mixed-case-attributes","\nA","A","data-x='value' title='>'");
    dom_case(doc.handle,"LiStInG","mixed-case-attributes","\r\nA","A","data-x='value' title='>'");
    dom_case(doc.handle,"pre+foo","legacy-truncated-name","\nA","\nA");
    dom_case(doc.handle,"listing+foo","legacy-truncated-name","\nA","\nA");
    dom_case(doc.handle,"pre:foo","colon-name","\nA","\nA");
    dom_case(doc.handle,"div","control-LF","\nA","\nA");
    dom_case(doc.handle,"textarea","control-LF","\nA","A");
    dom_case(doc.handle,"title","control-LF","\nA","\nA");
    dom_case(doc.handle,"script","control-LF","\nA","\nA");
    dom_case(doc.handle,"style","control-LF","\nA","\nA");

    check(cases == 91, "all 91 approved baseline cases retained");
}
void supplemental_cases(Document& doc) {
    struct Sample { const char* name; const char* source; const char* expected; };
    const Sample samples[] = {
        {"processing-instruction", "<?x?>\nA", "\nA"},
        {"FF-before-LF", "\f\nA", "\f\nA"},
        {"VT-before-LF", "\v\nA", "\v\nA"},
        {"source-CR-then-decoded-CR", "\r&#13;A", "\rA"},
        {"decoded-CR-then-source-CRLF", "&#13;\r\nA", "\r\nA"},
        {"not-double-decoded", "&amp;#10;\nA", "&#10;\nA"},
        {"unsupported-named-newline", "&NewLine;\nA", "&NewLine;\nA"},
        {"empty-end-then-space", "</> \nA", " \nA"},
        {"empty-end-then-reference-CR", "</>&#13;\nA", "\r\nA"},
        {"LF-then-empty-end-no-rearm", "\n</>\nA", "\nA"},
        {"LF-then-declaration-no-rearm", "\n<!X>\nA", "\nA"},
        {"UTF8-and-trailing-LF", "\né東\n", "é東\n"}
    };
    for (const char* tag : {"pre", "listing"})
        for (const auto& s : samples) dom_case(doc.handle, tag, s.name, s.source, s.expected);
}
void structures(Document& doc, const std::string& tag) {
    auto start = [&](const char* name, const std::string& html) {
        context = tag + "/" + name;
        ++cases;
        return parse(doc.handle, html);
    };
    const std::string open = "<" + tag + " id=target>";
    const std::string close = "</" + tag + ">";
    if (start("attributes-and-single-LF", "<body><" + tag +
              " ID=target DATA-X='first' data-x='discarded' title='>'>\n" + close +
              "<i id=after>tail</i></body>")) {
        auto node = target(doc.handle);
        auto after = target(doc.handle, "after");
        check(aether_node_get_child_count(node) == 0, "single LF creates no text node");
        check(aether_element_get_attribute_count(node) == 3, "exact attributes, duplicate ignored");
        check(take(aether_element_get_attribute(node, "data-x")) == "first", "first attribute retained");
        check(take(aether_element_get_attribute(node, "title")) == ">", "quoted terminator retained");
        check(aether_node_get_parent(node) == aether_node_get_parent(after), "sentinel sibling after no text");
        child_text(after, 0, "tail");
    }
    if (start("comment-retention", open + "<!--x-->\nA" + close)) {
        auto node = target(doc.handle);
        check(aether_node_get_child_count(node) == 2, "comment and text are separate");
        auto comment = aether_node_get_child(node, 0);
        check(aether_node_get_type(comment) == AETHER_NODE_COMMENT, "comment retained");
        bytes(take(aether_node_get_text_content(comment)), "x");
        child_text(node, 1, "\nA");
    }
    if (start("consumed-empty-before-comment", open + "\n<!---->\nA" + close)) {
        auto node = target(doc.handle);
        check(aether_node_get_child_count(node) == 2, "no first empty text node");
        auto comment = aether_node_get_child(node, 0);
        check(aether_node_get_type(comment) == AETHER_NODE_COMMENT, "empty comment retained");
        bytes(take(aether_node_get_text_content(comment)), "");
        child_text(node, 1, "\nA");
    }
    if (start("nested-replaces-opportunity", open + "<listing id=inner>\nA</listing>\nB" + close)) {
        auto node = target(doc.handle);
        auto inner = target(doc.handle, "inner");
        check(aether_node_get_child_count(node) == 2 && aether_node_get_child(node, 0) == inner,
              "nested element and trailing text");
        check(aether_node_get_parent(inner) == node, "inner parent");
        child_text(inner, 0, "A");
        child_text(node, 1, "\nB");
    }
    if (start("close-cancels-context", "<body>" + open + close + "\nA</body>")) {
        auto body = aether_doc_get_body(doc.handle);
        check(aether_node_get_child_count(body) == 2, "closed element followed by parent text");
        child_text(body, 1, "\nA");
    }
    for (const auto& suffix : {std::string("/>"), std::string(" />"), std::string(" data-v=x />")}) {
        if (start("simplified-self-close", "<body><" + tag + " id='target'" + suffix + "\nA</body>")) {
            auto node = target(doc.handle);
            auto body = aether_doc_get_body(doc.handle);
            check(aether_node_get_child_count(node) == 0, "self-close does not open context");
            check(aether_node_get_child_count(body) == 2, "LF belongs to parent");
            child_text(body, 1, "\nA");
        }
    }
    if (start("unquoted-slash-data", "<" + tag + " id=target data-v=x/>\nA" + close)) {
        auto node = target(doc.handle);
        check(take(aether_element_get_attribute(node, "data-v")) == "x/", "slash stays in unquoted attribute");
        check(aether_node_get_child_count(node) == 1, "ordinary context opened");
        child_text(node, 0, "A");
    }
    for (const char boundary : {'\v', '\f'}) {
        if (start(boundary == '\v' ? "invalid-VT-boundary" : "valid-FF-boundary",
                  "<" + tag + boundary + "id=target>\nA" + close)) {
            auto node = target(doc.handle);
            check(take(aether_element_get_tag_name(node)) == tag, "legacy stored tag retained");
            child_text(node, 0, boundary == '\v' ? "\nA" : "A");
        }
    }
    if (start("invalid-less-than-topology", open + "<\nA" + close)) {
        auto node = target(doc.handle);
        check(aether_node_get_child_count(node) == 2, "literal less-than and following text");
        child_text(node, 0, "<");
        child_text(node, 1, "\nA");
    }
    if (start("no-rearm-after-tokenless-close", open + "A</>\nB" + close)) {
        auto node = target(doc.handle);
        check(aether_node_get_child_count(node) == 2, "discarded syntax does not coalesce text");
        child_text(node, 0, "A");
        child_text(node, 1, "\nB");
    }
    // Real EOF, not a wrapper that could accidentally terminate a start tag.
    for (const std::string tail : {"", " data-v='>"}) {
        if (start("incomplete-opening-EOF", "<" + tag + " id=target" + tail)) {
            check(!aether_doc_get_element_by_id(doc.handle, "target"), "unfinished opening absent");
            check(!aether_doc_get_document_element(doc.handle), "no invented root at EOF");
        }
    }
    if (start("complete-opening-EOF", open)) {
        auto node = target(doc.handle);
        check(aether_node_get_child_count(node) == 0, "complete opening has no EOF text");
        // Independent parse while the old document's final opener was eligible.
        Document other;
        check(parse(other.handle, "<div id=target>\nA</div>"), "independent document parse");
        child_text(target(other.handle), 0, "\nA");
    }
    if (start("reparse-reset", "<div id=fresh>\nA</div>")) {
        check(!aether_doc_get_element_by_id(doc.handle, "target"), "previous ID removed");
        child_text(target(doc.handle, "fresh"), 0, "\nA");
    }
    if (start("only-LF-at-EOF", open + "\n")) {
        auto node = target(doc.handle);
        check(aether_node_get_child_count(node) == 0, "only LF at EOF omitted entirely");
    }
    if (start("complete-text-at-EOF", open + "\nA")) child_text(target(doc.handle), 0, "A");
    if (start("incomplete-child-EOF", open + "<span id=unfinished")) {
        auto node = target(doc.handle);
        check(aether_node_get_child_count(node) == 0, "incomplete child absent");
        check(!aether_doc_get_element_by_id(doc.handle, "unfinished"), "incomplete child ID absent");
    }
    if (start("append-child-preserves-text", "<body>" + open + close +
              "<div id=donor>\nA</div></body>")) {
        auto node = target(doc.handle);
        auto donor = target(doc.handle, "donor");
        auto text = aether_node_get_child(donor, 0);
        const auto id = aether_node_get_style_node_id(text);
        child_text(donor, 0, "\nA");
        check(aether_node_append_child(node, text) == AETHER_OK, "move existing text within document");
        check(aether_node_get_child_count(donor) == 0 && aether_node_get_child_count(node) == 1, "move child counts");
        check(aether_node_get_child(node, 0) == text && aether_node_get_parent(text) == node, "same node and new parent");
        check(aether_node_get_style_node_id(text) == id, "same-node style ID preserved during mutation");
        child_text(node, 0, "\nA");
        integrity(doc.handle);
    }
}
} // namespace
int main() {
    context = "ABI";
    check(aether_abi_version() == AETHER_ABI_VERSION_CURRENT, "ABI version unchanged");
    Document doc;
    proposal_cases(doc);
    supplemental_cases(doc);
    for (const char* tag : {"pre", "listing"}) structures(doc, tag);
    dom_case(doc.handle, "pre", "64-KiB-byte-sanity", "\n" + std::string(65536, 'x'), std::string(65536, 'x'));
    check(cases <= 200, "bounded regression case count");
    std::printf("HTML pre/listing leading LF: cases=%d proposal_cases=91 checks=%d failures=%d NDEBUG=%s\n",
                cases, checks, failures,
#ifdef NDEBUG
                "yes"
#else
                "no"
#endif
    );
    return failures ? 1 : 0;
}
