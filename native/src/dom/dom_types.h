#pragma once
#include <aether/aether.h>
#include "pseudo_element.h"
#include "node_identity.h"
#include "mutation_journal.h"
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
    aether::dom::MutationJournal mutation_journal;
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
// Keep the original epoch boundary/counts. The optional subject distinguishes a
// moved/removed child from the parent whose root receives structural invalidation.
inline void aether_node_touch_typed(aether_node_s* node, aether::dom::MutationKind kind,
                                   bool structure = false, int32_t old_state = 0,
                                   int32_t new_state = 0, aether_node_s* subject = nullptr) noexcept {
    auto* root = aether_dom_root(node);
    if (!root) return;
    ++root->identity->mutation_epoch;
    if (structure) ++root->identity->structure_epoch;
    if (root->type == AETHER_NODE_DOCUMENT) {
        // A type tag alone is not proof of document storage (internal standalone
        // roots may use the document tag). Such roots remain without coverage.
        if (auto* document = dynamic_cast<aether_doc_s*>(root)) {
            document->mutation_journal.append(
                (subject ? subject : node)->identity, kind, root->identity->mutation_epoch,
                root->identity->structure_epoch, old_state, new_state);
        }
    }
    // Detached/non-document roots retain their existing epochs but have no
    // journal coverage. Reattachment is always a structural/full invalidation.
}
inline void aether_node_touch(aether_node_s* node, bool structure = false) noexcept {
    // Legacy/internal writes without a declared kind must never look state-only.
    aether_node_touch_typed(node, aether::dom::MutationKind::unknown, structure);
}
inline const aether::dom::MutationJournal* aether_node_mutation_journal(aether_node_s* node) noexcept {
    auto* root = aether_dom_root(node);
    if (!root || root->type != AETHER_NODE_DOCUMENT) return nullptr;
    auto* document = dynamic_cast<aether_doc_s*>(root);
    return document ? &document->mutation_journal : nullptr;
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
