#pragma once
#include <aether/aether_work_counters.h>

/* Additive DOMsync diagnostics. The original aether_work_counters_v1_t ABI is
 * unchanged. These counters share its opt-in, calling-thread enable/reset and
 * saturating arithmetic. Work includes aborted attempts, not only publication.
 * No pointer, node identity, attribute value or input position is recorded. */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct aether_dom_work_counters_v1_s {
    uint64_t available; /* 1 only when compiled with work counters. */
    uint64_t enabled; /* Collection on the calling thread at read time. */
    uint64_t dom_commit_allocation_coverage; /* 0 = unavailable, NOT measured zero. */
    uint64_t dom_token_reads;
    uint64_t dom_snapshot_builds; /* Attempts entering snapshot construction. */
    uint64_t dom_snapshot_nodes_visited;
    uint64_t dom_snapshot_attributes_visited;
    uint64_t dom_snapshot_children_visited;
    uint64_t dom_nodes_imported; /* Managed node payloads prepared, including aborts. */
    uint64_t dom_edit_prepare_attempts;
    uint64_t dom_edits_prepared; /* Successfully returned prepared handles. */
    uint64_t dom_edits_committed;
    uint64_t dom_edits_aborted; /* Failed prepare or uncommitted handle destroyed. */
    uint64_t dom_commit_allocations; /* Read ONLY with nonzero coverage. */
    uint64_t transition_candidates_cloned; /* Style candidate or atomic Tick storage. */
    uint64_t transition_entries_copied; /* Actual completed active/finished vector copies. */
    uint64_t managed_dom_parse_calls;
    uint64_t managed_dom_rebind_calls;
    uint64_t managed_dom_attributes_visited;
    uint64_t managed_dom_children_visited;
    uint64_t dom_import_initial_binding;
    uint64_t dom_import_token_changed;
} aether_dom_work_counters_v1_t;

/* Managed bridge sites report actual work. These are not global native
 * parse/rebind/all-traversal counters. Unknown kinds are rejected. */
typedef enum aether_dom_managed_work_kind_e {
    AETHER_DOM_WORK_NODES_IMPORTED = 0,
    AETHER_DOM_WORK_PARSE_CALL = 1,
    AETHER_DOM_WORK_REBIND_CALL = 2,
    AETHER_DOM_WORK_ATTRIBUTES_VISITED = 3,
    AETHER_DOM_WORK_CHILDREN_VISITED = 4,
    AETHER_DOM_WORK_IMPORT_INITIAL_BINDING = 5,
    AETHER_DOM_WORK_IMPORT_TOKEN_CHANGED = 6
} aether_dom_managed_work_kind_t;
AETHER_API aether_status_t aether_dom_work_counters_get_v1(aether_dom_work_counters_v1_t* out_counters);
AETHER_API aether_status_t aether_dom_work_counters_record_managed_v1(int32_t kind, uint64_t amount);
#ifdef __cplusplus
}
#endif
