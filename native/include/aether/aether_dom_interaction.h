#pragma once
#include <aether/aether_dom_sync.h>
#include <aether/aether_dom_style_binding.h>
#ifdef __cplusplus
extern "C" {
#endif
/* All operations are owner-thread-only except the release-only functions.
 * Native stores opaque registry identities, never managed delegates. The caller
 * pins each managed registry until delivery and exact acknowledgment finish. */
typedef struct aether_dom_interaction_owner_s* aether_dom_interaction_owner_t;
typedef struct aether_dom_interaction_claim_s* aether_dom_interaction_claim_t;
typedef struct aether_dom_interaction_edit_s* aether_dom_interaction_edit_t;
typedef struct aether_dom_interaction_claim_info_v1_s {
    aether_node_t node;
    uint64_t host_identity, binding_identity, owner_identity, generation;
    uint64_t node_identity, disconnect_serial, state_bit_write_serial, registry_identity;
    int32_t bit, current;
} aether_dom_interaction_claim_info_v1_t;
typedef struct aether_dom_interaction_loss_v1_s {
    aether_dom_interaction_claim_info_v1_t claim;
    uint64_t record_id, record_serial, disconnect_serial, state_bit_write_serial;
    int32_t reason, handler_present;
    aether_dom_string_v1_t handler;
} aether_dom_interaction_loss_v1_t;
enum { AETHER_DOM_LOSS_DISCONNECT=1, AETHER_DOM_LOSS_RETIRED=2,
       AETHER_DOM_LOSS_RETIRED_EXHAUSTED=3 };
typedef struct aether_dom_interaction_candidate_v1_s {
    aether_dom_interaction_claim_info_v1_t claim;
    int32_t before_state, after_state, applied, state_changed;
} aether_dom_interaction_candidate_v1_t;
/* Prepare has no document side effects. Validate it with the style binding
 * candidate before the common publication segment. After that binding commits
 * to this exact document, activate is allocation/callback-free and cannot fail
 * under those preconditions; expose neither candidate before activation. */
AETHER_API aether_status_t aether_dom_interaction_owner_prepare_v1(aether_doc_t document,
    uint64_t host_identity, aether_dom_interaction_owner_t* out_owner);
/* Restored State bits must already be true on the private candidate document.
 * The first claim pins one exact DOM token: finish ALL restoration first.
 * Handles are inactive until owner activation; preparation changes no live
 * node authority, registry, generation or State. Duplicate node/bit is rejected. */
AETHER_API aether_status_t aether_dom_interaction_owner_prepare_claim_v1(aether_dom_interaction_owner_t owner,
    aether_node_t node, int32_t bit, uint64_t registry_identity, aether_dom_interaction_claim_t* out_claim);
AETHER_API aether_status_t aether_dom_interaction_owner_validate_prepared_v1(aether_dom_interaction_owner_t owner);
AETHER_API aether_status_t aether_dom_interaction_owner_activate_v1(aether_dom_interaction_owner_t owner,
    aether_dom_style_binding_t binding);
AETHER_API aether_status_t aether_dom_interaction_owner_create_v1(aether_doc_t document,
    aether_dom_style_binding_t binding, uint64_t host_identity, aether_dom_interaction_owner_t* out_owner);
/* Dispose is logical, allocation-free, and discards pending notifications. It
 * does not clear CSS State. Call it before release when the host is disposed. */
AETHER_API aether_status_t aether_dom_interaction_owner_dispose_v1(aether_dom_interaction_owner_t owner);
AETHER_API void aether_dom_interaction_owner_release_v1(aether_dom_interaction_owner_t owner);
AETHER_API aether_status_t aether_dom_interaction_prepare_acquire_v1(aether_dom_interaction_owner_t owner,
    const aether_dom_token_v1_t* expected, aether_node_t node, int32_t bit, uint64_t registry_identity,
    aether_dom_interaction_edit_t* out_edit);
/* A pending loss requires its exact record pair; zero is only normal release.
 * Superseded authority/stamps/ownership produce applied=false, not a stale write. */
AETHER_API aether_status_t aether_dom_interaction_prepare_cleanup_v1(aether_dom_interaction_claim_t claim,
    uint64_t generation, uint64_t record_id, uint64_t record_serial, aether_dom_interaction_edit_t* out_edit);
AETHER_API aether_status_t aether_dom_interaction_edit_candidate_v1(aether_dom_interaction_edit_t edit,
    aether_dom_interaction_candidate_v1_t* out_candidate);
AETHER_API aether_status_t aether_dom_interaction_edit_validate_v1(aether_dom_interaction_edit_t edit);
/* Acquisition returns a preallocated claim handle. Cleanup returns null. The
 * exact output token certifies the native State mirrored by the candidate. */
AETHER_API aether_status_t aether_dom_interaction_edit_commit_v1(aether_dom_interaction_edit_t edit,
    aether_dom_interaction_claim_t* out_claim, aether_dom_token_v1_t* out_token);
AETHER_API void aether_dom_interaction_edit_release_v1(aether_dom_interaction_edit_t edit);
AETHER_API aether_status_t aether_dom_interaction_claim_read_v1(aether_dom_interaction_claim_t claim,
    aether_dom_interaction_claim_info_v1_t* out_claim);
/* No allocation/callback. A present handler borrow survives DOM mutations and
 * remains valid until claim acknowledgment/release or owner disposal. */
AETHER_API aether_status_t aether_dom_interaction_claim_read_loss_v1(aether_dom_interaction_claim_t claim,
    int32_t* out_present, aether_dom_interaction_loss_v1_t* out_loss);
AETHER_API aether_status_t aether_dom_interaction_claim_ack_v1(aether_dom_interaction_claim_t claim,
    uint64_t generation, uint64_t record_id, uint64_t record_serial);
/* Explicit normal retirement requires no pending loss, and never writes State. */
AETHER_API aether_status_t aether_dom_interaction_claim_dispose_v1(aether_dom_interaction_claim_t claim);
/* Release-only: safe for finalizers; does not acknowledge/retire a live claim. */
AETHER_API void aether_dom_interaction_claim_release_v1(aether_dom_interaction_claim_t claim);
#ifdef __cplusplus
}
#endif
