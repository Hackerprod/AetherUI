#include "dom_types.h"
#include "../abi/aether_abi.h"

static void rebuild_id_index(aether_doc_s* doc) {
    doc->id_index.clear();
    std::function<void(aether_node_s*)> walk = [&](aether_node_s* node) {
        for (auto* child : node->children) {
            if (!child->is_generated && child->type == AETHER_NODE_ELEMENT) {
                auto it = child->attributes.find("id");
                if (it != child->attributes.end() && !it->second.empty()) {
                    doc->id_index[it->second] = child;
                }
            }
            walk(child);
        }
    };
    walk(doc);
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
    *out_doc = new aether_doc_s();
    return AETHER_OK;
}

AETHER_API void aether_doc_destroy(aether_doc_t doc) {
    if (!doc) return;
    std::function<void(aether_node_s*)> destroy = [&](aether_node_s* node) {
        for (auto* child : node->children) destroy(child);
        delete node;
    };
    destroy(doc);
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
    rebuild_id_index(doc);
    auto it = doc->id_index.find(id);
    return (it != doc->id_index.end()) ? it->second : nullptr;
}
