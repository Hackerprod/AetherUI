#pragma once
#include <aether/aether.h>
#ifdef __cplusplus
extern "C" {
#endif
/* D2 diagnostic oracle. Performs a FULL map traversal deliberately. Run outside
 * measured intervals; it does not alter the engine, layout, map or transitions. */
AETHER_API aether_status_t aether_layout_context_validate_full(
    aether_transition_runtime_t runtime,aether_style_map_t map,aether_layout_result_t layout,
    aether_bool_t* out_equal);
/* Equality-only snapshot serial, retained by the layout. Not an input token. */
AETHER_API aether_status_t aether_layout_context_snapshot_info(
    aether_layout_result_t layout,uint64_t* out_serial,uint64_t* out_size);
#ifdef __cplusplus
}
#endif
