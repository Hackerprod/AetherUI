#include "state_invalidation.h"
#include "../dom/dom_types.h"
#include "../primitives/ascii_text.h"
#include "../primitives/html_space_tokens.h"
#include <algorithm>
#include <functional>
#include <map>
#include <unordered_set>

namespace aether::style::invalidation {
namespace {
constexpr size_t route_budget = 128;
StaticFilter filter(const compound_selector_s& compound) {
    return {compound.type_name, compound.ids, compound.classes};
}
bool forward(selector_combinator_t relation, Walk& out) {
    switch (relation) {
    case COMB_DESCENDANT: out = Walk::descendants; return true;
    case COMB_CHILD: out = Walk::children; return true;
    case COMB_ADJACENT_SIBLING: out = Walk::next_sibling; return true;
    case COMB_GENERAL_SIBLING: out = Walk::following_siblings; return true;
    default: return false;
    }
}
struct Work {
    const compiled_selector_s* selector;
    std::vector<Hop> continuation;
    bool negated = false;
    Fallback fallback = Fallback::none;
};
bool logical(pseudo_selector_kind_t kind) {
    return kind == PSEUDO_IS || kind == PSEUDO_WHERE || kind == PSEUDO_NOT || kind == PSEUDO_HAS;
}
bool fixed(pseudo_selector_kind_t kind) {
    return kind == PSEUDO_ROOT || kind == PSEUDO_EMPTY || kind == PSEUDO_FIRST_CHILD ||
        kind == PSEUDO_LAST_CHILD || kind == PSEUDO_ONLY_CHILD || kind == PSEUDO_NTH_CHILD;
}
bool dom_node(const aether_node_s* n) {
    return n && !n->is_generated && !n->anonymous_flex_owner;
}
bool element(const aether_node_s* n) {
    return dom_node(n) && n->type == AETHER_NODE_ELEMENT;
}
aether_node_s* parent(aether_node_s* n) {
    return n && dom_node(n->parent) ? n->parent : nullptr;
}
bool accepts(const StaticFilter& f, const aether_node_s* n) {
    if (!element(n)) return false;
    if (!f.tag.empty() && !aether::ascii_iequals(n->tag_name, f.tag)) return false;
    for (const auto& id : f.ids) {
        const auto found = n->attributes.find("id");
        if (found == n->attributes.end() || found->second != id) return false;
    }
    for (const auto& cls : f.classes) {
        const auto found = n->attributes.find("class");
        if (found == n->attributes.end() || !aether::html_space_contains(found->second, cls)) return false;
    }
    return true;
}
void descendants(aether_node_s* n, std::vector<aether_node_s*>& out) {
    std::vector<aether_node_s*> pending;
    if (dom_node(n)) pending.assign(n->children.begin(), n->children.end());
    while (!pending.empty()) {
        auto* child = pending.back(); pending.pop_back();
        if (!dom_node(child)) continue;
        if (element(child)) out.push_back(child);
        pending.insert(pending.end(), child->children.begin(), child->children.end());
    }
}
std::vector<aether_node_s*> walk(const Hop& hop, const std::vector<aether_node_s*>& from) {
    std::vector<aether_node_s*> out;
    for (auto* n : from) {
        switch (hop.walk) {
        case Walk::self: out.push_back(n); break;
        case Walk::children:
            for (auto* c : n->children) if (element(c)) out.push_back(c);
            break;
        case Walk::descendants: descendants(n, out); break;
        case Walk::ancestors:
            for (auto* p = parent(n); p; p = parent(p)) if (element(p)) out.push_back(p);
            break;
        case Walk::next_sibling:
        case Walk::following_siblings: {
            auto* p = parent(n);
            if (!p) break;
            bool seen = false;
            for (auto* sibling : p->children) {
                if (sibling == n) { seen = true; continue; }
                if (!seen || !element(sibling)) continue;
                out.push_back(sibling);
                if (hop.walk == Walk::next_sibling) break;
            }
            break;
        }
        }
    }
    std::unordered_set<aether_node_s*> seen;
    out.erase(std::remove_if(out.begin(), out.end(), [&](auto* n) {
        return !accepts(hop.filter, n) || !seen.insert(n).second;
    }), out.end());
    return out;
}
bool within(aether_node_s* root, aether_node_s* n) {
    for (; dom_node(n); n = parent(n)) if (n == root) return true;
    return false;
}
}

Program compile(int32_t rule_id, const compiled_selector_s& selector) {
    Program result;
    result.rule_id = rule_id;
    result.slot = selector.pseudo_element;
    std::vector<Work> work{{&selector, {}, false, Fallback::none}};
    while (!work.empty()) {
        auto item = std::move(work.back()); work.pop_back();
        const auto& steps = item.selector->steps;
        for (size_t i = 0; i < steps.size(); ++i) {
            auto reason = item.fallback;
            std::vector<Hop> route;
            if (reason == Fallback::none) {
                route.push_back({Walk::self, filter(steps[i].compound)});
                for (size_t j = i; j > 0; --j) {
                    Walk direction{};
                    if (!forward(steps[j - 1].relation_to_next, direction)) {
                        reason = Fallback::unsupported_ast; break;
                    }
                    route.push_back({direction, filter(steps[j - 1].compound)});
                    if (route.size() > route_budget) { reason = Fallback::route_budget; break; }
                }
                if (reason == Fallback::none) {
                    if (route.size() + item.continuation.size() > route_budget)
                        reason = Fallback::route_budget;
                    else route.insert(route.end(), item.continuation.begin(), item.continuation.end());
                }
            }
            if (reason != Fallback::none) route.clear();
            for (const auto& pseudo : steps[i].compound.pseudos) {
                if (pseudo.kind == PSEUDO_REQUIRED_STATE || pseudo.kind == PSEUDO_FORBIDDEN_STATE) {
                    const auto mask = static_cast<uint32_t>(pseudo.state);
                    result.state_mask |= mask;
                    result.occurrences.push_back({mask, pseudo.kind == PSEUDO_FORBIDDEN_STATE,
                        item.negated, reason == Fallback::none ? Coverage::proven_superset : Coverage::document_fallback,
                        reason, filter(steps[i].compound), route});
                } else if (!logical(pseudo.kind) && !fixed(pseudo.kind)) {
                    // Future AST kinds cannot silently claim coverage. This one
                    // occurrence alone conservatively depends on every state bit.
                    result.state_mask = UINT32_MAX;
                    result.occurrences.push_back({UINT32_MAX, false, item.negated,
                        Coverage::document_fallback, Fallback::unsupported_ast, {}, {}});
                }
                if (!pseudo.nested) continue;
                auto continuation = route;
                auto nested_reason = reason;
                if (!logical(pseudo.kind)) nested_reason = Fallback::unsupported_ast;
                if (pseudo.kind == PSEUDO_HAS && nested_reason == Fallback::none) {
                    // The authentic matcher searches descendant subjects of the
                    // anchor. Invert that necessary relation, ignoring its extra
                    // leftmost-compound anchor test (safe false positives).
                    continuation.insert(continuation.begin(), {Walk::ancestors, {}});
                }
                if (continuation.size() >= route_budget) nested_reason = Fallback::route_budget;
                if (nested_reason != Fallback::none) continuation.clear();
                for (const auto& nested : *pseudo.nested)
                    work.push_back({&nested, continuation,
                        item.negated != (pseudo.kind == PSEUDO_NOT), nested_reason});
            }
        }
    }
    return result;
}

bool source_applies(const StaticFilter& filter,aether_node_s* node) noexcept { return accepts(filter,node); }

std::vector<Candidate> plan(aether_node_s* root, const std::vector<Program>& programs,
                            const std::vector<StateChange>& changes) {
    std::vector<Candidate> result;
    if (!dom_node(root)) return result;
    // A subtree root cannot make storage under a generated/layout-only ancestor
    // part of the logical DOM merely by hiding that ancestor from traversal.
    for (auto* ancestor = root->parent; ancestor; ancestor = ancestor->parent)
        if (!dom_node(ancestor)) return result;
    using Key = std::pair<const node_identity_s*, aether_pseudo_element_t>;
    struct KeyLess {
        bool operator()(const Key& a, const Key& b) const {
            if (a.first != b.first) return std::less<const node_identity_s*>{}(a.first, b.first);
            return a.second < b.second;
        }
    };
    std::map<Key, size_t, KeyLess> index;
    std::vector<aether_node_s*> all;
    bool have_all = false;
    for (const auto& change : changes) {
        if (!element(change.node) || !within(root, change.node) || !change.changed_bits) continue;
        for (const auto& program : programs) {
            if (!(program.state_mask & change.changed_bits)) continue;
            for (const auto& occurrence : program.occurrences) {
                if (!(occurrence.state_mask & change.changed_bits) ||
                    !accepts(occurrence.source_filter, change.node)) continue;
                std::vector<aether_node_s*> candidates;
                const bool fallback = occurrence.coverage == Coverage::document_fallback;
                if (fallback) {
                    if (!have_all) {
                        if (element(root)) all.push_back(root);
                        descendants(root, all);
                        have_all = true;
                    }
                    candidates = all;
                } else {
                    candidates.push_back(change.node);
                    for (const auto& hop : occurrence.route) {
                        candidates = walk(hop, candidates);
                        if (candidates.empty()) break;
                    }
                }
                for (auto* host : candidates) {
                    if (!within(root, host)) continue;
                    const Key key{host->identity.get(), program.slot};
                    auto [it, inserted] = index.emplace(key, result.size());
                    if (inserted) result.push_back({host, host->identity, program.slot, {}, false});
                    auto& candidate = result[it->second];
                    candidate.used_fallback |= fallback;
                    if (std::find(candidate.rules.begin(), candidate.rules.end(), program.rule_id) == candidate.rules.end())
                        candidate.rules.push_back(program.rule_id);
                }
            }
        }
    }
    return result;
}
}
