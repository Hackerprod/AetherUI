#pragma once
#include <aether/aether.h>
#include "pseudo_element.h"
#include "node_identity.h"
#include "dom_interaction.h"
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
#include <thread>
#include <limits>
#include <stdexcept>

struct EditableNodeStorage;
struct LegacyDocumentLease;
struct DomStyleBinding;
struct DomOwnerControl {
    AETHER_DOM_COUNT_INSTANCE(owner_control)
    const std::thread::id owner_thread = std::this_thread::get_id();
    std::weak_ptr<EditableNodeStorage> document;
    uint64_t observation_serial = 0;
    aether_status_t sticky_status = AETHER_OK;
    bool alive = true;
    std::weak_ptr<DomStyleBinding> binding;
    std::weak_ptr<DomInteractionRegistry> interaction;
    uint64_t interaction_serial = 0, next_interaction_generation = 1;
    // Internal allocation-free hook, not a user callback. Installed only by
    // the interaction registry so standalone DOM tests need no new link unit.
    void (*interaction_retire)(aether_node_s*,bool) noexcept = nullptr;
};

struct aether_node_s {
    AETHER_DOM_COUNT_INSTANCE(node)
    aether_node_type_t type;
    std::shared_ptr<node_identity_s> identity = std::make_shared<node_identity_s>();
    std::weak_ptr<EditableNodeStorage> editable_storage;
    std::weak_ptr<DomOwnerControl> document_owner;
    // Authored value is separate from the legacy effective attribute map.
    bool authored_value_present = false;
    std::string authored_value;
    bool runtime_value_present = false;
    std::string runtime_value;
    bool unpublished = false; // Prepared typed nodes are not live mutation targets.
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

    std::shared_ptr<DomOwnerControl> owner_control = std::make_shared<DomOwnerControl>();
    std::unordered_map<aether_node_s*, std::shared_ptr<EditableNodeStorage>> editable_nodes;
    // Borrowed reference to the external legacy root lease. Never owned by root.
    LegacyDocumentLease* legacy_lease = nullptr;
    bool owns_storage_root = false;
    ~aether_doc_s() override;
    aether_doc_s() { type = AETHER_NODE_DOCUMENT; document_owner = owner_control; identity->document_owner = owner_control; }
};

// Storage owns exactly one editable object; links never own children/parents.
struct EditableNodeStorage {
    AETHER_DOM_COUNT_INSTANCE(storage)
    std::unique_ptr<aether_node_s> node;
    explicit EditableNodeStorage(std::unique_ptr<aether_node_s> value) noexcept : node(std::move(value)) {}
};
// External consumers own these relays; DOM identities only hold weak refs.
// current_root is accessed/mutated on the owner thread, never by finalizers.
struct DomNodeOwnerPin { std::shared_ptr<EditableNodeStorage> current_root; AETHER_DOM_COUNT_INSTANCE(owner_relay) };
struct LegacyDocumentLease { std::shared_ptr<EditableNodeStorage> storage; AETHER_DOM_COUNT_INSTANCE(legacy_cell) };
inline std::shared_ptr<EditableNodeStorage> aether_editable_lease(const aether_node_s* node) noexcept {
    return node ? node->editable_storage.lock() : nullptr;
}
inline std::shared_ptr<EditableNodeStorage> aether_identity_lease(const std::shared_ptr<node_identity_s>& identity) noexcept {
    return identity ? identity->storage.lock() : nullptr;
}
// The caller already protects the incoming node. Generated nodes inherit the
// ownership domain of their host, but have their own weak relay identity.
inline std::shared_ptr<DomNodeOwnerPin> aether_live_owner_pin(const aether_node_s* node) {
    if (!node) return {};
    if (auto existing=node->identity->live_owner_pin.lock()) return existing;
    auto* host=node;
    auto owner=host->document_owner.lock();
    while (!owner && host->parent) { host=host->parent;owner=host->document_owner.lock(); }
    if (!owner && node->style_origin) owner=node->style_origin->document_owner.lock();
    if (!owner) return {}; // Unsupported raw legacy borrower, no storage promise.
    if (owner->owner_thread!=std::this_thread::get_id()) throw std::invalid_argument("DOM relay owner thread");
    auto root=owner->document.lock();
    if (!root) throw std::invalid_argument("DOM relay expired root");
    auto result=std::make_shared<DomNodeOwnerPin>();result->current_root=std::move(root);
    node->identity->live_owner_pin=result;
    return result;
}
inline aether_status_t aether_dom_access(const aether_node_s* node) noexcept {
    if (!node) return AETHER_ERROR_NULL_ARG;
    if (!node->identity->alive) return AETHER_ERROR_DISPOSED;
    if (node->unpublished) return AETHER_ERROR_INVALID_STATE;
    const auto owner = node->document_owner.lock();
    if (owner && (!owner->alive || owner->owner_thread != std::this_thread::get_id())) return AETHER_ERROR_INVALID_STATE;
    return owner ? owner->sticky_status : AETHER_OK;
}
// Caller reserves every required advance before the first write.
inline bool aether_dom_can_touch(const aether_node_s* node, uint64_t count=1, bool structure=false) noexcept {
    if (aether_dom_access(node) != AETHER_OK) return false;
    const auto owner=node->document_owner.lock();
    if (owner && owner->observation_serial > UINT64_MAX-count) return false;
    auto* root=node; while(root && root->parent) root=root->parent;
    return root && root->identity->mutation_epoch <= UINT64_MAX-count &&
        (!structure || root->identity->structure_epoch <= UINT64_MAX-count);
}
inline std::shared_ptr<EditableNodeStorage> aether_make_editable_node(aether_doc_s* doc, aether_node_type_t type) {
    if (doc->next_style_node_id == INT64_MAX) throw std::length_error("DOM identity exhausted");
    auto node=std::make_unique<aether_node_s>(); node->type=type;
    auto result=std::make_shared<EditableNodeStorage>(std::move(node));
    auto* value=result->node.get();
    value->editable_storage=result; value->identity->storage=result;
    value->document_owner=doc->owner_control; value->identity->document_owner=doc->owner_control;
    value->style_node_id=doc->next_style_node_id;
    doc->editable_nodes.emplace(value,result); ++doc->next_style_node_id;
    return result;
}
inline void aether_dom_retire_node(aether_node_s* node) noexcept {
    if(node->identity->alive) {
        const bool exhausted=node->identity->disconnect_serial==UINT64_MAX;
        if(!exhausted)++node->identity->disconnect_serial;
        if(auto owner=node->document_owner.lock()) {
            if(exhausted)owner->sticky_status=AETHER_ERROR_INVALID_STATE;
            if(owner->interaction_retire)owner->interaction_retire(node,exhausted);
        }
    }
    node->identity->alive=false; node->identity->attached=false;
    for (auto* child : node->children) if (child->snapshot_owned || !aether_editable_lease(child)) aether_dom_retire_node(child);
}

struct EditableDomPin {
    std::vector<std::shared_ptr<DomNodeOwnerPin>> live_owners;
    std::vector<std::shared_ptr<EditableNodeStorage>> editable;
    std::vector<std::shared_ptr<aether_node_s>> generated;
};
inline void aether_pin_dom_subtree(aether_node_s* node, EditableDomPin& pin) {
    if(auto relay=aether_live_owner_pin(node))pin.live_owners.push_back(std::move(relay));
    if(auto lease=aether_editable_lease(node)) pin.editable.push_back(std::move(lease));
    for(const auto& storage:node->generated_storage) pin.generated.push_back(storage);
    for(auto* child:node->children) aether_pin_dom_subtree(child,pin);
}
inline std::shared_ptr<const EditableDomPin> aether_pin_dom_snapshot(aether_node_s* node) {
    auto pin=std::make_shared<EditableDomPin>(); if(node)aether_pin_dom_subtree(node,*pin); return pin;
}

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
    if (!aether_dom_can_touch(node, 1, structure)) {
        if (aether_dom_access(node)==AETHER_OK) if (auto owner=node->document_owner.lock()) owner->sticky_status=AETHER_ERROR_INVALID_STATE;
        return;
    }
    if (auto owner=node->document_owner.lock()) ++owner->observation_serial;
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
    if (aether_editable_lease(node)) { aether_dom_retire_node(node); return; }
    if (node->snapshot_owned) { aether_node_mark_attached(node, false); return; } // Host and revision share generated ownership.
    for (auto* child : node->children) aether_destroy_dom_subtree(child);
    delete node;
}

// Compatibility for internal/legacy manually-created nodes. Such nodes have no
// retained-storage promise; ordinary typed edits/snapshots never certify them.
// Call while every registry node is still alive, before releasing the registry.
inline void aether_dom_dispose_unmanaged_children(aether_node_s* node) noexcept {
    auto& children=node->children;
    for(auto it=children.begin();it!=children.end();) {
        auto* child=*it;
        if(!aether_editable_lease(child) && !child->snapshot_owned) {
            aether_destroy_dom_subtree(child);it=children.erase(it);
        } else ++it;
    }
}

inline aether_doc_s::~aether_doc_s() {
    // Last physical root release can run on a finalizer thread. Every legitimate
    // live reader holds a root/relay, so none remain here. Explicit retirement
    // has already cleaned raw links; do not traverse those a second time.
    if (!owns_storage_root || !owner_control->alive) return;
    owner_control->alive=false;
    aether_dom_retire_node(this);
    for (const auto& [node,storage]:editable_nodes) aether_dom_retire_node(node);
    aether_dom_dispose_unmanaged_children(this);
    for (const auto& [node,storage]:editable_nodes) aether_dom_dispose_unmanaged_children(node);
    editable_nodes.clear();
}
