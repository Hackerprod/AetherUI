#include <aether/aether_dom_owner.h>
#include "dom_types.h"

struct aether_dom_owner_s {
    std::shared_ptr<EditableNodeStorage> root;
};

AETHER_API aether_status_t aether_dom_owner_create_v1(
    aether_dom_owner_t* out_owner, aether_doc_t* out_document) {
    if (out_owner) *out_owner = nullptr;
    if (out_document) *out_document = nullptr;
    if (!out_owner || !out_document) return AETHER_ERROR_NULL_ARG;
    try {
        // Allocate the external cell before creating/publishing a legacy root.
        // After doc_create succeeds, transfer and publication cannot allocate.
        auto owner = std::make_unique<aether_dom_owner_s>();
        aether_doc_t document = nullptr;
        const auto status = aether_doc_create(&document);
        if (status != AETHER_OK) return status;
        auto* legacy = document->legacy_lease;
        owner->root = std::move(legacy->storage);
        document->legacy_lease = nullptr;
        delete legacy;
        *out_document = document;
        *out_owner = owner.release();
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}

AETHER_API aether_status_t aether_dom_owner_retire_v1(aether_dom_owner_t owner) {
    if (!owner) return AETHER_ERROR_NULL_ARG;
    auto* document = static_cast<aether_doc_s*>(owner->root->node.get());
    if (document->owner_control->owner_thread != std::this_thread::get_id())
        return AETHER_ERROR_INVALID_STATE;
    if (document->owner_control->alive) aether_doc_destroy(document);
    return AETHER_OK;
}

AETHER_API void aether_dom_owner_release_v1(aether_dom_owner_t owner) {
    delete owner;
}
