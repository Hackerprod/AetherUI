#include "state_invalidation.h"
#include "../src/dom/dom_types.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace inv = aether::style::invalidation;
namespace {
size_t checks = 0, comparisons = 0, transitions = 0, changed_matches = 0;
void check(bool yes, const std::string& label) {
    ++checks;
    if (!yes) throw std::runtime_error(label);
}
struct Tree {
    aether_doc_s root;
    std::vector<std::unique_ptr<aether_node_s>> storage;
    std::vector<aether_node_s*> elements;
    aether_node_s* add(aether_node_s* parent, std::string cls = "", std::string tag = "div",
                       aether_node_type_t type = AETHER_NODE_ELEMENT) {
        auto owner = std::make_unique<aether_node_s>();
        auto* n = owner.get(); n->type = type; n->tag_name = std::move(tag);
        n->attributes["class"] = std::move(cls);
        n->attributes["id"] = "id" + std::to_string(storage.size());
        n->parent = parent; parent->children.push_back(n);
        if (type == AETHER_NODE_ELEMENT) elements.push_back(n);
        storage.push_back(std::move(owner)); return n;
    }
};
compiled_selector_s parse(const std::string& text) {
    compiled_selector_s ast;
    check(selector_parse(text.c_str(), ast), "parse: " + text);
    return ast;
}
bool contains(const std::vector<inv::Candidate>& candidates, aether_node_s* n,
              aether_pseudo_element_t slot, int32_t rule = 42) {
    for (const auto& candidate : candidates)
        if (candidate.host == n && candidate.slot == slot &&
            std::find(candidate.rules.begin(), candidate.rules.end(), rule) != candidate.rules.end()) return true;
    return false;
}
bool logical_node(aether_node_s* n) {
    for (; n; n=n->parent) if(n->is_generated || n->anonymous_flex_owner) return false;
    return true;
}
void differential(Tree& tree, const std::string& source, const std::vector<uint32_t>& masks) {
    auto ast = parse(source);
    // Prove compiler metadata derives from AST, not CSS text or legacy dep flags.
    ast.source = ".unrelated:not-a-state";
    ast.dependencies = SELECTOR_DEP_NONE;
    const auto program = inv::compile(42, ast);
    for (auto* changed : tree.elements) {
        if (!logical_node(changed)) continue;
        for (const auto mask : masks) for (int direction = 0; direction < 2; ++direction) {
            const int32_t saved = changed->state;
            changed->state = direction ? (saved | mask) : (saved & ~mask);
            std::vector<bool> before;
            for (auto* subject : tree.elements) before.push_back(selector_match(ast, subject));
            const auto candidates_before = inv::plan(&tree.root, {program}, {{changed, mask}});
            changed->state ^= mask;
            const auto candidates_after = inv::plan(&tree.root, {program}, {{changed, mask}});
            check(candidates_before.size() == candidates_after.size(), source + " state-independent count");
            for (const auto& candidate : candidates_before)
                check(contains(candidates_after, candidate.host, candidate.slot), source + " state-independent agenda");
            for (size_t i = 0; i < tree.elements.size(); ++i) {
                auto* subject = tree.elements[i];
                if (!logical_node(subject->is_generated ? subject->style_origin : subject)) continue;
                const bool after = selector_match(ast, subject);
                ++comparisons;
                if (before[i] == after) continue;
                ++changed_matches;
                auto* host = subject->is_generated ? subject->style_origin : subject;
                check(contains(candidates_after, host, ast.pseudo_element),
                    "FALSE NEGATIVE: " + source + " changed=" + changed->attributes["id"] +
                    " subject=" + subject->attributes["id"] + " mask=" + std::to_string(mask));
            }
            changed->state = saved; ++transitions;
        }
    }
}
void focused() {
    Tree tree;
    auto* panel = tree.add(&tree.root, "panel");
    auto* card = tree.add(panel, "card item", "article");
    auto* text = tree.add(panel, "", "", AETHER_NODE_TEXT); text->text = "space";
    tree.add(panel, "", "", AETHER_NODE_COMMENT);
    auto* next = tree.add(panel, "card label");
    auto* child = tree.add(card, "label item");
    auto* leaf = tree.add(child, "label leaf", "span");
    auto* far = tree.add(panel, "card");
    auto* far_leaf = tree.add(far, "label");
    auto agenda = [&](const char* source, aether_node_s* changed) {
        return inv::plan(&tree.root, {inv::compile(42, parse(source))}, {{changed, PSEUDO_STATE_HOVER}});
    };
    auto a = agenda(".card:hover", card);
    check(a.size() == 1 && contains(a, card, aether_pseudo_element_t::none), "self exact");
    check(agenda(".card:hover", child).empty(), "hover on child without card class stays local and empty");
    check(agenda(".card:hover:focus", card).size() == 1, "other dynamic state cannot filter changed occurrence");
    a = agenda(".card:hover > .label", card);
    check(a.size() == 1 && contains(a, child, aether_pseudo_element_t::none), "child exact");
    a = agenda(".card:hover .label", card);
    check(a.size() == 2 && contains(a, leaf, aether_pseudo_element_t::none), "descendant exact");
    a = agenda(".card:hover + .card", card);
    check(a.size() == 1 && contains(a, next, aether_pseudo_element_t::none), "adjacent skips text/comment");
    a = agenda(".card:hover ~ .card .label", card);
    check(a.size() == 1 && contains(a, far_leaf, aether_pseudo_element_t::none), "general sibling plus tail");
    auto* outside_label = tree.add(&tree.root, "label");
    a = agenda(".panel:hover .label", panel);
    check(a.size() == 4 && !contains(a, outside_label, aether_pseudo_element_t::none), "panel tail excludes unrelated subtree");
    a = agenda(".panel:has(.item:hover) .label", child);
    check(contains(a, far_leaf, aether_pseudo_element_t::none), "has inverse and outer tail");
    a = agenda(".card:not(.missing:hover)", card);
    check(a.empty(), "necessary occurrence-local missing filter is safe under not");
    a = agenda(".card:not(.missing):hover", card);
    check(a.size() == 1, "negated static branch not hoisted positive");
    a = agenda(".card:is(.missing,:hover)", card);
    check(a.size() == 1, "alternative branch filter not hoisted");
    auto ast = parse(".card:is(:hover,:focus):not(:disabled):enabled");
    auto program = inv::compile(42, ast);
    check(program.occurrences.size() == 4, "all list and forbidden occurrences");
    check(program.state_mask == (PSEUDO_STATE_HOVER | PSEUDO_STATE_FOCUS | PSEUDO_STATE_DISABLED), "state masks union");
    bool forbidden = false, negative = false;
    for (const auto& occurrence : program.occurrences) {
        forbidden |= occurrence.forbidden_state; negative |= occurrence.under_negation;
        check(occurrence.coverage == inv::Coverage::proven_superset, "known AST covered");
    }
    check(forbidden && negative, "explicit polarity metadata");
    a = inv::plan(&tree.root, {program}, {{card, PSEUDO_STATE_CHECKED}});
    check(a.empty(), "unrelated state skipped");
    a = inv::plan(&tree.root, {program}, {{card, 0}});
    check(a.empty(), "no transition skipped");

    auto* before = tree.add(card, "label card item");
    before->is_generated = true; before->pseudo_element = aether_pseudo_element_t::before; before->style_origin = card;
    auto* hidden = tree.add(before, "label card item");
    auto* anonymous = tree.add(card, "label"); anonymous->anonymous_flex_owner = card;
    tree.add(anonymous, "label");
    a = agenda(".card:hover .label", card);
    check(a.size() == 2, "generated and anonymous entire subtrees pruned");
    check(agenda(".label:hover", before).empty(), "generated changed input not normalized");
    check(agenda(".label:hover", hidden).empty(), "generated descendant changed input excluded");
    a = agenda(".card:hover::before", card);
    check(a.size() == 1 && contains(a, card, aether_pseudo_element_t::before), "host plus before slot");
    const auto p1 = inv::compile(1, parse(".card:hover::before"));
    const auto p2 = inv::compile(2, parse(".card:is(:hover,:hover)::before"));
    const auto p3 = inv::compile(3, parse(".card:hover::after"));
    a = inv::plan(&tree.root, {p1,p2,p3}, {{card,1},{card,1}});
    check(a.size() == 2, "dedup token plus slot across occurrences/rules/batch");
    for (const auto& c : a) {
        check(c.token.get() == card->identity.get(), "authentic retained token");
        check(c.rules.size() == (c.slot == aether_pseudo_element_t::before ? 2u : 1u), "all affected rules retained");
    }
    // Out-of-scope external node does not become a candidate.
    Tree external; auto* other = external.add(&external.root, "card");
    check(inv::plan(&tree.root, {p1}, {{other,1}}).empty(), "foreign root rejected");

    const std::vector<std::string> cases = {
        ".card:hover", ".card:not(:hover)", ".card:hover > .label", ".card:hover .label",
        ".card:hover + .card", ".card:hover ~ .card .label", ".panel:hover .card + .card .label",
        ".card:is(:hover,:focus)", ".card:where(:hover,:focus)", ".card:not(:hover,:focus)",
        ".card:not(:is(:hover,:not(:focus)))", ".card:is(.missing,:not(:hover))",
        ".panel:has(.item:hover) .label", ".panel:not(:has(.item:hover)) .label",
        ".panel:has(.item:not(:hover)) .label", ".panel:has(.card:hover > .label) .label",
        ".panel:has(.card:hover + .card) > .card", ".panel:has(.card:hover ~ .card .label) .label",
        ".panel:has(.card:has(.leaf:hover))", ".panel:has(.card:not(:has(.leaf:hover))) .label",
        ".card:is(.panel:hover .card)", ".card:not(.panel:hover > .card)",
        ".panel:has(.card:is(.panel:hover .card))", ".panel:has(.card:not(.panel:hover .card))",
        ".card:hover::before", ".panel:has(.item:hover) .label::after", ":enabled", ":disabled",
        ":checked", ":focus-within", ":read-only", ":read-write",
        ":selected", ":expanded", ":invalid", ":active",
        ".card:not(.missing):hover", ".card:not(.missing:hover)", ".card:is(.missing,:hover)",
        ".card:where(:unknown,:hover)", ".card:where(:unknown)", "[data-x=x]:hover", "article#id1:hover",
        ".card:hover:first-child", ".card:hover:nth-child(2n+1)", ".card:empty:hover"
    };
    const std::vector<uint32_t> masks = {PSEUDO_STATE_HOVER,PSEUDO_STATE_FOCUS,PSEUDO_STATE_ACTIVE,
        PSEUDO_STATE_DISABLED,PSEUDO_STATE_CHECKED,PSEUDO_STATE_FOCUS_WITHIN,PSEUDO_STATE_INVALID,
        PSEUDO_STATE_READ_ONLY,PSEUDO_STATE_SELECTED,PSEUDO_STATE_EXPANDED,
        PSEUDO_STATE_HOVER|PSEUDO_STATE_FOCUS};
    for (const auto& source : cases) differential(tree, source, masks);
}
void fallback() {
    Tree tree; auto* root = tree.add(&tree.root, "root"); auto* n = root;
    std::string source = ".root:hover";
    for (int i=0; i<130; ++i) { n=tree.add(n, "tail"); source += " > .tail"; }
    auto ast = parse(source);
    auto program = inv::compile(42, ast);
    check(program.occurrences.size() == 1 && program.occurrences[0].fallback == inv::Fallback::route_budget,
        "long route explicit fallback");
    auto unrelated = inv::compile(7, parse(".tail:focus"));
    auto a = inv::plan(&tree.root, {program,unrelated}, {{root,PSEUDO_STATE_HOVER}});
    check(a.size() == tree.elements.size() && contains(a,n,aether_pseudo_element_t::none), "fallback covers document");
    for (const auto& candidate : a) check(candidate.used_fallback && candidate.rules == std::vector<int32_t>{42}, "fallback stays rule scoped");
    check(inv::plan(&tree.root, {program}, {{root,PSEUDO_STATE_FOCUS}}).empty(), "fallback stays mask scoped");
    check(inv::plan(&tree.root, {program}, {{n,PSEUDO_STATE_HOVER}}).empty(), "fallback honors necessary occurrence filter");
    root->state = 0; check(!selector_match(ast,n), "deep before oracle");
    root->state = PSEUDO_STATE_HOVER; check(selector_match(ast,n), "deep after oracle");
    // Exercise unsupported AST seam without extending parser syntax.
    auto unknown = parse(".root:hover .tail");
    unknown.steps[0].relation_to_next = static_cast<selector_combinator_t>(255);
    program = inv::compile(42, unknown);
    check(program.occurrences[0].fallback == inv::Fallback::unsupported_ast, "unknown edge explicit fallback");
    unknown = parse(".tail:hover");
    unknown.steps[0].compound.pseudos[0].kind = static_cast<pseudo_selector_kind_t>(255);
    program = inv::compile(42, unknown);
    check(program.state_mask == UINT32_MAX && program.occurrences[0].coverage == inv::Coverage::document_fallback,
        "future pseudo cannot silently claim state independence");
    // Budget also propagates through nested logical ASTs, retaining occurrences.
    source=":hover"; for (int i=0;i<140;++i) source=":not("+source+")";
    program=inv::compile(42,parse(source));
    check(program.occurrences.size()==1 && program.occurrences[0].fallback==inv::Fallback::route_budget,
        "deep nested occurrence retained as fallback");
}
std::string random_selector(std::mt19937& rng, int depth) {
    static const char* compounds[]={"*",".a",".b","div","span","#id3","[data-x]",".a.b"};
    static const char* state[]={":hover",":focus",":enabled",":checked",":active",":disabled"};
    static const char* logic[]={":is(",":where(",":not(",":has("};
    static const char* comb[]={" "," > "," + "," ~ "};
    const int count=1+int(rng()%3);
    std::string out;
    for(int i=0;i<count;++i) {
        if(i) out+=comb[rng()%4];
        out+=compounds[rng()%8];
        if(depth>0 && rng()%3==0) {
            out+=logic[rng()%4]; out+=random_selector(rng,depth-1);
            if(rng()%3==0) out+=","+random_selector(rng,depth-1);
            out+=")";
        } else out+=state[rng()%6];
    }
    return out;
}
void fuzz() {
    std::mt19937 rng(0xC2A37u);
    Tree tree;
    for (int i=0;i<32;++i) {
        aether_node_s* parent = i==0 ? static_cast<aether_node_s*>(&tree.root) : tree.elements[rng()%tree.elements.size()];
        auto* n=tree.add(parent, rng()%2 ? "a b" : (rng()%2 ? "a" : "b"), rng()%2 ? "div" : "span");
        n->state=int32_t(rng()%64); if(rng()%2) n->attributes["data-x"]="x";
    }
    for(int i=0;i<180;++i) {
        auto source=random_selector(rng,2);
        if(i%9==0) source+="::before";
        differential(tree,source,{PSEUDO_STATE_HOVER,PSEUDO_STATE_FOCUS,PSEUDO_STATE_DISABLED,PSEUDO_STATE_CHECKED});
    }
}
}
int main() {
    try { focused(); fallback(); fuzz(); }
    catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
    std::printf("PASS checks=%zu transitions=%zu matcher comparisons=%zu changed matches covered=%zu seed=0xC2A37\n",
        checks,transitions,comparisons,changed_matches);
}
