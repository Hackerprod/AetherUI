#pragma once
#include <aether/aether.h>
#include "pseudo_element.h"
#include "node_identity.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <cstdlib>
#include <cstring>

struct aether_node_s {
    aether_node_type_t type;
    std::shared_ptr<node_identity_s> identity = std::make_shared<node_identity_s>();
    bool snapshot_owned = false;
    // Current DOM ownership of generated nodes, also retained by style revisions.
    std::vector<std::shared_ptr<aether_node_s>> generated_storage;
    aether_node_s* parent = nullptr;
    std::vector<aether_node_s*> children;
    std::string tag_name;
    std::map<std::string, std::string, std::less<>> attributes;
    int32_t state = 0;
    std::string text;
    int64_t style_node_id = 0;
    bool is_generated = false;
    aether_pseudo_element_t pseudo_element = aether_pseudo_element_t::none;
    // The originating element is kept separately from parent: generated nodes inherit from their
    // host, while selector combinators must navigate from the host's real DOM position.
    aether_node_s* style_origin = nullptr;
    // Layout-only anonymous flex wrappers resolve visibility/pointer-events
    // from this DOM owner. Unlike style_origin, this is never set for pseudos.
    aether_node_s* anonymous_flex_owner = nullptr;
    aether_node_s() { identity->owner = this; }
    virtual ~aether_node_s() { identity->alive = false; identity->attached = false; }
};

struct aether_doc_s : aether_node_s {
    int64_t version = 0;
    std::unordered_map<std::string, aether_node_s*> id_index;
    int64_t next_style_node_id = 1;

    aether_doc_s() { type = AETHER_NODE_DOCUMENT; }
};

inline uint64_t aether_node_identity(const aether_node_s* node) noexcept {
    return node ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(node->identity.get())) : 0;
}
inline aether_node_s* aether_dom_root(aether_node_s* node) noexcept {
    while (node && node->parent) node = node->parent;
    return node;
}
inline void aether_node_touch(aether_node_s* node, bool structure = false) noexcept {
    auto* root = aether_dom_root(node);
    if (!root) return;
    ++root->identity->mutation_epoch;
    if (structure) ++root->identity->structure_epoch;
}
inline void aether_node_mark_attached(aether_node_s* node, bool attached) noexcept {
    if (!node) return;
    node->identity->attached = attached;
    for (auto* child : node->children) aether_node_mark_attached(child, attached);
}
inline void aether_destroy_dom_subtree(aether_node_s* node) noexcept {
    if (!node) return;
    node->identity->attached = false;
    if (node->snapshot_owned) { aether_node_mark_attached(node, false); return; } // Host and revision share generated ownership.
    for (auto* child : node->children) aether_destroy_dom_subtree(child);
    delete node;
}
