#include "style_runtime_types.h"
#include "../dom/dom_types.h"
#include "../primitives/ascii_text.h"
#include "../primitives/html_space_tokens.h"

// Layout and paint share storage with generated content, but selector traversal
// sees only the DOM. Never turn a generated traversal candidate into its host.
static bool is_selector_dom_element(const aether_node_s* node) {
    return node && node->type == AETHER_NODE_ELEMENT && !node->is_generated &&
           !node->anonymous_flex_owner;
}

// Normalize only the styled subject at selector_match's entry. The cascade
// independently selects before/after rules; their selector prefix uses the
// host's current identity and position, not the generated node's copied data.
static aether_node_s* selector_subject(aether_node_s* node) {
    if (!node || node->anonymous_flex_owner) return nullptr;
    if (node->is_generated) {
        if (node->type != AETHER_NODE_ELEMENT ||
            (node->pseudo_element != aether_pseudo_element_t::before &&
             node->pseudo_element != aether_pseudo_element_t::after) ||
            node->style_origin == node)
            return nullptr;
        node = node->style_origin;
    }
    return is_selector_dom_element(node) ? node : nullptr;
}

// The document parent is retained for root/child-position semantics, but cannot
// itself match an element selector. Generated/layout-only parents are a boundary.
static aether_node_s* dom_parent(aether_node_s* node) {
    auto* parent = node ? node->parent : nullptr;
    return parent && !parent->is_generated && !parent->anonymous_flex_owner ? parent : nullptr;
}

/* ── Internal helpers for DOM navigation ──────────────────────────── */

static bool has_class(aether_node_s* node, const std::string& name) {
    auto it = node->attributes.find("class");
    if (it == node->attributes.end()) return false;
    return aether::html_space_contains(it->second, name);
}

// :empty considers direct DOM elements and nonempty text (including whitespace),
// not comments or generated content. Pseudos share children with DOM nodes, so
// skip them before testing their ELEMENT type, even during pseudo regeneration.
static bool is_empty_for_selector(aether_node_s* node) {
    if (!node) return true;
    for (const auto* child : node->children) {
        if (child->is_generated || child->anonymous_flex_owner) continue;
        if (is_selector_dom_element(child) ||
            (child->type == AETHER_NODE_TEXT && !child->text.empty()))
            return false;
    }
    return true;
}

static int dom_element_sibling_index(aether_node_s* node) {
    if (!is_selector_dom_element(node)) return -1;
    if (!node->parent) return 0; // Preserve existing disconnected-element behavior.
    auto* parent = dom_parent(node);
    if (!parent) return -1;
    int element_index = 0;
    for (auto* sibling : parent->children) {
        if (!is_selector_dom_element(sibling)) continue;
        if (sibling == node) return element_index;
        ++element_index;
    }
    return -1;
}

static int dom_element_child_count(aether_node_s* node) {
    if (!node) return 0;
    int count = 0;
    for (auto* child : node->children)
        if (is_selector_dom_element(child)) ++count;
    return count;
}

static bool is_last_dom_element_child(aether_node_s* node) {
    if (!node->parent) return true;
    auto* parent = dom_parent(node);
    if (!parent) return false;
    for (auto it = parent->children.rbegin(); it != parent->children.rend(); ++it) {
        if (!is_selector_dom_element(*it)) continue;
        return *it == node;
    }
    return false;
}

static aether_node_s* dom_previous_element_sibling(aether_node_s* node) {
    auto* parent = dom_parent(node);
    if (!parent) return nullptr;
    aether_node_s* prev = nullptr;
    for (auto* child : parent->children) {
        if (child == node) return prev;
        if (is_selector_dom_element(child)) prev = child;
    }
    return nullptr;
}

/* ── Forward declarations ─────────────────────────────────────────── */

static bool match_step(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                       aether_node_s* relative_anchor = nullptr);
static bool matches_compound(const compound_selector_s& compound, aether_node_s* node);
static bool matches_attribute(const attribute_selector_s& attr, aether_node_s* node);
static bool matches_pseudo(const pseudo_selector_s& pseudo, aether_node_s* node);
static bool match_ancestor(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                           aether_node_s* relative_anchor);
static bool match_previous_siblings(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                           aether_node_s* relative_anchor);
static bool has_matching_descendant(aether_node_s* node, const std::vector<compiled_selector_s>& selectors,
                                    aether_node_s* relative_anchor);

/* ── Attribute matching ───────────────────────────────────────────── */

static bool matches_attribute(const attribute_selector_s& attr, aether_node_s* node) {
    const auto it = node->attributes.find(attr.name);
    if (it == node->attributes.end()) return false;
    if (attr.op == ATTR_EXISTS) return true;
    const std::string_view actual = it->second;
    const std::string_view expected = attr.value;
    const bool insensitive = aether::attribute_value_is_ascii_insensitive(
        attr.case_mode, attr.html_default_ascii_insensitive);
    const auto equals = [insensitive](std::string_view a, std::string_view b) {
        return insensitive ? aether::ascii_iequals(a, b) : a == b;
    };
    // CSS substring operators reject empty operands. The shared primitives
    // deliberately retain ordinary empty-string comparison/search semantics.
    if (expected.empty() &&
        (attr.op == ATTR_PREFIX || attr.op == ATTR_SUFFIX || attr.op == ATTR_CONTAINS))
        return false;
    switch (attr.op) {
        case ATTR_EQUALS: return equals(actual, expected);
        case ATTR_INCLUDES: {
            if (expected.empty()) return false;
            for (const unsigned char byte : expected)
                if (aether::is_html_space(byte)) return false;
            aether::html_space_token_scanner scanner(actual);
            std::string_view token;
            while (scanner.next(token))
                if (equals(token, expected)) return true;
            return false;
        }
        case ATTR_DASH_MATCH: return equals(actual, expected) ||
            (actual.size() > expected.size() && actual[expected.size()] == '-' &&
             equals(actual.substr(0, expected.size()), expected));
        case ATTR_PREFIX: return insensitive ? aether::ascii_istarts_with(actual, expected) :
            actual.starts_with(expected);
        case ATTR_SUFFIX: return insensitive ? aether::ascii_iends_with(actual, expected) :
            actual.ends_with(expected);
        case ATTR_CONTAINS: return insensitive ? aether::ascii_icontains(actual, expected) :
            actual.find(expected) != std::string_view::npos;
        default: return false;
    }
}

/* ── Pseudo matching ──────────────────────────────────────────────── */

static bool matches_pseudo(const pseudo_selector_s& pseudo, aether_node_s* node) {
    switch (pseudo.kind) {
    case PSEUDO_REQUIRED_STATE:
        return (static_cast<uint32_t>(node->state) & static_cast<uint32_t>(pseudo.state)) ==
               static_cast<uint32_t>(pseudo.state);
    case PSEUDO_FORBIDDEN_STATE:
        return (static_cast<uint32_t>(node->state) & static_cast<uint32_t>(pseudo.state)) == 0;
    case PSEUDO_ROOT:
        // :root matches the document element — the node whose parent is the
        // Document node (the native DOM keeps the document as the html element's
        // parent, so a bare parent==nullptr check would never match).
        return node->parent == nullptr || node->parent->type == AETHER_NODE_DOCUMENT;
    case PSEUDO_EMPTY:
        return is_empty_for_selector(node);
    case PSEUDO_FIRST_CHILD:
        return dom_element_sibling_index(node) == 0;
    case PSEUDO_LAST_CHILD:
        return is_last_dom_element_child(node);
    case PSEUDO_ONLY_CHILD:
        return dom_parent(node) && dom_element_sibling_index(node) == 0 &&
               dom_element_child_count(dom_parent(node)) == 1;
    case PSEUDO_NTH_CHILD: {
        const int index = dom_element_sibling_index(node);
        return index >= 0 && pseudo.nth.matches(index + 1);
    }
    case PSEUDO_IS:
    case PSEUDO_WHERE:
        if (!pseudo.nested) return false;
        for (auto& sel : *pseudo.nested)
            if (match_step(sel, 0, node)) return true;
        return false;
    case PSEUDO_NOT:
        if (!pseudo.nested) return true;
        for (auto& sel : *pseudo.nested)
            if (match_step(sel, 0, node)) return false;
        return true;
    case PSEUDO_HAS:
        return pseudo.nested && has_matching_descendant(node, *pseudo.nested, node);
    default:
        return false;
    }
}

/* ── Compound matching ────────────────────────────────────────────── */

static bool matches_compound(const compound_selector_s& compound, aether_node_s* node) {
    if (!compound.type_name.empty()) {
        if (node->type != AETHER_NODE_ELEMENT) return false;
        if (!aether::ascii_iequals(std::string_view(node->tag_name), std::string_view(compound.type_name)))
            return false;
    }
    if (!compound.ids.empty()) {
        auto it = node->attributes.find("id");
        if (it == node->attributes.end()) return false;
        for (const auto& id : compound.ids)
            if (it->second != id) return false;
    }
    for (auto& cls : compound.classes)
        if (!has_class(node, cls)) return false;
    for (auto& attr : compound.attributes)
        if (!matches_attribute(attr, node)) return false;
    for (auto& pseudo : compound.pseudos)
        if (!matches_pseudo(pseudo, node)) return false;
    return true;
}

/* ── Step matching (recursive, right-to-left) ─────────────────────── */

static bool match_step(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                           aether_node_s* relative_anchor) {
    if (step_index < 0 || step_index >= static_cast<int>(sel.steps.size()))
        return false;
    if (!is_selector_dom_element(node))
        return false;
    auto& step = sel.steps[step_index];
    if (!matches_compound(step.compound, node))
        return false;
    if (step.relation_to_next == COMB_NONE) {
        if (!relative_anchor) return true;
        // The implicit descendant edge of :has() starts at the leftmost
        // compound's parent: the subject itself cannot satisfy this edge.
        for (auto* parent = dom_parent(node); parent; parent = dom_parent(parent))
            if (parent == relative_anchor) return true;
        return false;
    }
    int next = step_index + 1;
    switch (step.relation_to_next) {
    case COMB_CHILD:
        return match_step(sel, next, dom_parent(node), relative_anchor);
    case COMB_DESCENDANT:
        return match_ancestor(sel, next, dom_parent(node), relative_anchor);
    case COMB_ADJACENT_SIBLING: {
        auto* prev = dom_previous_element_sibling(node);
        return prev && match_step(sel, next, prev, relative_anchor);
    }
    case COMB_GENERAL_SIBLING:
        return match_previous_siblings(sel, next, node, relative_anchor);
    default:
        return false;
    }
}

/* ── Ancestor walk (descendant combinator) ────────────────────────── */

static bool match_ancestor(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                           aether_node_s* relative_anchor) {
    while (node) {
        if (match_step(sel, step_index, node, relative_anchor))
            return true;
        node = dom_parent(node);
    }
    return false;
}

/* ── Previous-sibling walk (general sibling combinator) ───────────── */

static bool match_previous_siblings(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                           aether_node_s* relative_anchor) {
    auto* parent = dom_parent(node);
    if (!parent) return false;
    bool matched = false;
    for (auto* sibling : parent->children) {
        if (sibling == node) return matched;
        if (!is_selector_dom_element(sibling)) continue;
        if (!matched && match_step(sel, step_index, sibling, relative_anchor)) matched = true;
    }
    return false;
}

/* ── :has() descendant search ─────────────────────────────────────── */

static bool has_matching_descendant(aether_node_s* node,
                                    const std::vector<compiled_selector_s>& selectors,
                                    aether_node_s* relative_anchor) {
    for (auto* child : node->children) {
        // Prune the whole generated subtree before matching or recursing. Its
        // text children need not carry is_generated, and stale pseudos can still
        // exist while an ancestor is being resolved earlier in the same pass.
        if (child->is_generated || child->anonymous_flex_owner) continue;
        for (auto& sel : selectors)
            if (match_step(sel, 0, child, relative_anchor)) return true;
        if (has_matching_descendant(child, selectors, relative_anchor)) return true;
    }
    return false;
}

/* ── Public API ───────────────────────────────────────────────────── */

bool selector_match(const compiled_selector_s& sel, aether_node_s* element) {
    element = selector_subject(element);
    if (!element || sel.steps.empty()) return false;
    return match_step(sel, 0, element);
}
