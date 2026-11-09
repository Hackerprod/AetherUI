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
AETHER_API aether_bool_t aether_work_counters_set_enabled(aether_bool_t enabled);
AETHER_API void aether_work_counters_reset(void);
AETHER_API aether_status_t aether_work_counters_get_v1(aether_work_counters_v1_t* out_counters);
#ifdef __cplusplus
}
#endif
