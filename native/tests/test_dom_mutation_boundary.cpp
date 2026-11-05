// Private provenance/reciprocity coverage complements the public layout fixture.
// Stack nodes deliberately have no recursive ownership: even a regression that
// corrupts an edge can be reported and destroyed without traversing that edge.
#include "../src/dom/dom_types.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const std::string& label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
        ++failures;
    }
}

void link(aether_node_s& parent, aether_node_s& child) {
    parent.children.push_back(&child);
    child.parent = &parent;
}

struct Fixture {
    aether_doc_s document;
    aether_node_s origin, subject, leaf, target, authored;
    Fixture() {
        document.version = 71;
        int64_t id = 1;
        for (auto* node : nodes()) {
            if (node != &document) node->type = AETHER_NODE_ELEMENT;
            node->tag_name = "arbitrary-authored-name";
            node->text = "unchanged content";
            node->attributes["id"] = "node-" + std::to_string(id);
            node->style_node_id = id++;
            document.id_index[node->attributes["id"]] = node;
        }
        link(document, origin);
        link(origin, subject);
        link(subject, leaf);
        link(document, target);
        link(target, authored);
    }
    std::vector<aether_node_s*> nodes() {
        return {&document, &origin, &subject, &leaf, &target, &authored};
    }
};

bool same_node(const aether_node_s& a, const aether_node_s& b) {
    return a.type == b.type && a.parent == b.parent && a.children == b.children &&
        a.tag_name == b.tag_name && a.attributes == b.attributes &&
        a.state == b.state && a.text == b.text && a.style_node_id == b.style_node_id &&
        a.is_generated == b.is_generated && a.pseudo_element == b.pseudo_element &&
        a.style_origin == b.style_origin && a.anonymous_flex_owner == b.anonymous_flex_owner;
}

struct Snapshot {
    std::vector<aether_node_s> nodes;
    int64_t version, next_id;
    std::unordered_map<std::string, aether_node_s*> index;
    explicit Snapshot(Fixture& f) : version(f.document.version),
        next_id(f.document.next_style_node_id), index(f.document.id_index) {
        for (auto* node : f.nodes()) nodes.push_back(*node);
    }
    bool unchanged(Fixture& f) const {
        const auto current = f.nodes();
        for (size_t i = 0; i < current.size(); ++i)
            if (!same_node(nodes[i], *current[i])) return false;
        return version == f.document.version && next_id == f.document.next_style_node_id &&
            index == f.document.id_index;
    }
};

using Mutation = aether_status_t (*)(aether_node_t, aether_node_t);
void rejected(Fixture& f, Mutation operation, aether_node_t parent,
              aether_node_t child, const std::string& label) {
    const Snapshot before(f);
    const auto status = operation(parent, child); // Never hidden inside assert/NDEBUG.
    check(status == AETHER_ERROR_INVALID_STATE, label + ": status");
    check(before.unchanged(f), label + ": complete snapshot unchanged");
}

void provenance_cases() {
    // Both flags, with arbitrary tags, and unmarked descendants of each flag.
    for (int flag = 0; flag < 2; ++flag) {
        for (bool inherited : {false, true}) {
            for (bool append : {false, true}) {
                for (bool parent_endpoint : {false, true}) {
                    Fixture f;
                    auto& marked = inherited ? f.origin : f.subject;
                    if (flag == 0) marked.is_generated = true;
                    else marked.anonymous_flex_owner = &f.document;
                    check(!inherited || (!f.subject.is_generated &&
                        f.subject.anonymous_flex_owner == nullptr), "unmarked descendant precondition");
                    const std::string label = std::string(flag == 0 ? "generated" : "anonymous") +
                        (inherited ? " ancestor" : " endpoint") +
                        (append ? " append" : " remove") +
                        (parent_endpoint ? " parent" : " child");
                    auto operation = append ? aether_node_append_child : aether_node_remove_child;
                    aether_node_t parent = parent_endpoint ? &f.subject :
                        (append ? &f.target : &f.origin);
                    aether_node_t child = parent_endpoint ?
                        (append ? &f.authored : &f.leaf) : &f.subject;
                    rejected(f, operation, parent, child, label);
                    rejected(f, operation, parent, child, label + " repeated");
                }
            }
        }
    }
}

void nonreciprocal_remove() {
    Fixture f;
    // Authored nodes only: reaches the reciprocity guard rather than provenance.
    f.target.children.push_back(&f.subject);
    check(f.subject.parent == &f.origin, "nonreciprocal precondition");
    rejected(f, aether_node_remove_child, &f.target, &f.subject, "nonreciprocal remove");
    rejected(f, aether_node_remove_child, &f.target, &f.subject, "nonreciprocal remove repeated");
}

void positive_cases() {
    for (bool style_only : {false, true}) {
        Fixture f;
        // Neither the wrapper-like name nor style_origin alone implies synthetic.
        f.subject.tag_name = "aether-anonymous-flex-item";
        if (style_only) {
            f.subject.style_origin = &f.origin;
            f.target.style_origin = &f.origin;
            f.authored.style_origin = &f.origin;
        }
        check(aether_node_append_child(&f.subject, &f.authored) == AETHER_OK,
            "authored wrapper-like parent append");
        check(f.authored.parent == &f.subject && f.target.children.empty() &&
            f.subject.children == std::vector<aether_node_s*>{&f.leaf, &f.authored},
            "authored parent append topology");
        check(aether_node_remove_child(&f.subject, &f.authored) == AETHER_OK,
            "authored wrapper-like parent remove");
        check(f.authored.parent == nullptr &&
            f.subject.children == std::vector<aether_node_s*>{&f.leaf},
            "authored parent remove topology");
        check(aether_node_append_child(&f.target, &f.subject) == AETHER_OK,
            "authored wrapper-like child append");
        check(f.subject.parent == &f.target && f.origin.children.empty() &&
            f.target.children == std::vector<aether_node_s*>{&f.subject},
            "authored child append topology");
        check(aether_node_remove_child(&f.target, &f.subject) == AETHER_OK,
            "authored wrapper-like child remove");
        check(f.subject.parent == nullptr && f.target.children.empty() &&
            f.leaf.parent == &f.subject &&
            f.subject.children == std::vector<aether_node_s*>{&f.leaf},
            "authored child remove preserves subtree");
    }
}
} // namespace

int main() {
    provenance_cases();
    nonreciprocal_remove();
    positive_cases();
    if (failures) {
        std::fprintf(stderr, "%d DOM mutation boundary checks failed\n", failures);
        return 1;
    }
    std::puts("DOM mutation boundary checks passed");
    return 0;
}
