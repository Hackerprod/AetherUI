#pragma once
#include <aether/aether_work_counters.h>
namespace aether::diagnostics {
enum class WorkCounter { compute_nodes, state_entries_visited, revision_records_created, revision_entries_created, revision_prepares, revision_publications, state_full_fallbacks, map_entries_copied, transition_context_entries_visited, transition_context_builds, hit_queries, hit_boxes_visited, paint_order_children_classified, context_calls, context_backing_visits, context_active_visits, context_updates, context_builds, context_reuses, context_fallbacks, context_allocations, context_index_visits, context_static_visits, context_fallback_reason };
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
// Exported for internal inline layout helpers also instantiated by native tests.
AETHER_API void count_work(WorkCounter counter, uint64_t amount = 1) noexcept;
#else
inline void count_work(WorkCounter, uint64_t = 1) noexcept {}
#endif
}
