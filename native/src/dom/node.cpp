#include "dom_types.h"
#include "../abi/aether_abi.h"

namespace {
// Layout wrappers contain borrowed aliases, and generated text may carry no
// marker of its own. Neither is part of the editable, reciprocal DOM forest.
// Handles must be live and the initial DOM coherent; this is not a lifetime or
// pre-existing-corruption check.
bool is_synthetic_endpoint(const aether_node_s* node) {
    for (auto* ancestor = node; ancestor; ancestor = ancestor->parent) {
        if (ancestor->is_generated || ancestor->anonymous_flex_owner) return true;
    }
    return false;
}
}

AETHER_API aether_node_type_t aether_node_get_type(aether_node_t node) {
    return node ? node->type : AETHER_NODE_DOCUMENT;
}

AETHER_API aether_node_t aether_node_get_parent(aether_node_t node) {
    return node ? node->parent : nullptr;
}

AETHER_API int32_t aether_node_get_child_count(aether_node_t node) {
    if (!node) return 0;
    return static_cast<int32_t>(std::count_if(node->children.begin(), node->children.end(),
        [](const aether_node_s* child) { return !child->is_generated; }));
}

AETHER_API aether_node_t aether_node_get_child(aether_node_t node, int32_t index) {
    if (!node || index < 0) return nullptr;
    for (auto* child : node->children) {
        if (child->is_generated) continue;
        if (index-- == 0) return child;
    }
    return nullptr;
}

AETHER_API char* aether_node_get_text_content(aether_node_t node) {
    if (!node) return nullptr;
    return aether::strdup(node->text.c_str());
}

AETHER_API int64_t aether_node_get_style_node_id(aether_node_t node) {
    return node ? node->style_node_id : 0;
}

AETHER_API aether_status_t aether_node_append_child(aether_node_t parent, aether_node_t child) {
    if (!parent || !child) return AETHER_ERROR_NULL_ARG;
    if (is_synthetic_endpoint(parent) || is_synthetic_endpoint(child))
        return AETHER_ERROR_INVALID_STATE;
    // Validate before detaching: moving an ancestor below its descendant (or
    // itself) would otherwise disconnect and corrupt an owned subtree.
    for (auto* ancestor = parent; ancestor; ancestor = ancestor->parent) {
        if (ancestor == child) return AETHER_ERROR_INVALID_STATE;
    }
    parent->children.reserve(parent->children.size() + 1);
    aether_node_touch(parent, true);
    if (child->parent) {
        aether_node_touch(child->parent, true);
        auto& siblings = child->parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    }
    child->parent = parent;
    parent->children.push_back(child);
    aether_node_mark_attached(child, true);
    return AETHER_OK;
}

AETHER_API aether_status_t aether_node_remove_child(aether_node_t parent, aether_node_t child) {
    if (!parent || !child) return AETHER_ERROR_NULL_ARG;
    if (is_synthetic_endpoint(parent) || is_synthetic_endpoint(child))
        return AETHER_ERROR_INVALID_STATE;
    auto& vec = parent->children;
    auto it = std::find(vec.begin(), vec.end(), child);
    if (it == vec.end() || child->parent != parent) return AETHER_ERROR_INVALID_STATE;
    aether_node_touch(parent, true);
    vec.erase(it);
    aether_node_mark_attached(child, false);
    child->parent = nullptr;
    return AETHER_OK;
}
