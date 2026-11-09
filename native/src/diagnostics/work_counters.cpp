#include "work_counters.h"
#include <limits>
namespace {
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
thread_local bool enabled = false;
thread_local aether_work_counters_v1_t counters{};
thread_local aether_context_work_counters_v1_t context{};
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
#endif
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
    }
    if (!target) return;
    constexpr auto maximum = std::numeric_limits<uint64_t>::max();
    *target = amount > maximum - *target ? maximum : *target + amount;
}
#endif
