#include "selector_empty_fixture.h"

using namespace empty_test;

namespace {

struct CommentCase {
    const char* markup;
    const char* content;
};

void check_child(aether_node_t parent, int index, aether_node_type_t type,
                 const std::string& content, const std::string& label) {
    const auto node = aether_node_get_child(parent, index);
    require(node != nullptr, label + ": child exists");
    require(aether_node_get_type(node) == type, label + ": child type");
    require(aether_node_get_parent(node) == parent, label + ": parent link");
    if (type != AETHER_NODE_ELEMENT)
        require(text(node) == content, label + ": literal child content");
}

std::vector<DomSnapshot> snapshot(aether_node_t root) {
    std::unordered_set<aether_node_t> nodes;
    Fixture::collect_dom(root, nodes);
    std::vector<DomSnapshot> result;
    for (auto node : nodes) result.emplace_back(node);
    return result;
}

void closed_comments() {
    const CommentCase cases[] = {
        {"<!--ordinary-->", "ordinary"}, {"<!---->", ""},
        {"<!-->", ""}, {"<!--->", ""},
        {"<!--bang--!>", "bang"}, {"<!-- --!>", " "},
        {"<!--<b>&amp; literal</b>-->", "<b>&amp; literal</b>"}
    };
    struct SelectorCase { const char* selector; bool a, b, c; };
    const SelectorCase selectors[] = {
        {".target:empty", true, true, true},
        {".target:first-child", true, false, false},
        {".target:last-child", false, false, true},
        {".target:nth-child(1)", true, false, false},
        {".target:nth-child(2)", false, true, false},
        {".target:nth-child(3)", false, false, true},
        {".target:nth-child(2n+1)", true, false, true},
        {"#a + .target", false, true, false},
        {"#b + .target", false, false, true},
        {"#a ~ .target", false, true, true},
        {"#c ~ .target", false, false, false}
    };
    for (const auto& item : cases) {
        const std::string comment = item.markup;
        const std::string html = "<body><div id='host'>" + comment +
            "<i id='a' class='target'>" + comment + "</i>" + comment +
            "<i id='b' class='target'>" + comment + "</i>" + comment +
            "<i id='c' class='target'>" + comment + "</i>" + comment +
            "</div><div id='text' class='target'>" + comment + "x</div></body>";
        for (const auto& selector : selectors) {
            Fixture f(html, std::string(".target{opacity:.2}") + selector.selector + "{opacity:.8}", 2);
            // Reuse the same document and style runtime across reparses. Only
            // fresh handles survive a reparse; cached style observations do not.
            for (int parse = 0; parse < 2; ++parse) {
                if (parse) {
                    require(aether_doc_parse_html(f.doc, html.c_str()) == AETHER_OK, "reparse comment fixture");
                    f.opacity.clear();
                    aether_style_runtime_clear_cache(f.runtime);
                }
                const auto host = f.get("host");
                const auto a = f.get("a"), b = f.get("b"), c = f.get("c");
                const auto text_host = f.get("text");
                const std::string label = std::string(item.markup) + " " + selector.selector;
                require(aether_node_get_child_count(host) == 7, label + ": full public child sequence");
                for (int index = 0; index < 7; ++index)
                    check_child(host, index, index % 2 ? AETHER_NODE_ELEMENT : AETHER_NODE_COMMENT,
                                item.content, label);
                require(aether_node_get_child(host, 1) == a && aether_node_get_child(host, 3) == b &&
                        aether_node_get_child(host, 5) == c, label + ": element order");
                for (auto element : {a, b, c}) {
                    require(aether_node_get_child_count(element) == 1, label + ": comment-only element");
                    check_child(element, 0, AETHER_NODE_COMMENT, item.content, label);
                }
                require(aether_node_get_child_count(text_host) == 2, label + ": text negative control");
                check_child(text_host, 0, AETHER_NODE_COMMENT, item.content, label);
                check_child(text_host, 1, AETHER_NODE_TEXT, "x", label);
                const auto originals = snapshot(aether_doc_get_body(f.doc));
                for (int pass = 0; pass < 3; ++pass) {
                    f.resolve();
                    f.expect(a, selector.a, label + " a");
                    f.expect(b, selector.b, label + " b");
                    f.expect(c, selector.c, label + " c");
                    if (std::string(selector.selector) == ".target:empty")
                        f.expect(text_host, false, label + ": real text defeats :empty");
                    for (const auto& original : originals) original.verify(label + ": resolved DOM");
                }
            }
        }
    }
}

void eof_comments() {
    // No closing tags after these comments: EOF is the terminator, and pending
    // delimiter bytes are discarded by EOF recovery rather than leaked as text.
    const CommentCase cases[] = {
        {"<!--", ""}, {"<!---", ""}, {"<!--eof", "eof"}, {"<!--eof-", "eof"},
        {"<!--eof--", "eof"}, {"<!--eof--!", "eof"},
        {"<!--<b>&amp;", "<b>&amp;"}
    };
    for (const auto& item : cases) {
        const std::string html = std::string("<body><div id='host'>") + item.markup;
        Fixture f(html, "#host{opacity:.2}#host:empty{opacity:.8}", 2);
        for (int parse = 0; parse < 2; ++parse) {
            if (parse) {
                require(aether_doc_parse_html(f.doc, html.c_str()) == AETHER_OK, "reparse EOF comment");
                f.opacity.clear();
                aether_style_runtime_clear_cache(f.runtime);
            }
            const auto host = f.get("host");
            require(aether_node_get_child_count(host) == 1, "EOF comment retained as one child");
            check_child(host, 0, AETHER_NODE_COMMENT, item.content, item.markup);
            const auto originals = snapshot(aether_doc_get_body(f.doc));
            for (int pass = 0; pass < 3; ++pass) {
                f.resolve();
                f.expect(host, true, "EOF comment ignored by :empty");
                for (const auto& original : originals) original.verify("EOF comment after resolution");
            }
        }
    }
}

} // namespace

int main() {
    closed_comments();
    eof_comments();
    std::printf("HTML comment consumer tests passed (%d checks)\n", checks);
    return 0;
}
