#pragma once
#include <aether/aether_work_counters.h>
#include <aether/aether_dom_work_counters.h>
namespace aether::diagnostics {
enum class WorkCounter { compute_nodes, state_entries_visited, revision_records_created, revision_entries_created, revision_prepares, revision_publications, state_full_fallbacks, map_entries_copied, transition_context_entries_visited, transition_context_builds, hit_queries, hit_boxes_visited, paint_order_children_classified,
    dom_token_reads, dom_snapshot_builds, dom_snapshot_nodes_visited, dom_snapshot_attributes_visited, dom_snapshot_children_visited,
    dom_nodes_imported, dom_edit_prepare_attempts, dom_edits_prepared, dom_edits_committed, dom_edits_aborted,
    transition_candidates_cloned, transition_entries_copied, managed_dom_parse_calls, managed_dom_rebind_calls,
    managed_dom_attributes_visited, managed_dom_children_visited, dom_import_initial_binding, dom_import_token_changed };
#if defined(AETHERUI_ENABLE_WORK_COUNTERS)
// Exported for internal inline layout helpers also instantiated by native tests.
AETHER_API void count_work(WorkCounter counter, uint64_t amount = 1) noexcept;
#else
inline void count_work(WorkCounter, uint64_t = 1) noexcept {}
#endif
}
