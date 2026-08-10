#pragma once

#include <aether/aether.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── CSS transition runtime (v1: opacity + transform) ─────────────────
 *
 * A generic, self-contained per-element transition engine. The host owns the
 * clock and the style pipeline; this module only records what changed between
 * two computed styles and interpolates the animatable values for the display
 * list builder.
 *
 * The runtime is pre-layout: transform matrices are resolved against a
 * placeholder border box ({0,0,100,100}) because the runtime observes computed
 * styles, not layout boxes. The display list builder applies the interpolated
 * matrix over the real, post-layout geometry.
 */

typedef struct aether_transition_runtime_s* aether_transition_runtime_t;

AETHER_API aether_status_t aether_transition_runtime_create(aether_transition_runtime_t* out_runtime);
AETHER_API void aether_transition_runtime_destroy(aether_transition_runtime_t runtime);

/* Compares an element's previous computed style against its current one. For
   every property bit set in `after->transition_properties` with a positive
   transition_duration whose value actually changed, a transition is started
   at now_ms + delay. An interrupted in-flight transition restarts from its
   current interpolated value so the element never jumps. */
AETHER_API aether_status_t aether_transition_runtime_observe(
    aether_transition_runtime_t runtime,
    aether_node_t node,
    const aether_computed_style_t* before,
    const aether_computed_style_t* after,
    int64_t now_ms);

/* Advances every active transition to now_ms. Transitions whose progress
   reaches 1 are moved into a "finished" set that still serves their exact
   end value to the overrides until they are superseded by a new observe or
   the runtime is destroyed. Returns AETHER_TRUE while any transition remains
   active (finished entries are not counted). */
AETHER_API aether_bool_t aether_transition_runtime_tick(aether_transition_runtime_t runtime, int64_t now_ms);

/* True while at least one transition is still progressing. The host calls this
   after tick() to decide whether another frame is required. */
AETHER_API aether_bool_t aether_transition_runtime_has_active(aether_transition_runtime_t runtime);

/* Returns the interpolated (or finished end) opacity for the element when an
   opacity transition is live; AETHER_FALSE otherwise. */
AETHER_API aether_bool_t aether_transition_runtime_opacity_override(
    aether_transition_runtime_t runtime, aether_node_t node, float* out_opacity);

/* Returns the interpolated (or finished end) transform matrix for the element
   when a transform transition is live; AETHER_FALSE otherwise. */
AETHER_API aether_bool_t aether_transition_runtime_transform_override(
    aether_transition_runtime_t runtime, aether_node_t node, aether_transform_matrix* out_matrix);

/* Display list builder integration: the builder consults the runtime for
   per-element opacity/transform overrides while painting. Null disables
   transitions. */
AETHER_API void aether_display_list_builder_set_transition_runtime(
    aether_display_list_builder_t builder, aether_transition_runtime_t runtime);

#ifdef __cplusplus
}
#endif
