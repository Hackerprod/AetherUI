// Public C ABI only. Checks and mutation operations remain active with NDEBUG.
#include <aether/aether.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

static int checks = 0, failures = 0;
static void require(bool ok, const char* label) {
    if (!ok) {
        std::fprintf(stderr, "SETUP FAILURE: %s\n", label);
        std::exit(2);
    }
}
static void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
    }
}
static std::string take(char* value) {
    std::string text = value ? value : "";
    aether_string_free(value);
    return text;
}
static aether_node_t child(aether_node_t parent, int index) {
    auto node = aether_node_get_child(parent, index);
    require(node != nullptr && aether_node_get_parent(node) == parent, "enumerated expected child");
    return node;
}
static std::vector<aether_node_t> children(aether_node_t node) {
    const int count = aether_node_get_child_count(node);
    require(count >= 0 && count <= 64, "bounded public child count");
    std::vector<aether_node_t> nodes;
    for (int i = 0; i < count; ++i) nodes.push_back(child(node, i));
    require(aether_node_get_child(node, count) == nullptr, "child count agrees with enumeration");
    return nodes;
}
struct NodeState {
    aether_node_t node, parent;
    aether_node_type_t type;
    int64_t style_id;
    std::vector<aether_node_t> children;
    std::map<std::string, std::string> attributes;
    std::string tag, text;
    bool operator==(const NodeState&) const = default;
};
struct Snapshot {
    int64_t version;
    std::vector<NodeState> nodes;
    bool operator==(const Snapshot&) const = default;
};
struct Document {
    aether_doc_t doc = nullptr;
    explicit Document(const char* html) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        parse(html);
    }
    ~Document() { aether_doc_destroy(doc); }
    void parse(const char* html) {
        require(aether_doc_parse_html(doc, html) == AETHER_OK, "parse document");
    }
    aether_node_t body() const {
        auto node = aether_doc_get_body(doc);
        require(node != nullptr, "body exists");
        return node;
    }
    Snapshot snapshot() const {
        Snapshot result{aether_doc_get_version(doc), {}};
        auto root = aether_doc_get_document_element(doc);
        if (!root) return result;
        for (int i = 0; aether_node_get_parent(root); ++i) {
            require(i < 64, "bounded root walk");
            root = aether_node_get_parent(root);
        }
        std::vector<aether_node_t> queue{root};
        for (size_t i = 0; i < queue.size(); ++i) {
            require(queue.size() <= 128, "bounded snapshot traversal");
            auto node = queue[i];
            NodeState state{node, aether_node_get_parent(node), aether_node_get_type(node),
                            aether_node_get_style_node_id(node), children(node), {}, {},
                            take(aether_node_get_text_content(node))};
            if (state.type == AETHER_NODE_ELEMENT) {
                state.tag = take(aether_element_get_tag_name(node));
                const int count = aether_element_get_attribute_count(node);
                require(count >= 0 && count < 64, "bounded attribute count");
                for (int a = 0; a < count; ++a) {
                    auto name = take(aether_element_get_attribute_name(node, a));
                    auto value = take(aether_element_get_attribute_value(node, a));
                    require(state.attributes.emplace(name, value).second, "unique attribute name");
                }
            }
            for (auto c : state.children) {
                require(std::find(queue.begin(), queue.end(), c) == queue.end(), "acyclic snapshot traversal");
                queue.push_back(c);
            }
            result.nodes.push_back(std::move(state));
        }
        return result;
    }
    void expect(const char* id, aether_node_t expected, const char* label) const {
        const auto before = snapshot();
        check(aether_doc_get_element_by_id(doc, id) == expected, label);
        check(snapshot() == before, std::string(label) + ": lookup preserves DOM and version");
    }
};
static void append(aether_node_t parent, aether_node_t node) {
    require(aether_node_append_child(parent, node) == AETHER_OK, "valid append");
}
static void remove(aether_node_t parent, aether_node_t node) {
    require(aether_node_remove_child(parent, node) == AETHER_OK, "valid remove");
    require(aether_node_get_parent(node) == nullptr, "removed node is disconnected");
}
static void set_id(aether_node_t node, const char* id) {
    require(aether_element_set_attribute(node, "id", id) == AETHER_OK, "set ID");
}
static void siblings_and_moves() {
    Document d("<body><i id=dup data-label=first style='display:none'>first</i>"
               "<i id=dup data-label=second>second</i><i id=dup data-label=third>third</i>"
               "<b id=other></b><b id=other></b></body>");
    auto body = d.body(), a = child(body, 0), b = child(body, 1), c = child(body, 2);
    auto x = child(body, 3), y = child(body, 4);
    d.expect("dup", a, "three duplicates: first wins even display:none");
    d.expect("other", x, "second duplicate group is independent");
    for (int i = 0; i < 3; ++i) d.expect("dup", a, "repeated lookup");
    append(body, a);
    check(children(body) == std::vector<aether_node_t>({b, c, x, y, a}), "move winner to end: public order");
    d.expect("dup", b, "move winner to end");
    append(body, c);
    check(children(body) == std::vector<aether_node_t>({b, x, y, a, c}), "move later duplicate: public order");
    d.expect("dup", b, "moving later duplicate preserves winner");
    const auto before = d.snapshot();
    append(body, c);
    check(d.snapshot() == before, "append last child preserves tree");
    d.expect("dup", b, "append already-last duplicate");
    remove(body, c);
    d.expect("dup", b, "disconnect last duplicate");
    remove(body, b);
    d.expect("dup", a, "disconnect first duplicate");
    remove(body, a);
    d.expect("dup", nullptr, "all duplicates disconnected");
    set_id(b, "offline");
    d.expect("offline", nullptr, "detached setter cannot make node connected");
    append(body, b);
    d.expect("offline", b, "detached ID becomes visible on reconnect");
    append(body, a); append(body, c);
    d.expect("dup", a, "reconnected duplicate order");
    d.expect("other", x, "unrelated group unchanged by moves");
    // Every detached node is reconnected before document destruction.
}
static void preorder_and_branches() {
    Document d("<body><section><i id=dup><b id=dup></b></i></section>text<!-- comment -->"
               "<aside><i id=dup></i></aside><footer id=dup></footer></body>");
    auto body = d.body(), left = child(body, 0), right = child(body, 3), last = child(body, 4);
    auto a = child(left, 0), nested = child(a, 0), b = child(right, 0);
    require(aether_node_get_type(child(body, 1)) == AETHER_NODE_TEXT &&
            aether_node_get_type(child(body, 2)) == AETHER_NODE_COMMENT, "text/comment fixture");
    d.expect("dup", a, "deep first subtree beats shallow later match");
    set_id(left, "dup");
    d.expect("dup", left, "ancestor before child and grandchild");
    set_id(left, "");
    d.expect("dup", a, "ancestor ID removed");
    append(right, a);
    check(children(left).empty() && children(right) == std::vector<aether_node_t>({b, a}), "move across branches");
    d.expect("dup", b, "first match after move to later branch");
    append(left, a);
    check(children(left) == std::vector<aether_node_t>({a}) && children(right) == std::vector<aether_node_t>({b}),
          "move back across branches");
    d.expect("dup", a, "first match after move to earlier branch");
    remove(body, left);
    d.expect("dup", b, "detached subtree with several matches");
    append(body, left);
    d.expect("dup", b, "reattached subtree after earlier branch");
    append(body, right);
    d.expect("dup", last, "shallow match now precedes moved branches");
    append(body, last);
    d.expect("dup", a, "nested match wins in new branch order");
    const auto before = d.snapshot();
    require(aether_node_append_child(nested, left) == AETHER_ERROR_INVALID_STATE, "cycle rejected before traversal");
    require(aether_node_append_child(a, a) == AETHER_ERROR_INVALID_STATE, "self insertion rejected");
    check(d.snapshot() == before, "rejected mutation preserves order and version");
    d.expect("dup", a, "same winner after rejected mutations");
}
static void attribute_changes() {
    Document d("<body><i id=other></i><i id=dup></i><i id=dup></i><i id=other></i></body>");
    auto body = d.body(), a = child(body, 0), b = child(body, 1), c = child(body, 2), last = child(body, 3);
    d.expect("dup", b, "initial ID winner");
    d.expect("other", a, "initial other ID winner");
    set_id(a, "dup");
    d.expect("dup", a, "earlier nonmatch becomes winner");
    d.expect("other", last, "group separation after convergence");
    set_id(a, "dup");
    d.expect("dup", a, "same ID assignment preserves first match");
    set_id(c, "other");
    d.expect("dup", a, "later candidate ID changed");
    d.expect("other", c, "later candidate precedes other group");
    set_id(a, "other");
    d.expect("dup", b, "winner renamed");
    d.expect("other", a, "renamed winner converges with other group");
    set_id(b, "");
    d.expect("dup", nullptr, "last match cleared");
    d.expect("", nullptr, "empty attribute does not match");
    set_id(c, "dup"); set_id(b, "dup");
    d.expect("dup", b, "restore earlier match after later match");
    set_id(a, "dup"); set_id(last, "dup");
    d.expect("other", nullptr, "old ID absent after all changes");
    d.expect("dup", a, "converged group still uses tree order");
}
static void exact_values_and_reparse() {
    Document d("<body><i id=A></i><i id=a></i><i id=' A '></i><i id=' '></i>"
               "<i id='&#65;'></i><i id=''></i><i></i><i id='\xC3\xA9'></i>"
               "<i id='\xC3\xA9'></i><i id='e\xCC\x81'></i>"
               "<aether-anonymous-flex-item id=named></aether-anonymous-flex-item><i id=named></i></body>");
    auto body = d.body();
    d.expect("A", child(body, 0), "decoded entity duplicates exact uppercase ID");
    d.expect("a", child(body, 1), "case-sensitive lowercase ID");
    d.expect(" A ", child(body, 2), "leading and trailing spaces preserved");
    d.expect(" ", child(body, 3), "space-only ID is nonempty");
    d.expect("A ", nullptr, "no trimming or partial matching");
    d.expect("\xC3\xA9", child(body, 7), "duplicate UTF-8 ID first match");
    d.expect("e\xCC\x81", child(body, 9), "decomposed Unicode remains distinct");
    d.expect("named", child(body, 10), "authored layout-wrapper tag still participates");
    d.expect("", nullptr, "empty ID returns null");
    d.expect(nullptr, nullptr, "null ID returns null");
    d.expect("absent", nullptr, "missing ID returns null");
    check(aether_doc_get_element_by_id(nullptr, "A") == nullptr, "null document returns null");
    check(aether_doc_get_element_by_id(nullptr, nullptr) == nullptr, "null document and ID return null");
    const auto version = aether_doc_get_version(d.doc);
    d.parse("<body><i id=new></i><i id=new></i></body>");
    check(aether_doc_get_version(d.doc) > version, "parse advances generation");
    d.expect("new", child(d.body(), 0), "new duplicate tree after reparse");
    for (const auto* old : {"A", "a", "named", "\xC3\xA9"}) d.expect(old, nullptr, "old ID absent after reparse");
    d.parse("");
    d.expect("new", nullptr, "empty reparse clears matches");
    d.expect("", nullptr, "empty reparse and empty ID");
    d.parse("<body><b id=new></b><b id=new></b></body>");
    d.expect("new", child(d.body(), 0), "reparse following empty tree");
    // No handles from an earlier parse are dereferenced or compared to new ones.
}
int main() {
    siblings_and_moves();
    preorder_and_branches();
    attribute_changes();
    exact_values_and_reparse();
    std::printf("SUMMARY dom-id-order checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
