#ifndef AETHER_STATE_STYLE_H
#define AETHER_STATE_STYLE_H
#include <aether/aether_contextual_style.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Additive state transaction API. It observes already-applied DOM inputs and
 * never writes caller old/new values back to the DOM. Errors are not zero deltas.
 * Document access must be externally serialized, as for the existing ABI. */
typedef struct aether_style_state_change_s {
    aether_node_t node;
    uint64_t node_identity;
    int32_t old_state;
    int32_t new_state;
} aether_style_state_change_t;
typedef enum aether_state_fallback_e {
    AETHER_STATE_FALLBACK_NONE = 0,
    AETHER_STATE_FALLBACK_DISABLED,
    AETHER_STATE_FALLBACK_FIRST_REVISION,
    AETHER_STATE_FALLBACK_BASE_REVISION,
    AETHER_STATE_FALLBACK_SHEET,
    AETHER_STATE_FALLBACK_ENVIRONMENT,
    AETHER_STATE_FALLBACK_STRUCTURE,
    AETHER_STATE_FALLBACK_JOURNAL_MISSING,
    AETHER_STATE_FALLBACK_JOURNAL_INCOMPLETE,
    AETHER_STATE_FALLBACK_NON_STATE_MUTATION,
    AETHER_STATE_FALLBACK_IDENTITY,
    AETHER_STATE_FALLBACK_LEDGER,
    AETHER_STATE_FALLBACK_CALLER_BATCH,
    AETHER_STATE_FALLBACK_SELECTOR,
    AETHER_STATE_FALLBACK_PSEUDO,
    AETHER_STATE_FALLBACK_ROOT_FONT,
    AETHER_STATE_FALLBACK_REGISTRY
} aether_state_fallback_t;
typedef struct aether_style_state_result_s {
    uint32_t struct_size;
    aether_bool_t incremental;
    aether_state_fallback_t fallback;
    int32_t semantic_delta_count;
    int32_t visual_delta_count;
    uint32_t visual_impact;
    uint64_t generation;
    uint64_t document_identity;
    uint64_t input_epoch;
    uint64_t structure_epoch;
    uint64_t style_tree_epoch;
} aether_style_state_result_t;
AETHER_API uint32_t aether_state_style_version(void);
AETHER_API aether_status_t aether_style_runtime_prepare_state_revision(
    aether_style_runtime_t runtime, aether_node_t root, aether_style_revision_t base_revision,
    const aether_style_state_change_t* changes, int32_t change_count,
    const aether_style_environment_t* environment, aether_style_transaction_t* out_transaction);
/* Pure inspection of a prepared candidate. A candidate is not yet current and
 * must not be used for layout/hit-test. Commit is still required and may reject.
 * Initialize out_result.struct_size to sizeof(aether_style_state_result_t). */
AETHER_API aether_status_t aether_style_transaction_state_result(
    aether_style_transaction_t transaction, aether_style_state_result_t* out_result);
/* Compact semantic delta, unlike the legacy complete logical change view.
 * Metadata-only entries have visual impact zero. Removed entries have no style.
 * Exported handles retain this exact candidate generation across later commits. */
AETHER_API aether_status_t aether_style_transaction_get_state_delta(
    aether_style_transaction_t transaction, int32_t index, aether_node_t* out_node,
    aether_computed_style_handle_t* out_style, uint32_t* out_visual_impact);
/* Managed publication support, independent of the incremental capability.
 * Opaque identities are equality-only and must be protected by a retained
 * transaction/revision. Transaction inspection requires its runtime alive.
 * All operations require the runtime owner thread.
 * Preview artifacts never authorize layout, hit-test or presentation. */
typedef struct aether_style_publication_token_s {
    uint32_t struct_size;
    aether_bool_t inputs_current;
    uint64_t revision_identity, generation, document_identity;
    uint64_t input_epoch, structure_epoch, style_tree_epoch;
    uint64_t sheet_epoch, registry_identity;
} aether_style_publication_token_t;
AETHER_API aether_status_t aether_style_runtime_read_publication_token(
    aether_style_runtime_t runtime, aether_style_publication_token_t* out_token);
AETHER_API aether_status_t aether_style_transaction_read_publication_token(
    aether_style_transaction_t transaction, aether_style_publication_token_t* out_token);
AETHER_API aether_status_t aether_style_transaction_preview_revision(
    aether_style_transaction_t transaction, aether_style_revision_t* out_revision);
AETHER_API aether_status_t aether_style_transaction_preview_legacy_map(
    aether_style_transaction_t transaction, aether_style_map_t* out_map);
/* Staging-only legacy patch. Base must be authentic, non-contextual and belong
 * to this document. Never mutates base. Semantic metadata and measure-context
 * changes are included even when their visual impact is zero. Full enumeration
 * is permitted here; this capability makes no incremental complexity promise. */
AETHER_API aether_status_t aether_style_transaction_preview_legacy_patch(
    aether_style_transaction_t transaction, aether_style_map_t base_map, aether_style_map_t* out_map);
/* Complete candidate index, but out_style is null for a provably reusable
 * legacy projection. out_impact remains the candidate's original impact.
 * Null base_map requests all styles. Callers keep their prior projection when
 * out_style is null; this is not an absent/removed node. */
AETHER_API aether_status_t aether_style_transaction_get_publication_style(
    aether_style_transaction_t transaction, aether_style_map_t base_map, int32_t index,
    aether_node_t* out_node, aether_computed_style_handle_t* out_style, uint32_t* out_impact);
#ifdef __cplusplus
}
#endif
#endif
