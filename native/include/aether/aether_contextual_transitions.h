#pragma once
#include <aether/aether.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct aether_contextual_transition_runtime_s* aether_contextual_transition_runtime_t;
typedef struct aether_contextual_transition_batch_s* aether_contextual_transition_batch_t;
/* IDs are monotonically increasing in event order per runtime. The caller retains
 * the durable command and its original timestamp until a frame commits. */
typedef struct {
    uint64_t event_id;
    int64_t change_timestamp_ms;
    aether_node_t node;
    aether_computed_style_handle_t before;
    aether_computed_style_handle_t after;
} aether_contextual_transition_event;
typedef struct {
    aether_transform_matrix matrix; /* world-space origin, applied exactly once */
    float opacity;
    aether_bool_t has_transform;
    aether_bool_t active;
    int64_t presentation_timestamp_ms;
} aether_contextual_transition_sample;
AETHER_API uint32_t aether_contextual_transition_capability(void);
AETHER_API uint32_t aether_contextual_transition_version(void);
AETHER_API aether_bool_t aether_contextual_transition_batch_has_active(aether_contextual_transition_batch_t batch);
AETHER_API aether_status_t aether_contextual_transition_create(aether_contextual_transition_runtime_t* out);
AETHER_API void aether_contextual_transition_destroy(aether_contextual_transition_runtime_t runtime);
/* Transactional: errors leave runtime untouched and set *out to null. Batch owns endpoint
 * copies, never borrowed handles. Stable order is (timestamp, event_id). */
AETHER_API aether_status_t aether_contextual_transition_prepare(aether_contextual_transition_runtime_t runtime,
    const aether_contextual_transition_event* events, size_t count, int64_t presentation_ms,
    aether_contextual_transition_batch_t* out);
AETHER_API void aether_contextual_transition_batch_destroy(aether_contextual_transition_batch_t batch);
/* Allocation-free publication; stale candidates are rejected. Committed batches
 * remain immutable samples and can outlive the runtime. */
AETHER_API aether_status_t aether_contextual_transition_commit(aether_contextual_transition_runtime_t runtime,
    aether_contextual_transition_batch_t batch);
AETHER_API int32_t aether_contextual_transition_invalidation(aether_contextual_transition_batch_t batch);
AETHER_API aether_bool_t aether_contextual_transition_active_transform(aether_contextual_transition_batch_t batch, aether_node_t node);
/* current supplies the current, independently computed origin. Without a channel
 * this resolves the static style. No legacy/flat reconstruction is accepted. */
AETHER_API aether_status_t aether_contextual_transition_sample_box(aether_contextual_transition_batch_t batch,
    aether_computed_style_handle_t current, aether_rectf box, aether_contextual_transition_sample* out);
AETHER_API aether_status_t aether_contextual_transition_prepare_layout(aether_layout_engine_t engine,
    aether_contextual_transition_batch_t batch);
/* Mutates only the caller-owned unpublished layout candidate; all samples are
 * staged before box mutation. On failure the candidate is untouched. */
AETHER_API aether_status_t aether_contextual_transition_finalize_layout(aether_layout_result_t layout,
    aether_contextual_transition_batch_t batch);
#ifdef __cplusplus
}
#endif
