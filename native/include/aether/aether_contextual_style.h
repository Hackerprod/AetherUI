#ifndef AETHER_CONTEXTUAL_STYLE_H
#define AETHER_CONTEXTUAL_STYLE_H
#include <aether/aether.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Additive contextual capability. Flat structs remain a legacy projection. */
typedef struct aether_style_transaction_s* aether_style_transaction_t;
typedef struct aether_style_revision_handle_s* aether_style_revision_t;
AETHER_API uint32_t aether_contextual_style_version(void);
AETHER_API aether_status_t aether_style_runtime_prepare_revision(
    aether_style_runtime_t runtime, aether_node_t root,
    const aether_style_environment_t* environment, aether_style_transaction_t* out_transaction);
AETHER_API aether_status_t aether_style_runtime_commit_revision(
    aether_style_runtime_t runtime, aether_style_transaction_t transaction,
    aether_style_revision_t* out_revision);
AETHER_API void aether_style_transaction_destroy(aether_style_transaction_t transaction);
AETHER_API aether_status_t aether_style_runtime_retain_revision(
    aether_style_runtime_t runtime, aether_style_revision_t* out_revision);
AETHER_API aether_status_t aether_style_revision_clone(
    aether_style_revision_t revision, aether_style_revision_t* out_revision);
AETHER_API void aether_style_revision_destroy(aether_style_revision_t revision);
AETHER_API uint64_t aether_style_revision_generation(aether_style_revision_t revision);
AETHER_API uint64_t aether_style_revision_document_identity(aether_style_revision_t revision);
/* Enumerates the complete retained revision, including entries with visual impact NONE. */
AETHER_API int32_t aether_style_revision_style_count(aether_style_revision_t revision);
AETHER_API aether_status_t aether_style_revision_get_style(
    aether_style_revision_t revision, int32_t index, aether_node_t* out_node,
    aether_computed_style_handle_t* out_style, uint32_t* out_impact);
AETHER_API int32_t aether_style_revision_change_count(aether_style_revision_t revision);
/* A removal has out_style=null. Change records include metadata-only changes. */
AETHER_API aether_status_t aether_style_revision_get_change(
    aether_style_revision_t revision, int32_t index, aether_node_t* out_node,
    aether_computed_style_handle_t* out_style, uint32_t* out_impact);
AETHER_API aether_status_t aether_style_revision_create_map(
    aether_style_revision_t revision, aether_style_map_t* out_map);
AETHER_API aether_status_t aether_computed_style_handle_clone(
    aether_computed_style_handle_t style, aether_computed_style_handle_t* out_style);
/* Typed line height and independent gaps are intentionally not a flat round trip. */
AETHER_API aether_status_t aether_computed_style_contextual_metrics(
    aether_computed_style_handle_t style, float* out_used_line_height,
    aether_css_length* out_row_gap, aether_css_length* out_column_gap,
    uint64_t* out_generation, uint64_t* out_document_identity, uint64_t* out_node_identity);
#ifdef __cplusplus
}
#endif
#endif
