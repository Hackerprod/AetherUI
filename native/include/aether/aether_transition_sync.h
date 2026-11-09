#pragma once
#include <aether/aether.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Version 1. Tokens are pure certificates, not storage leases. Runtime handles
 * remain borrowed and must not be used after destroy. Candidates retain an
 * authentic lifetime control and may safely outlive runtime destruction. */
typedef struct aether_transition_candidate_s* aether_transition_candidate_t;
typedef struct aether_transition_token_s {
    uint64_t instance_id;
    uint64_t mutation_serial;
    uint64_t status_revision;
    aether_status_t tick_error;
} aether_transition_token_t;
typedef struct aether_transition_observation_s {
    aether_node_t node;
    const aether_computed_style_t* before;
    const aether_computed_style_t* after;
} aether_transition_observation_t;
AETHER_API aether_status_t aether_transition_runtime_read_token(
    aether_transition_runtime_t runtime, aether_transition_token_t* out_token);
/* All observations share now_ms. Inputs are borrowed only for this call. Empty
 * batches share state without copying entries. Retire removes both sets. */
AETHER_API aether_status_t aether_transition_runtime_prepare(
    aether_transition_runtime_t runtime,
    const aether_transition_observation_t* observations, size_t observation_count,
    const aether_node_t* retire_nodes, size_t retire_count, int64_t now_ms,
    aether_transition_candidate_t* out_candidate);
/* Validate checks lifetime, owner thread, both serials and live Tick status.
 * Commit rechecks, swaps without allocation/callback, and retains old storage
 * in the candidate until destroy. A committed candidate cannot be reused. */
AETHER_API aether_status_t aether_transition_candidate_validate(aether_transition_candidate_t candidate);
AETHER_API aether_status_t aether_transition_candidate_commit(
    aether_transition_candidate_t candidate, aether_transition_token_t* out_token);
AETHER_API void aether_transition_candidate_destroy(aether_transition_candidate_t candidate);
/* Complete explicit sampling is the only operation acknowledging TickError.
 * out_active is initialized false on failure; callers must check status. */
AETHER_API aether_status_t aether_transition_runtime_try_tick(
    aether_transition_runtime_t runtime, int64_t now_ms, aether_bool_t* out_active);
#ifdef __cplusplus
}
#endif
