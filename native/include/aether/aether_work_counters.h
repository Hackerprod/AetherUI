#pragma once
#include <aether/aether.h>

/* Diagnostic counters for work performed on the calling thread, not visual
 * updates. Disabled by default. No node identities or pointer positions are
 * retained. Reset never enables collection. Counts include aborted work and
 * saturate at UINT64_MAX. Enable only in a diagnostic/benchmark build configured
 * with AETHERUI_ENABLE_WORK_COUNTERS=ON; unsupported builds return false. */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct aether_work_counters_v1_s {
    uint64_t compute_nodes;
    uint64_t state_entries_visited;
    uint64_t revision_records_created;
    uint64_t revision_entries_created;
    uint64_t revision_prepares;
    uint64_t revision_publications;
    uint64_t state_full_fallbacks;
    uint64_t map_entries_copied;
    uint64_t transition_context_entries_visited;
    uint64_t transition_context_builds;
    uint64_t hit_queries;
    uint64_t hit_boxes_visited;
    uint64_t paint_order_children_classified;
} aether_work_counters_v1_t;
typedef struct aether_context_work_counters_v1_s {
    uint64_t calls, backing_entries_visited, active_owner_entries_visited;
    uint64_t owners_updated, snapshot_builds, snapshot_reuses, fallbacks;
    uint64_t allocations, index_nodes_visited, static_entries_visited;
    uint64_t fallback_reason;
} aether_context_work_counters_v1_t;
/* E1 ordinary-content queries only; input chrome remains the full oracle.
 * Build work includes failed staging and is separate from warm query work. */
typedef struct aether_hit_work_counters_v1_s {
    uint64_t cache_hits, cache_misses, build_attempts, builds;
    uint64_t build_boxes, build_range_nodes, query_range_nodes;
    uint64_t exact_boxes, conservative_bounds, fallbacks, fallback_reason;
    uint64_t allocation_requests, requested_bytes;
} aether_hit_work_counters_v1_t;
AETHER_API aether_status_t aether_hit_work_counters_get_v1(aether_hit_work_counters_v1_t* out_counters);
AETHER_API aether_status_t aether_context_work_counters_get_v1(aether_context_work_counters_v1_t* out_counters);
AETHER_API aether_bool_t aether_work_counters_set_enabled(aether_bool_t enabled);
AETHER_API void aether_work_counters_reset(void);
AETHER_API aether_status_t aether_work_counters_get_v1(aether_work_counters_v1_t* out_counters);
#ifdef __cplusplus
}
#endif
