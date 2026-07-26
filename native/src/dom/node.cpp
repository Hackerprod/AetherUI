#include "dom_types.h"
#include "../abi/aether_abi.h"

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

AETHER_API aether_status_t aether_node_append_child(aether_node_t parent, aether_node_t child) {
    if (!parent || !child) return AETHER_ERROR_NULL_ARG;
    if (child->parent) {
        auto& siblings = child->parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    }
    child->parent = parent;
    parent->children.push_back(child);
    return AETHER_OK;
}

AETHER_API aether_status_t aether_node_remove_child(aether_node_t parent, aether_node_t child) {
    if (!parent || !child) return AETHER_ERROR_NULL_ARG;
    auto& vec = parent->children;
    auto it = std::find(vec.begin(), vec.end(), child);
    if (it == vec.end()) return AETHER_ERROR_INVALID_STATE;
    vec.erase(it);
    child->parent = nullptr;
    return AETHER_OK;
}
