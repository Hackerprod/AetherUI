#include "style_runtime_types.h"
#include "selector_match_context.h"
#include "../dom/dom_types.h"
#include "../primitives/ascii_text.h"
#include "../primitives/html_space_tokens.h"
#include <new>

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

struct sibling_scan_s {
    size_t position = 0;
    size_t visited = 0;
};

static sibling_scan_s scan_dom_position(aether_node_s* parent, aether_node_s* node) {
    sibling_scan_s result;
    size_t ordinal = 0;
    for (auto* sibling : parent->children) {
        ++result.visited; // At most children.size(), representable in size_t.
        if (!is_selector_dom_element(sibling)) continue;
        ordinal = aether::selector_position::next_ordinal(ordinal);
        if (sibling == node) { result.position = ordinal; break; }
    }
    return result;
}

static size_t dom_element_sibling_position(aether_node_s* node) {
    if (!is_selector_dom_element(node)) return 0;
    if (!node->parent) return 1; // Preserve disconnected-element behavior.
    auto* parent = dom_parent(node);
    return parent ? scan_dom_position(parent, node).position : 0;
}

size_t selector_match_context_s::position(aether_node_s* node) {
    if (!is_selector_dom_element(node)) return 0;
    if (!node->parent) return 1;
    auto* parent = dom_parent(node);
    if (!parent) return 0;
    constexpr size_t cheap_limit = 128;
    if (parent->children.size() <= cheap_limit)
        return scan_dom_position(parent, node).position;

    if (parents_) {
        const auto cached = parents_->find(parent);
        if (cached != parents_->end()) {
            const auto found = cached->second.positions.find(node);
            if (found != cached->second.positions.end()) return found->second;
            if (cached->second.complete) return 0;
            // A partial miss is not proof of nonmembership. Scan it normally.
        }
    }
    const auto scan = scan_dom_position(parent, node);
    // Keep map construction, exception cleanup and the temporary complete-map
    // frame off the cheap path. An absent subject never creates metadata.
    if (scan.position && scan.visited > cheap_limit)
        remember_position(parent, node, scan.position, scan.visited);
    return scan.position;
}

void selector_match_context_s::remember_position(aether_node_s* parent,
    aether_node_s* node, size_t position, size_t visited) {
    size_t budget = 0;
    if (!aether::selector_position::work_budget(parent->children.size(), budget)) return;
    if (!parents_) parents_.emplace();
    auto& entry = parents_->try_emplace(parent, budget).first->second;
    entry.positions.try_emplace(node, position);
    entry.remaining = visited >= entry.remaining ? 0 : entry.remaining - visited;
    entry.observed = std::max(entry.observed, position);
    constexpr size_t coverage_divisor = 8;
    const size_t required = entry.observed / coverage_divisor +
        (entry.observed % coverage_divisor != 0);
    if (entry.remaining || entry.positions.size() < required) return;

    positions_s complete;
    size_t ordinal = 0;
    // Do not reserve raw children: text/generated entries need no map storage.
    // try_emplace preserves the first occurrence even in duplicate-pointer tests.
    for (auto* sibling : parent->children) {
        if (!is_selector_dom_element(sibling)) continue;
        ordinal = aether::selector_position::next_ordinal(ordinal);
        complete.try_emplace(sibling, ordinal);
    }
    // OOM propagates to transactional preparation. Publish only a complete map.
    entry.positions = std::move(complete);
    entry.complete = true;
    return;
}

static bool is_first_dom_element_child(aether_node_s* node) {
    if (!is_selector_dom_element(node)) return false;
    if (!node->parent) return true; // Preserve existing disconnected-element behavior.
    auto* parent = dom_parent(node);
    if (!parent) return false;
    for (auto* child : parent->children) {
        if (!is_selector_dom_element(child)) continue;
        return child == node;
    }
    return false;
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

aether_node_s* selector_match_context_s::previous_sibling(aether_node_s* node) {
    if (!is_selector_dom_element(node)) return nullptr;
    auto* parent = dom_parent(node);
    if (!parent) return nullptr;
    constexpr size_t cheap_limit = 128;
    if (parent->children.size() <= cheap_limit)
        return dom_previous_element_sibling(node);

    if (previous_parents_) {
        const auto cached = previous_parents_->find(parent);
        if (cached != previous_parents_->end()) {
            const auto found = cached->second.previous.find(node);
            // A null predecessor is a valid hit for the first DOM element.
            if (found != cached->second.previous.end()) return found->second;
            if (cached->second.complete) return nullptr;
        }
    }
    aether_node_s* previous = nullptr;
    size_t position = 0, visited = 0;
    for (auto* sibling : parent->children) {
        ++visited; // At most children.size(), representable in size_t.
        if (!is_selector_dom_element(sibling)) continue;
        position = aether::selector_position::next_ordinal(position);
        if (sibling == node) {
            // Keep fallible map construction off cheap and nonmember paths.
            if (visited > cheap_limit)
                remember_previous(parent, node, previous, position, visited);
            return previous;
        }
        previous = sibling;
    }
    return nullptr;
}

void selector_match_context_s::remember_previous(aether_node_s* parent,
    aether_node_s* node, aether_node_s* previous, size_t position, size_t visited) {
    size_t budget = 0;
    if (!aether::selector_position::work_budget(parent->children.size(), budget)) return;
    if (!previous_parents_) previous_parents_.emplace();
    auto& entry = previous_parents_->try_emplace(parent, budget).first->second;
    entry.previous.try_emplace(node, previous);
    entry.remaining = visited >= entry.remaining ? 0 : entry.remaining - visited;
    entry.observed = std::max(entry.observed, position);
    constexpr size_t coverage_divisor = 8;
    const size_t required = entry.observed / coverage_divisor +
        (entry.observed % coverage_divisor != 0);
    if (entry.remaining || entry.previous.size() < required) return;

    previous_s complete;
    previous = nullptr;
    // No raw-child reserve: text/generated nodes require no index entries.
    // Preserve the first occurrence of duplicate pointers, but advance previous
    // for every occurrence exactly as the generic navigation loop does.
    for (auto* sibling : parent->children) {
        if (!is_selector_dom_element(sibling)) continue;
        complete.try_emplace(sibling, previous);
        previous = sibling;
    }
    // Any allocation failure propagates to prepare; never publish partial as full.
    entry.previous = std::move(complete);
    entry.complete = true;
}

/* ── Forward declarations ─────────────────────────────────────────── */

static bool match_step(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                       aether_node_s* relative_anchor, selector_match_context_s* context);
static bool matches_compound(const compound_selector_s& compound, aether_node_s* node,
                             selector_match_context_s* context);
static bool matches_attribute(const attribute_selector_s& attr, aether_node_s* node);
static bool matches_pseudo(const pseudo_selector_s& pseudo, aether_node_s* node,
                           selector_match_context_s* context);
static bool match_ancestor(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                           aether_node_s* relative_anchor, selector_match_context_s* context);
static bool match_previous_siblings(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                           aether_node_s* relative_anchor, selector_match_context_s* context);
static bool has_matching_descendant(aether_node_s* node, const std::vector<compiled_selector_s>& selectors,
                                    aether_node_s* relative_anchor, selector_match_context_s* context);

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

static bool matches_pseudo(const pseudo_selector_s& pseudo, aether_node_s* node,
                           selector_match_context_s* context) {
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
        return is_first_dom_element_child(node);
    case PSEUDO_LAST_CHILD:
        return is_last_dom_element_child(node);
    case PSEUDO_ONLY_CHILD:
        return dom_parent(node) && is_first_dom_element_child(node) &&
               is_last_dom_element_child(node);
    case PSEUDO_NTH_CHILD: {
        const size_t position = context ? context->position(node) : dom_element_sibling_position(node);
        return pseudo.nth.matches_position(position);
    }
    case PSEUDO_IS:
    case PSEUDO_WHERE:
        if (!pseudo.nested) return false;
        for (auto& sel : *pseudo.nested)
            if (match_step(sel, 0, node, nullptr, context)) return true;
        return false;
    case PSEUDO_NOT:
        if (!pseudo.nested) return true;
        for (auto& sel : *pseudo.nested)
            if (match_step(sel, 0, node, nullptr, context)) return false;
        return true;
    case PSEUDO_HAS:
        return pseudo.nested && has_matching_descendant(node, *pseudo.nested, node, context);
    default:
        return false;
    }
}

/* ── Compound matching ────────────────────────────────────────────── */

static bool matches_compound(const compound_selector_s& compound, aether_node_s* node,
                             selector_match_context_s* context) {
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
        if (!matches_pseudo(pseudo, node, context)) return false;
    return true;
}

/* ── Step matching (recursive, right-to-left) ─────────────────────── */

static bool match_step(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                           aether_node_s* relative_anchor, selector_match_context_s* context) {
    if (step_index < 0 || step_index >= static_cast<int>(sel.steps.size()))
        return false;
    if (!is_selector_dom_element(node))
        return false;
    auto& step = sel.steps[step_index];
    if (!matches_compound(step.compound, node, context))
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
        return match_step(sel, next, dom_parent(node), relative_anchor, context);
    case COMB_DESCENDANT:
        return match_ancestor(sel, next, dom_parent(node), relative_anchor, context);
    case COMB_ADJACENT_SIBLING: {
        auto* prev = context ? context->previous_sibling(node) :
                              dom_previous_element_sibling(node);
        return prev && match_step(sel, next, prev, relative_anchor, context);
    }
    case COMB_GENERAL_SIBLING:
        return match_previous_siblings(sel, next, node, relative_anchor, context);
    default:
        return false;
    }
}

/* ── Ancestor walk (descendant combinator) ────────────────────────── */

static bool match_ancestor(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                           aether_node_s* relative_anchor, selector_match_context_s* context) {
    while (node) {
        if (match_step(sel, step_index, node, relative_anchor, context))
            return true;
        node = dom_parent(node);
    }
    return false;
}

/* ── Previous-sibling walk (general sibling combinator) ───────────── */

static bool match_previous_siblings(const compiled_selector_s& sel, int step_index, aether_node_s* node,
                           aether_node_s* relative_anchor, selector_match_context_s* context) {
    auto* parent = dom_parent(node);
    if (!parent) return false;
    bool matched = false;
    for (auto* sibling : parent->children) {
        if (sibling == node) return matched;
        if (!is_selector_dom_element(sibling)) continue;
        if (!matched && match_step(sel, step_index, sibling, relative_anchor, context)) matched = true;
    }
    return false;
}

/* ── :has() descendant search ─────────────────────────────────────── */

static bool has_matching_descendant(aether_node_s* node,
                                    const std::vector<compiled_selector_s>& selectors,
                                    aether_node_s* relative_anchor, selector_match_context_s* context) {
    for (auto* child : node->children) {
        // Prune the whole generated subtree before matching or recursing. Its
        // text children need not carry is_generated, and stale pseudos can still
        // exist while an ancestor is being resolved earlier in the same pass.
        if (child->is_generated || child->anonymous_flex_owner) continue;
        for (auto& sel : selectors)
            if (match_step(sel, 0, child, relative_anchor, context)) return true;
        if (has_matching_descendant(child, selectors, relative_anchor, context)) return true;
    }
    return false;
}

/* ── Public API ───────────────────────────────────────────────────── */

bool selector_match(const compiled_selector_s& sel, aether_node_s* element) {
    element = selector_subject(element);
    if (!element || sel.steps.empty()) return false;
    return match_step(sel, 0, element, nullptr, nullptr);
}

// A complete inventory proves only absence. Reuse the exact ancestor boundary
// of match_ancestor, never the document's ID index or the subject's own ID.
void selector_match_context_s::collect_ancestor_ids(aether_node_s* subject) {
    constexpr size_t link_limit = 128;
    constexpr size_t id_limit = 64;
    constexpr size_t byte_limit = 4096;
    constexpr size_t capacity_limit = 8192;
    size_t links = 0, bytes = 0, string_capacity = 0;
    ancestor_state_ = ancestor_state_s::unavailable;
    for (auto* parent = dom_parent(subject); parent; parent = dom_parent(parent)) {
        if (links == link_limit) { ancestor_ids_.reset(); return; }
        ++links;
        if (!is_selector_dom_element(parent)) continue;
        const auto found = parent->attributes.find("id");
        if (found == parent->attributes.end() || found->second.empty()) continue;
        const auto& id = found->second;
        if ((ancestor_ids_ && ancestor_ids_->size() == id_limit) ||
            id.size() > byte_limit - bytes) { ancestor_ids_.reset(); return; }
        // Only container/string scratch allocation is recoverable here. The
        // original matcher and ordinary DOM reads are outside this catch.
        try {
            if (!ancestor_ids_) ancestor_ids_.emplace();
            auto& ids = *ancestor_ids_;
            if (ids.size() == ids.capacity()) {
                const size_t capacity = ids.capacity();
                ids.reserve(capacity ? std::min(id_limit, capacity * 2) : 1);
                if (ids.capacity() > id_limit) { ancestor_ids_.reset(); return; }
            }
            ids.emplace_back(id);
        } catch (const std::bad_alloc&) {
            ancestor_ids_.reset();
            return;
        }
        const auto capacity = ancestor_ids_->back().capacity();
        if (capacity >= capacity_limit - string_capacity) {
            ancestor_ids_.reset();
            return;
        }
        string_capacity += capacity + 1;
        bytes += id.size();
    }
    ancestor_state_ = ancestor_state_s::complete;
}

static bool admits_ancestor_id_filter(const compiled_selector_s& sel,
                                     aether_node_s* original) {
    if (!is_selector_dom_element(original) ||
        sel.pseudo_element != aether_pseudo_element_t::none ||
        sel.steps.size() != 2 || sel.steps[0].relation_to_next != COMB_DESCENDANT ||
        sel.steps[1].relation_to_next != COMB_NONE ||
        !sel.steps[0].compound.pseudos.empty()) return false;
    const auto& left = sel.steps[1].compound;
    return left.ids.size() == 1 && !left.ids.front().empty() && left.type_name.empty() &&
        left.classes.empty() && left.attributes.empty() && left.pseudos.empty();
}

bool selector_match(const compiled_selector_s& sel, aether_node_s* element,
                    selector_match_context_s& context) {
    // Admission precedes normalization: generated/anonymous subjects always
    // take the unchanged entry path. An admitted RHS is evaluated only once.
    if (context.ancestor_ids_enabled_ && admits_ancestor_id_filter(sel, element)) {
        if (!matches_compound(sel.steps[0].compound, element, &context)) return false;
        if (context.ancestor_subject_ != element) {
            context.ancestor_ids_.reset();
            context.ancestor_subject_ = element;
            context.ancestor_state_ = selector_match_context_s::ancestor_state_s::unseen;
        }
        if (context.ancestor_state_ == selector_match_context_s::ancestor_state_s::complete) {
            const auto& required = sel.steps[1].compound.ids.front();
            if (!context.ancestor_ids_ ||
                std::find(context.ancestor_ids_->begin(), context.ancestor_ids_->end(), required) ==
                    context.ancestor_ids_->end()) return false;
        }
        const bool matched = match_ancestor(sel, 1, dom_parent(element), nullptr, &context);
        if (!matched && context.ancestor_state_ == selector_match_context_s::ancestor_state_s::unseen)
            context.collect_ancestor_ids(element);
        // The first negative is already proved. Even cap/OOM cannot cause a
        // second continuation; unseen positives never build or copy anything.
        return matched;
    }
    element = selector_subject(element);
    if (!element || sel.steps.empty()) return false;
    return match_step(sel, 0, element, nullptr, &context);
}
