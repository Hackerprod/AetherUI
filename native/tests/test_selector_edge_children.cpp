#include "style_runtime_types.h"
#include "../src/dom/dom_types.h"
#include "selector_empty_fixture.h"

#include <memory>
#include <utility>

namespace {
using empty_test::require;
uint64_t digest = 14695981039346656037ULL;
void record(bool value) { digest = (digest ^ static_cast<uint64_t>(value)) * 1099511628211ULL; }

// Independent old index/count semantics. Only normalize the subject; never
// normalize traversal candidates. Nested/combinator masks are additionally
// emitted for before/after comparison of this harness against the base commit.
bool dom_element(const aether_node_s* n) {
    return n && n->type == AETHER_NODE_ELEMENT && !n->is_generated && !n->anonymous_flex_owner;
}
aether_node_s* subject(aether_node_s* n) {
    if (!n || n->anonymous_flex_owner) return nullptr;
    if (n->is_generated) {
        if (n->type != AETHER_NODE_ELEMENT ||
            (n->pseudo_element != aether_pseudo_element_t::before &&
             n->pseudo_element != aether_pseudo_element_t::after) || n->style_origin == n)
            return nullptr;
        n = n->style_origin;
    }
    return dom_element(n) ? n : nullptr;
}
bool oracle(aether_node_s* raw, bool only) {
    auto* n = subject(raw);
    if (!n) return false;
    if (!n->parent) return !only;
    if (n->parent->is_generated || n->parent->anonymous_flex_owner) return false;
    int index = -1, count = 0;
    for (auto* sibling : n->parent->children) {
        if (!dom_element(sibling)) continue;
        if (sibling == n) index = count;
        ++count;
    }
    return index == 0 && (!only || count == 1);
}

struct Arena {
    std::vector<std::unique_ptr<aether_node_s>> nodes;
    aether_node_s* make(aether_node_type_t type = AETHER_NODE_ELEMENT) {
        auto node = std::make_unique<aether_node_s>();
        node->type = type;
        node->tag_name = "div";
        node->attributes["id"] = "same"; // IDs cannot substitute for identity.
        node->attributes["class"] = "item";
        node->style_node_id = 7; // Nor can style IDs.
        auto* result = node.get();
        nodes.push_back(std::move(node));
        return result;
    }
    static void children(aether_node_s* parent, std::initializer_list<aether_node_s*> nodes) {
        for (auto* child : parent->children) child->parent = nullptr;
        parent->children.assign(nodes);
        for (auto* child : parent->children) child->parent = parent;
    }
};

// Snapshot private representation too, including generated children that public
// navigation intentionally hides. Matchers must neither rewrite nor cache in DOM.
struct Snapshot {
    aether_node_s* node;
    aether_node_s* parent;
    std::vector<aether_node_s*> children;
    decltype(aether_node_s::attributes) attributes;
    std::string tag, text;
    aether_node_type_t type;
    int32_t state;
    int64_t id;
    bool generated;
    aether_pseudo_element_t pseudo;
    aether_node_s* origin;
    aether_node_s* wrapper;
    uint64_t mutation, structure;
    explicit Snapshot(aether_node_s* n)
        : node(n), parent(n->parent), children(n->children), attributes(n->attributes),
          tag(n->tag_name), text(n->text), type(n->type), state(n->state), id(n->style_node_id),
          generated(n->is_generated), pseudo(n->pseudo_element), origin(n->style_origin),
          wrapper(n->anonymous_flex_owner), mutation(n->identity->mutation_epoch),
          structure(n->identity->structure_epoch) {}
    void verify() const {
        require(node->parent == parent && node->children == children && node->attributes == attributes &&
                node->tag_name == tag && node->text == text && node->type == type && node->state == state &&
                node->style_node_id == id && node->is_generated == generated && node->pseudo_element == pseudo &&
                node->style_origin == origin && node->anonymous_flex_owner == wrapper &&
                node->identity->mutation_epoch == mutation && node->identity->structure_epoch == structure,
                "matching must preserve private DOM representation");
    }
};

struct Differential {
    std::vector<std::pair<std::string, compiled_selector_s>> selectors;
    Differential() {
        for (const char* source : {
            "*", ":first-child", ":only-child", ":last-child", ":first-child:last-child",
            "#same:first-child", "#same:only-child", ":first-child:only-child",
            ":is(:first-child)", ":where(:only-child)", ":not(:first-child)", ":not(:only-child)",
            ":not(:not(:first-child))", ":not(:not(:only-child))",
            ":is(:where(:first-child), :only-child)", ":is(:only-child, :nth-child(2))",
            ":has(:first-child)", ":has(:only-child)", ":has(.item:first-child + .item)",
            ":has(.item:first-child ~ .item:last-child)", ":has(:has(:only-child))",
            ".item > .item:first-child", ".item .item:only-child",
            ".item:first-child + .item", ".item:first-child ~ .item",
            ".item:only-child > .item", ".item:only-child .item:first-child",
            ":nth-child(0)", ":nth-child(1)", ":nth-child(2)", ":nth-child(n)",
            ":nth-child(odd)", ":nth-child(even)", ":nth-child(-n+2)",
            ":first-child:nth-child(1)", ":only-child:nth-child(1)", ":root:first-child"
        }) {
            compiled_selector_s compiled;
            require(selector_parse(source, compiled), std::string("compile ") + source);
            selectors.emplace_back(source, std::move(compiled));
        }
    }
    void check(const Arena& arena, const char* label) const {
        std::vector<Snapshot> snapshots;
        for (const auto& node : arena.nodes) snapshots.emplace_back(node.get());
        for (const auto& [source, compiled] : selectors) {
            require(!selector_match(compiled, nullptr), "null subject");
            for (const auto& node : arena.nodes) {
                const bool actual = selector_match(compiled, node.get());
                record(actual);
                const bool valid = subject(node.get()) != nullptr;
                const bool first = oracle(node.get(), false);
                const bool only = oracle(node.get(), true);
                bool expected = false;
                bool checked = true;
                if (source == ":first-child" || source == ":is(:first-child)" ||
                    source == ":not(:not(:first-child))")
                    expected = first;
                else if (source == ":only-child" || source == ":where(:only-child)" ||
                         source == ":not(:not(:only-child))")
                    expected = only;
                else if (source == ":not(:first-child)")
                    expected = valid && !first;
                else if (source == ":not(:only-child)")
                    expected = valid && !only;
                else if (source == ":is(:where(:first-child), :only-child)")
                    expected = first || only;
                else if (source == ":first-child:only-child")
                    expected = first && only;
                else
                    checked = false;
                if (checked)
                    require(actual == expected, std::string(label) + ": independent oracle " + source);
            }
        }
        for (const auto& snapshot : snapshots) snapshot.verify();
    }
    void expect(aether_node_s* node, const char* source, bool expected) const {
        for (const auto& [name, compiled] : selectors) {
            if (name != source) continue;
            require(selector_match(compiled, node) == expected, std::string("explicit ") + source);
            return;
        }
        require(false, "expected selector is in compiled matrix");
    }
};

void private_matrix() {
    const Differential d; // Reused throughout every topology mutation.
    Arena arena;
    auto* doc = arena.make(AETHER_NODE_DOCUMENT);
    auto* html = arena.make(); html->tag_name = "html";
    auto* parent = arena.make();
    auto* a = arena.make(); auto* b = arena.make(); auto* c = arena.make();
    auto* missing = arena.make();
    auto* text = arena.make(AETHER_NODE_TEXT);
    auto* comment = arena.make(AETHER_NODE_COMMENT);
    auto* before = arena.make(); auto* after = arena.make();
    auto* wrapper = arena.make();
    for (auto* generated : {before, after}) {
        generated->is_generated = true;
        generated->style_origin = parent;
    }
    before->pseudo_element = aether_pseudo_element_t::before;
    after->pseudo_element = aether_pseudo_element_t::after;
    wrapper->anonymous_flex_owner = parent;
    wrapper->style_origin = parent;
    auto* hidden = arena.make();
    Arena::children(before, {hidden});
    Arena::children(doc, {html});
    Arena::children(html, {parent});
    missing->parent = parent;
    d.check(arena, "empty parent and missing membership");
    d.expect(missing, ":first-child", false);
    d.expect(missing, ":only-child", false);
    d.expect(a, ":first-child", true);
    d.expect(a, ":only-child", false);
    d.expect(html, ":only-child", true);
    d.expect(hidden, ":first-child", false);
    d.expect(hidden, ":only-child", false);

    for (int cycle = 0; cycle < 3; ++cycle) {
        Arena::children(parent, {text, before, comment, wrapper, after});
        d.check(arena, "zero DOM elements with synthetic and text children");
        Arena::children(parent, {text, before, comment, a, wrapper, after});
        d.check(arena, "one DOM element");
        d.expect(a, ":first-child", true); d.expect(a, ":only-child", true);
        d.expect(missing, ":first-child", false); d.expect(missing, ":only-child", false);
        Arena::children(parent, {before, text, a, comment, b, wrapper, c, after});
        d.check(arena, "three elements separated by non-DOM nodes");
        d.expect(a, ":first-child", true); d.expect(b, ":first-child", false);
        d.expect(c, ":first-child", false); d.expect(a, ":only-child", false);
        Arena::children(parent, {c, after, comment, b, wrapper, a, text, before});
        d.check(arena, "reorder same nodes and IDs");
        d.expect(c, ":first-child", true); d.expect(a, ":first-child", false);
        Arena::children(parent, {before, text, b, after});
        d.check(arena, "remove back to singleton");
        d.expect(b, ":only-child", true);
        Arena::children(parent, {before, text, b, a, after});
        d.check(arena, "reinsert");
        Arena::children(parent, {before, text, b, after});
        Arena::children(b, {a});
        d.check(arena, "reparent nested singleton");
        Arena::children(b, {});
        for (const char* value : {"", "text", " ", "\t\n"}) {
            text->text = value;
            d.check(arena, "text transitions preserve child position");
        }
        before->style_origin = b; after->style_origin = b;
        d.check(arena, "pseudo subjects normalize to singleton host");
        d.expect(before, ":only-child", true); d.expect(after, ":first-child", true);
        before->style_origin = parent; after->style_origin = parent;
    }

    auto* invalid = arena.make();
    invalid->is_generated = true; invalid->pseudo_element = aether_pseudo_element_t::before;
    for (auto* origin : {static_cast<aether_node_s*>(nullptr), invalid, before, wrapper, text, doc}) {
        invalid->style_origin = origin;
        d.check(arena, "invalid origin");
        d.expect(invalid, ":first-child", false); d.expect(invalid, ":only-child", false);
    }
    invalid->style_origin = parent;
    invalid->pseudo_element = aether_pseudo_element_t::none;
    d.check(arena, "generated node without pseudo kind");
    invalid->pseudo_element = aether_pseudo_element_t::after;
    invalid->type = AETHER_NODE_TEXT;
    d.check(arena, "generated text cannot normalize");
    Arena::children(wrapper, {c});
    d.check(arena, "layout-only parent boundary");
    d.expect(c, ":first-child", false); d.expect(c, ":only-child", false);

    // Long sibling lists exercise first/middle/last and missing-member paths.
    Arena many;
    auto* p = many.make();
    for (int i = 0; i < 257; ++i) {
        auto* n = many.make(i % 4 == 0 ? AETHER_NODE_TEXT : AETHER_NODE_ELEMENT);
        n->parent = p; p->children.push_back(n);
    }
    many.make()->parent = p;
    d.check(many, "many siblings");
}

void runtime_mutations() {
    using empty_test::Fixture;
    using empty_test::append;
    using empty_test::remove;
    for (const char* pseudo : {":first-child", ":only-child", ":not(:not(:first-child))",
                               ":is(:where(:only-child))"}) {
        const bool only = std::string(pseudo).find("only") != std::string::npos;
        Fixture f("<body><div id='p'><!--slot--><i id='a' class='item'></i></div>"
                  "<div id='q'><i id='b' class='item'></i>x</div></body>",
                  std::string(".item{opacity:.2}.item") + pseudo + "{opacity:.8}"
                  ".item:first-child::before{content:'FIRST';font-size:17px}"
                  ".item:only-child::after{content:'ONLY';font-size:19px}", 4);
        auto* p = f.get("p"); auto* q = f.get("q");
        auto* a = f.get("a"); auto* b = f.get("b");
        auto* text = aether_node_get_child(q, 1);
        require(aether_node_get_type(text) == AETHER_NODE_TEXT, "runtime text fixture");
        auto check = [&](bool first_a, bool only_a, bool first_b, bool only_b, const char* label) {
            std::vector<empty_test::DomSnapshot> before;
            std::unordered_set<aether_node_t> dom;
            Fixture::collect_dom(aether_doc_get_body(f.doc), dom);
            for (auto* node : dom) before.emplace_back(node);
            for (int pass = 0; pass < 3; ++pass) {
                f.resolve();
                f.expect(a, only ? only_a : first_a, label);
                f.expect(b, only ? only_b : first_b, label);
                for (auto* host : {a, b}) {
                    const bool first = host == a ? first_a : first_b;
                    const bool single = host == a ? only_a : only_b;
                    std::vector<std::string> expected;
                    if (first) expected.push_back("FIRST");
                    if (single) expected.push_back("ONLY");
                    f.expect_pseudos(host, expected, label);
                    record(f.opacity.at(host) > .5f);
                    for (const auto& pseudo : f.pseudos) {
                        if (pseudo.host != host) continue;
                        record(pseudo.content == "FIRST");
                        record(pseudo.content == "ONLY");
                        record(pseudo.font_size == 17.f);
                        record(pseudo.font_size == 19.f);
                    }
                    record(expected.empty());
                }
                for (const auto& original : before) original.verify(label);
            }
        };
        check(true, true, true, true, "initial singletons");
        for (int cycle = 0; cycle < 3; ++cycle) {
            append(p, b);
            check(true, false, false, false, "singleton to multiple");
            append(p, a);
            check(false, false, true, false, "reorder via append");
            remove(p, b); append(q, b);
            check(true, true, true, true, "remove and reparent");
            append(p, text);
            check(true, true, true, true, "text insertion");
            append(q, text);
            remove(p, a); append(p, a);
            check(true, true, true, true, "remove and reinsert with pseudo regeneration");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    require(argc == 1 || (argc == 2 && std::string(argv[1]) == "--digest"), "usage: test [--digest]");
    private_matrix();
    runtime_mutations();
    std::printf("selector-edge-children digest: %016llx\n", static_cast<unsigned long long>(digest));
    std::printf("PASS selector-edge-children: %d checks (active under NDEBUG)\n", empty_test::checks);
}
