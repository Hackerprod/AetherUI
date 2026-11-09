#include "work_counters.h"
#include <limits>
namespace {
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
thread_local bool enabled = false;
thread_local aether_work_counters_v1_t counters{};
thread_local aether_context_work_counters_v1_t context{};
thread_local aether_dom_work_counters_v1_t dom_counters{};
thread_local aether_hit_work_counters_v1_t hit{};
#endif
}
extern "C" {
AETHER_API aether_bool_t aether_work_counters_set_enabled(aether_bool_t value) {
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
    enabled = value != AETHER_FALSE;
    return AETHER_TRUE;
#else
    (void)value;
    return AETHER_FALSE;
#endif
}
AETHER_API void aether_work_counters_reset(void) {
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
    counters = {}; context = {};
    dom_counters = {};
    hit = {};
#endif
}
AETHER_API aether_status_t aether_hit_work_counters_get_v1(aether_hit_work_counters_v1_t* out) {
    if (!out) return AETHER_ERROR_NULL_ARG;
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
    *out = hit;
#else
    *out = {};
#endif
    return AETHER_OK;
}
AETHER_API aether_status_t aether_context_work_counters_get_v1(aether_context_work_counters_v1_t* out) {
    if (!out) return AETHER_ERROR_NULL_ARG;
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
    *out = context;
#else
    *out = {};
#endif
    return AETHER_OK;
}
AETHER_API aether_status_t aether_work_counters_get_v1(aether_work_counters_v1_t* out) {
    if (!out) return AETHER_ERROR_NULL_ARG;
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
    *out = counters;
#else
    *out = {};
#endif
    return AETHER_OK;
}
AETHER_API aether_status_t aether_dom_work_counters_get_v1(aether_dom_work_counters_v1_t* out) {
    if (!out) return AETHER_ERROR_NULL_ARG;
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
    *out = dom_counters;
    out->available = 1;
    out->enabled = enabled ? 1 : 0;
#else
    *out = {};
#endif
    // No allocator observer is installed. Zero is explicitly NOT a measurement.
    return AETHER_OK;
}
AETHER_API aether_status_t aether_dom_work_counters_record_managed_v1(int32_t kind, uint64_t amount) {
    using aether::diagnostics::WorkCounter;
    WorkCounter counter;
    switch (kind) {
    case AETHER_DOM_WORK_NODES_IMPORTED: counter = WorkCounter::dom_nodes_imported; break;
    case AETHER_DOM_WORK_PARSE_CALL: counter = WorkCounter::managed_dom_parse_calls; break;
    case AETHER_DOM_WORK_REBIND_CALL: counter = WorkCounter::managed_dom_rebind_calls; break;
    case AETHER_DOM_WORK_ATTRIBUTES_VISITED: counter = WorkCounter::managed_dom_attributes_visited; break;
    case AETHER_DOM_WORK_CHILDREN_VISITED: counter = WorkCounter::managed_dom_children_visited; break;
    case AETHER_DOM_WORK_IMPORT_INITIAL_BINDING: counter = WorkCounter::dom_import_initial_binding; break;
    case AETHER_DOM_WORK_IMPORT_TOKEN_CHANGED: counter = WorkCounter::dom_import_token_changed; break;
    default: return AETHER_ERROR_INVALID_STATE;
    }
    aether::diagnostics::count_work(counter, amount);
    return AETHER_OK;
}
}
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
void aether::diagnostics::count_work(WorkCounter counter, uint64_t amount) noexcept {
    if (!enabled) return;
    uint64_t* target = nullptr;
    switch (counter) {
    case WorkCounter::compute_nodes: target = &counters.compute_nodes; break;
    case WorkCounter::state_entries_visited: target = &counters.state_entries_visited; break;
    case WorkCounter::revision_records_created: target = &counters.revision_records_created; break;
    case WorkCounter::revision_entries_created: target = &counters.revision_entries_created; break;
    case WorkCounter::revision_prepares: target = &counters.revision_prepares; break;
    case WorkCounter::revision_publications: target = &counters.revision_publications; break;
    case WorkCounter::state_full_fallbacks: target = &counters.state_full_fallbacks; break;
    case WorkCounter::map_entries_copied: target = &counters.map_entries_copied; break;
    case WorkCounter::transition_context_entries_visited: target = &counters.transition_context_entries_visited; break;
    case WorkCounter::transition_context_builds: target = &counters.transition_context_builds; break;
    case WorkCounter::hit_queries: target = &counters.hit_queries; break;
    case WorkCounter::hit_boxes_visited: target = &counters.hit_boxes_visited; break;
    case WorkCounter::paint_order_children_classified: target = &counters.paint_order_children_classified; break;
    case WorkCounter::dom_token_reads: target = &dom_counters.dom_token_reads; break;
    case WorkCounter::dom_snapshot_builds: target = &dom_counters.dom_snapshot_builds; break;
    case WorkCounter::dom_snapshot_nodes_visited: target = &dom_counters.dom_snapshot_nodes_visited; break;
    case WorkCounter::dom_snapshot_attributes_visited: target = &dom_counters.dom_snapshot_attributes_visited; break;
    case WorkCounter::dom_snapshot_children_visited: target = &dom_counters.dom_snapshot_children_visited; break;
    case WorkCounter::dom_nodes_imported: target = &dom_counters.dom_nodes_imported; break;
    case WorkCounter::dom_edit_prepare_attempts: target = &dom_counters.dom_edit_prepare_attempts; break;
    case WorkCounter::dom_edits_prepared: target = &dom_counters.dom_edits_prepared; break;
    case WorkCounter::dom_edits_committed: target = &dom_counters.dom_edits_committed; break;
    case WorkCounter::dom_edits_aborted: target = &dom_counters.dom_edits_aborted; break;
    case WorkCounter::transition_candidates_cloned: target = &dom_counters.transition_candidates_cloned; break;
    case WorkCounter::transition_entries_copied: target = &dom_counters.transition_entries_copied; break;
    case WorkCounter::managed_dom_parse_calls: target = &dom_counters.managed_dom_parse_calls; break;
    case WorkCounter::managed_dom_rebind_calls: target = &dom_counters.managed_dom_rebind_calls; break;
    case WorkCounter::managed_dom_attributes_visited: target = &dom_counters.managed_dom_attributes_visited; break;
    case WorkCounter::managed_dom_children_visited: target = &dom_counters.managed_dom_children_visited; break;
    case WorkCounter::dom_import_initial_binding: target = &dom_counters.dom_import_initial_binding; break;
    case WorkCounter::dom_import_token_changed: target = &dom_counters.dom_import_token_changed; break;

    case WorkCounter::context_calls: target = &context.calls; break;
    case WorkCounter::context_backing_visits: target = &context.backing_entries_visited; break;
    case WorkCounter::context_active_visits: target = &context.active_owner_entries_visited; break;
    case WorkCounter::context_updates: target = &context.owners_updated; break;
    case WorkCounter::context_builds: target = &context.snapshot_builds; break;
    case WorkCounter::context_reuses: target = &context.snapshot_reuses; break;
    case WorkCounter::context_fallbacks: target = &context.fallbacks; break;
    case WorkCounter::context_allocations: target = &context.allocations; break;
    case WorkCounter::context_index_visits: target = &context.index_nodes_visited; break;
    case WorkCounter::context_static_visits: target = &context.static_entries_visited; break;
    case WorkCounter::context_fallback_reason: context.fallback_reason = amount; return;
    case WorkCounter::hit_cache_hits: target = &hit.cache_hits; break;
    case WorkCounter::hit_cache_misses: target = &hit.cache_misses; break;
    case WorkCounter::hit_build_attempts: target = &hit.build_attempts; break;
    case WorkCounter::hit_builds: target = &hit.builds; break;
    case WorkCounter::hit_build_boxes: target = &hit.build_boxes; break;
    case WorkCounter::hit_build_ranges: target = &hit.build_range_nodes; break;
    case WorkCounter::hit_query_ranges: target = &hit.query_range_nodes; break;
    case WorkCounter::hit_exact_boxes: target = &hit.exact_boxes; break;
    case WorkCounter::hit_conservative_bounds: target = &hit.conservative_bounds; break;
    case WorkCounter::hit_fallbacks: target = &hit.fallbacks; break;
    case WorkCounter::hit_allocations: target = &hit.allocation_requests; break;
    case WorkCounter::hit_bytes: target = &hit.requested_bytes; break;
    case WorkCounter::hit_fallback_reason: hit.fallback_reason = amount; return;
    }
    if (!target) return;
    constexpr auto maximum = std::numeric_limits<uint64_t>::max();
    *target = amount > maximum - *target ? maximum : *target + amount;
}
#endif
