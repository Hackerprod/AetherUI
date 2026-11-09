#pragma once
#include <aether/aether.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct aether_dom_style_binding_s* aether_dom_style_binding_t;
/* One binding per document; independent native runtimes remain unrestricted.
 * Initial source is read directly from the native DOM, not caller markup.
 * Binding/slot publication does not deliver diagnostics or resolve styles. */
AETHER_API aether_status_t aether_dom_style_binding_create_v1(aether_doc_t document,
    aether_style_runtime_t runtime, aether_dom_style_binding_t* out_binding);
/* Logical unbind immediately revokes retained stages. The native sheet remains
 * installed: keep its handle and explicitly remove it before a replacement if
 * the runtime survives. Destruction never compiles, resolves, or calls user code.
 * These operations, including destruction, require the original owner thread. */
AETHER_API void aether_dom_style_binding_destroy_v1(aether_dom_style_binding_t binding);
AETHER_API aether_status_t aether_dom_style_binding_read_slot_v1(
    aether_dom_style_binding_t binding, int64_t* out_handle, int32_t* out_present,
    uint64_t* out_sheet_epoch);
typedef struct aether_dom_style_rebind_s* aether_dom_style_rebind_t;
/* Prepare replacement against both native documents and the existing runtime.
 * No mutation or callback occurs until commit. The returned binding handle is
 * preallocated. Commit invalidates the old handle's authority, not its storage;
 * destroy both old and new handles through the regular destroy API. */
AETHER_API aether_status_t aether_dom_style_binding_prepare_rebind_v1(
    aether_dom_style_binding_t old_binding, aether_doc_t new_document,
    aether_dom_style_rebind_t* out_transaction);
AETHER_API aether_status_t aether_dom_style_binding_validate_rebind_v1(aether_dom_style_rebind_t transaction);
AETHER_API aether_status_t aether_dom_style_binding_commit_rebind_v1(
    aether_dom_style_rebind_t transaction, aether_dom_style_binding_t* out_binding);
AETHER_API void aether_dom_style_binding_rebind_destroy_v1(aether_dom_style_rebind_t transaction);
/* Callback user_data remains borrowed. Replace/clear the runtime callback before
 * freeing it. Deferred source batches are cancelled on recipient replacement. */
#ifdef __cplusplus
}
#endif
