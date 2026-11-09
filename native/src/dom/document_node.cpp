#include "dom_types.h"
#include "../abi/aether_abi.h"

static void rebuild_id_index(aether_doc_s* doc) {
    decltype(doc->id_index) candidate;
    std::function<void(aether_node_s*)> walk = [&](aether_node_s* node) {
        for (auto* child : node->children) {
            if (!child->is_generated && child->type == AETHER_NODE_ELEMENT) {
                auto it = child->attributes.find("id");
                if (it != child->attributes.end() && !it->second.empty()) {
                    // Preorder visits the first connected match before duplicates.
                    candidate.try_emplace(it->second, child);
                }
            }
            walk(child);
        }
    };
    walk(doc);
    doc->id_index.swap(candidate);
}

static aether_node_s* find_body(aether_node_s* node) {
    if (node->is_generated) return nullptr;
    if (node->type == AETHER_NODE_ELEMENT && node->tag_name == "body") return node;
    for (auto* child : node->children) {
        if (auto* found = find_body(child)) return found;
    }
    return nullptr;
}

AETHER_API aether_status_t aether_doc_create(aether_doc_t* out_doc) {
    if (!out_doc) return AETHER_ERROR_NULL_ARG;
    *out_doc = nullptr;
    try {
        auto value=std::make_unique<aether_doc_s>();
        auto* doc=value.get();
        auto storage=std::make_shared<EditableNodeStorage>(std::move(value));
        auto lease=std::make_unique<LegacyDocumentLease>(); lease->storage=storage;
        doc->editable_storage=storage; doc->identity->storage=storage;
        doc->owner_control->document=storage;
        doc->legacy_lease=lease.release(); *out_doc=doc; return AETHER_OK;
    } catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch(...) { return AETHER_ERROR_INVALID_STATE; }
}

AETHER_API void aether_doc_destroy(aether_doc_t doc) {
    if (!doc) return;
    if (doc->owner_control->owner_thread != std::this_thread::get_id()) return;
    auto root_pin=aether_editable_lease(doc);
    doc->owner_control->alive=false; aether_dom_retire_node(doc);
    for (const auto& [node, storage] : doc->editable_nodes) aether_dom_retire_node(node);
    doc->editable_nodes.clear();
    auto* lease=doc->legacy_lease; doc->legacy_lease=nullptr;
    if (lease) delete lease;
    else if (!root_pin) aether_destroy_dom_subtree(doc);
}

AETHER_API int64_t aether_doc_get_version(aether_doc_t doc) {
    return doc ? doc->version : 0;
}

AETHER_API aether_node_t aether_doc_get_body(aether_doc_t doc) {
    return doc ? find_body(doc) : nullptr;
}

AETHER_API aether_node_t aether_doc_get_document_element(aether_doc_t doc) {
    if (!doc) return nullptr;
    for (auto* child : doc->children) {
        if (!child->is_generated && child->type == AETHER_NODE_ELEMENT && child->tag_name == "html") return child;
    }
    for (auto* child : doc->children) if (!child->is_generated) return child;
    return nullptr;
}

AETHER_API aether_node_t aether_doc_get_element_by_id(aether_doc_t doc, const char* id) {
    if (!doc || !id) return nullptr;
    if (aether_dom_access(doc) != AETHER_OK) return nullptr;
    try {
        rebuild_id_index(doc);
        auto it = doc->id_index.find(id);
        return (it != doc->id_index.end()) ? it->second : nullptr;
    } catch(...) { return nullptr; }
}
