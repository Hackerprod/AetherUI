#ifndef AETHER_CONTEXTUAL_LAYOUT_H
#define AETHER_CONTEXTUAL_LAYOUT_H
#include <aether/aether.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Clones used geometry without sizing or invoking text measurement.
 * Source and clone are independently publishable, with bounded retained storage. */
AETHER_API aether_status_t aether_layout_clone_snapshot(aether_layout_result_t source, aether_layout_result_t* out);
/* Additive measurement contract. used_line_height is CSS px; flat style remains
 * the legacy projection and must not be used to infer line-height units. */
typedef aether_text_metrics_t (*aether_text_measurer_contextual_fn)(
    const char* text, const aether_computed_style_t* style,
    float used_line_height, float max_width, void* user_data);
AETHER_API void aether_layout_set_contextual_text_measurer(aether_layout_engine_t engine,
    aether_text_measurer_contextual_fn callback, void* user_data);
AETHER_API void aether_display_list_builder_set_contextual_text_measurer(aether_display_list_builder_t builder,
    aether_text_measurer_contextual_fn callback, void* user_data);
#ifdef __cplusplus
}
#endif
#endif
