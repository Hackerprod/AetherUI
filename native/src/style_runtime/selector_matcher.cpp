#include "style_runtime_types.h"
#include "../dom/dom_types.h"
#include <cctype>

static aether_node_s* selector_subject(aether_node_s* node) {
    return node && node->is_generated && node->style_origin ? node->style_origin : node;
}

static aether_node_s* selector_parent(aether_node_s* node) {
    node = selector_subject(node);
    return node ? node->parent : nullptr;
}

/* ── Internal helpers for DOM navigation ──────────────────────────── */

static bool has_class(aether_node_s* node, const std::string& name) {
    node = selector_subject(node);
    auto it = node->attributes.find("class");
    if (it == node->attributes.end()) return false;
    const auto& classes = it->second;
    size_t pos = 0;
    while (pos < classes.size()) {
        while (pos < classes.size() && std::isspace(static_cast<unsigned char>(classes[pos]))) ++pos;
        size_t end = pos;
        while (end < classes.size() && !std::isspace(static_cast<unsigned char>(classes[end]))) ++end;
        if (end > pos && classes.compare(pos, end - pos, name) == 0) return true;
        pos = end;
    }
    return false;
}

static const std::string empty_string;

static const std::string& get_attribute(aether_node_s* node, const std::string& name) {
    node = selector_subject(node);
    auto it = node->attributes.find(name);
    return (it != node->attributes.end()) ? it->second : empty_string;
}

static bool is_element(aether_node_s* node) {
    return node && node->type == AETHER_NODE_ELEMENT;
}

// :empty is deliberately based on every DOM child: whitespace text is content
// for that pseudo-class. Structural child pseudos and sibling combinators use
// the element-only helpers below instead.
static int dom_child_count(aether_node_s* node) {
    node = selector_subject(node);
    return node ? static_cast<int>(node->children.size()) : 0;
}

static int element_sibling_index(aether_node_s* node) {
    node = selector_subject(node);
    auto* parent = node ? node->parent : nullptr;
    if (!parent) return 0;
    int element_index = 0;
    for (auto* sibling : parent->children) {
        if (!is_element(sibling)) continue;
        if (sibling == node) return element_index;
        ++element_index;
    }
    return -1;
}

static int element_child_count(aether_node_s* node) {
    node = selector_subject(node);
    if (!node) return 0;
    int count = 0;
    for (auto* child : node->children)
        if (is_element(child)) ++count;
    return count;
}

static bool is_last_element_child(aether_node_s* node) {
    node = selector_subject(node);
    auto* parent = node ? node->parent : nullptr;
    if (!parent) return true;
    for (auto it = parent->children.rbegin(); it != parent->children.rend(); ++it) {
        if (!is_element(*it)) continue;
        return *it == node;
    }
    return false;
}

static aether_node_s* previous_element_sibling(aether_node_s* node) {
    node = selector_subject(node);
    auto* parent = node ? node->parent : nullptr;
    if (!parent) return nullptr;
    aether_node_s* prev = nullptr;
    for (auto* child : parent->children) {
        if (child == node) return prev;
        if (is_element(child)) prev = child;
    }
    return nullptr;
}

/* ── Forward declarations ─────────────────────────────────────────── */

static bool match_step(const compiled_selector_s& sel, int step_index, aether_node_s* node);
static bool matches_compound(const compound_selector_s& compound, aether_node_s* node);
static bool matches_attribute(const attribute_selector_s& attr, aether_node_s* node);
static bool matches_pseudo(const pseudo_selector_s& pseudo, aether_node_s* node);
static bool match_ancestor(const compiled_selector_s& sel, int step_index, aether_node_s* node);
static bool match_previous_siblings(const compiled_selector_s& sel, int step_index, aether_node_s* node);
static bool has_matching_descendant(aether_node_s* node, const std::vector<compiled_selector_s>& selectors);

/* ── Attribute matching ───────────────────────────────────────────── */

static bool ci_equals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

static bool ci_starts_with(std::string_view hay, std::string_view needle) {
    if (needle.size() > hay.size()) return false;
    return ci_equals(hay.substr(0, needle.size()), needle);
}

static bool ci_ends_with(std::string_view hay, std::string_view needle) {
    if (needle.size() > hay.size()) return false;
    return ci_equals(hay.substr(hay.size() - needle.size()), needle);
}

static bool ci_contains(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i <= hay.size() - needle.size(); ++i)
        if (ci_equals(hay.substr(i, needle.size()), needle)) return true;
    return false;
}

static bool matches_attribute(const attribute_selector_s& attr, aether_node_s* node) {
    node = selector_subject(node);
    auto it = node->attributes.find(attr.name);
    if (it == node->attributes.end()) return false;
    if (attr.op == ATTR_EXISTS) return true;
    const auto& actual = it->second;
    const auto& expected = attr.value;
    // Selectors 6.2: empty substring operands never match; other operators retain their semantics.
    if (expected.empty() &&
        (attr.op == ATTR_PREFIX || attr.op == ATTR_SUFFIX || attr.op == ATTR_CONTAINS))
        return false;
    if (attr.case_insensitive) {
        switch (attr.op) {
            case ATTR_EQUALS:    return ci_equals(actual, expected);
            case ATTR_INCLUDES:  {
                /* split actual on whitespace, check if any token equals expected */
                size_t pos = 0;
                while (pos < actual.size()) {
                    while (pos < actual.size() && std::isspace(static_cast<unsigned char>(actual[pos]))) ++pos;
                    size_t end = pos;
                    while (end < actual.size() && !std::isspace(static_cast<unsigned char>(actual[end]))) ++end;
                    if (end > pos && ci_equals(actual.substr(pos, end - pos), expected)) return true;
                    pos = end;
                }
                return false;
            }
            case ATTR_DASH_MATCH: return ci_equals(actual, expected) ||
                (actual.size() > expected.size() &&
                 ci_equals(actual.substr(0, expected.size()), expected) &&
                 actual[expected.size()] == '-');
            case ATTR_PREFIX:  return ci_starts_with(actual, expected);
            case ATTR_SUFFIX:  return ci_ends_with(actual, expected);
            case ATTR_CONTAINS: return ci_contains(actual, expected);
            default: return false;
        }
    }
    switch (attr.op) {
        case ATTR_EQUALS:    return actual == expected;
        case ATTR_INCLUDES:  {
            size_t pos = 0;
            while (pos < actual.size()) {
                while (pos < actual.size() && std::isspace(static_cast<unsigned char>(actual[pos]))) ++pos;
                size_t end = pos;
                while (end < actual.size() && !std::isspace(static_cast<unsigned char>(actual[end]))) ++end;
                if (end > pos && actual.compare(pos, end - pos, expected) == 0) return true;
                pos = end;
            }
            return false;
        }
        case ATTR_DASH_MATCH: return actual == expected ||
            (actual.size() > expected.size() &&
             actual.compare(0, expected.size(), expected) == 0 &&
             actual[expected.size()] == '-');
        case ATTR_PREFIX:  return actual.compare(0, expected.size(), expected) == 0;
        case ATTR_SUFFIX:  return actual.size() >= expected.size() &&
            actual.compare(actual.size() - expected.size(), expected.size(), expected) == 0;
        case ATTR_CONTAINS: return actual.find(expected) != std::string::npos;
        default: return false;
    }
}

/* ── Pseudo matching ──────────────────────────────────────────────── */

static bool matches_pseudo(const pseudo_selector_s& pseudo, aether_node_s* node) {
    node = selector_subject(node);
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
        return dom_child_count(node) == 0;
    case PSEUDO_FIRST_CHILD:
        return element_sibling_index(node) == 0;
    case PSEUDO_LAST_CHILD:
        return is_last_element_child(node);
    case PSEUDO_ONLY_CHILD:
        return node->parent != nullptr && element_child_count(node->parent) == 1;
    case PSEUDO_NTH_CHILD:
        return pseudo.nth.matches(element_sibling_index(node) + 1);
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
        return pseudo.nested && has_matching_descendant(node, *pseudo.nested);
    default:
        return false;
    }
}

/* ── Compound matching ────────────────────────────────────────────── */

static bool matches_compound(const compound_selector_s& compound, aether_node_s* node) {
    node = selector_subject(node);
    if (!compound.type_name.empty()) {
        if (node->type != AETHER_NODE_ELEMENT) return false;
        if (!ci_equals(std::string_view(node->tag_name), std::string_view(compound.type_name)))
            return false;
    }
    if (!compound.id.empty()) {
        auto it = node->attributes.find("id");
        if (it == node->attributes.end() || it->second != compound.id) return false;
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

static bool match_step(const compiled_selector_s& sel, int step_index, aether_node_s* node) {
    if (step_index < 0 || step_index >= static_cast<int>(sel.steps.size()))
        return false;
    if (!is_element(selector_subject(node)))
        return false;
    auto& step = sel.steps[step_index];
    if (!matches_compound(step.compound, node))
        return false;
    if (step.relation_to_next == COMB_NONE)
        return true;
    int next = step_index + 1;
    switch (step.relation_to_next) {
    case COMB_CHILD:
        return selector_parent(node) && match_step(sel, next, selector_parent(node));
    case COMB_DESCENDANT:
        return match_ancestor(sel, next, selector_parent(node));
    case COMB_ADJACENT_SIBLING: {
        auto* prev = previous_element_sibling(node);
        return prev && match_step(sel, next, prev);
    }
    case COMB_GENERAL_SIBLING:
        return match_previous_siblings(sel, next, node);
    default:
        return false;
    }
}

/* ── Ancestor walk (descendant combinator) ────────────────────────── */

static bool match_ancestor(const compiled_selector_s& sel, int step_index, aether_node_s* node) {
    while (node) {
        if (match_step(sel, step_index, node))
            return true;
        node = node->parent;
    }
    return false;
}

/* ── Previous-sibling walk (general sibling combinator) ───────────── */

static bool match_previous_siblings(const compiled_selector_s& sel, int step_index, aether_node_s* node) {
    node = selector_subject(node);
    auto* parent = node ? node->parent : nullptr;
    if (!parent) return false;
    for (auto* sibling : parent->children) {
        if (sibling == node) return false;
        if (!is_element(sibling)) continue;
        if (match_step(sel, step_index, sibling)) return true;
    }
    return false;
}

/* ── :has() descendant search ─────────────────────────────────────── */

static bool has_matching_descendant(aether_node_s* node,
                                    const std::vector<compiled_selector_s>& selectors) {
    for (auto* child : node->children) {
        for (auto& sel : selectors)
            if (match_step(sel, 0, child)) return true;
        if (has_matching_descendant(child, selectors)) return true;
    }
    return false;
}

/* ── Public API ───────────────────────────────────────────────────── */

bool selector_match(const compiled_selector_s& sel, aether_node_s* element) {
    if (!element || sel.steps.empty()) return false;
    return match_step(sel, 0, element);
}
