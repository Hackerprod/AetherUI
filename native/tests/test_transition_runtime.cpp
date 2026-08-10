#include <aether/aether.h>
#include "animations/transition_runtime.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static bool near(float a, float b, float tolerance = 0.01f) {
    return std::fabs(a - b) <= tolerance;
}

static aether_transform_list make_scale_list(float scale) {
    auto* operation = static_cast<aether_transform_operation*>(
        std::malloc(sizeof(aether_transform_operation)));
    require(operation != nullptr, "transform operation allocation failed");
    *operation = {};
    operation->kind = AETHER_TRANSFORM_SCALE;
    operation->x = aether_css_length_zero();
    operation->y = aether_css_length_zero();
    operation->scalar_x = scale;
    operation->scalar_y = scale;
    return {operation, 1};
}

static aether_text_metrics_t measure_text(const char* text, const aether_computed_style_t*, float, void*) {
    const float width = text ? static_cast<float>(std::strlen(text)) : 0.f;
    return {width, 1.f, .8f};
}

/* opacity: 1 -> 0 over 300ms linear. */
static void test_opacity_linear(aether_node_t node, aether_node_t other_node) {
    aether_transition_runtime_t runtime = nullptr;
    require(aether_transition_runtime_create(&runtime) == AETHER_OK, "opacity: runtime create failed");

    aether_computed_style_t before = aether_computed_style_default();
    before.opacity = 1.0f;
    before.transition_properties = AETHER_TRANSITION_PROPERTY_OPACITY;
    before.transition_duration = 0.3f;
    before.transition_timing = AETHER_TRANSITION_TIMING_LINEAR;
    aether_computed_style_t after = before;
    after.opacity = 0.0f;

    require(aether_transition_runtime_observe(runtime, node, &before, &after, 0) == AETHER_OK,
        "opacity: observe failed");
    require(aether_transition_runtime_has_active(runtime) == AETHER_TRUE,
        "opacity: transition must start active");

    float opacity = -1.f;
    require(aether_transition_runtime_opacity_override(runtime, node, &opacity) == AETHER_TRUE,
        "opacity: override missing at t=0");
    require(near(opacity, 1.0f), "opacity: must start at the from value");
    require(aether_transition_runtime_opacity_override(runtime, other_node, &opacity) == AETHER_FALSE,
        "opacity: a different element must not receive the override");

    require(aether_transition_runtime_tick(runtime, 150) == AETHER_TRUE,
        "opacity: halfway tick must remain active");
    require(aether_transition_runtime_opacity_override(runtime, node, &opacity) == AETHER_TRUE,
        "opacity: override missing at halfway");
    require(near(opacity, 0.5f), "opacity: linear interpolation at halfway");

    require(aether_transition_runtime_tick(runtime, 400) == AETHER_FALSE,
        "opacity: completed transition must report no active");
    require(aether_transition_runtime_has_active(runtime) == AETHER_FALSE,
        "opacity: has_active false after completion");
    require(aether_transition_runtime_opacity_override(runtime, node, &opacity) == AETHER_TRUE,
        "opacity: finished override must still serve the end value");
    require(opacity == 0.0f, "opacity: finished value must be exactly the end value");

    aether_transition_runtime_destroy(runtime);
}

/* transform: scale(0.8) -> scale(1) over 400ms linear. */
static void test_transform_linear(aether_node_t node) {
    aether_transition_runtime_t runtime = nullptr;
    require(aether_transition_runtime_create(&runtime) == AETHER_OK, "transform: runtime create failed");

    aether_computed_style_t before = aether_computed_style_default();
    before.transforms = make_scale_list(0.8f);
    before.transition_properties = AETHER_TRANSITION_PROPERTY_TRANSFORM;
    before.transition_duration = 0.4f;
    before.transition_timing = AETHER_TRANSITION_TIMING_LINEAR;
    aether_computed_style_t after = before;
    after.transforms = make_scale_list(1.0f);

    require(aether_transition_runtime_observe(runtime, node, &before, &after, 0) == AETHER_OK,
        "transform: observe failed");
    require(aether_transition_runtime_tick(runtime, 200) == AETHER_TRUE,
        "transform: halfway tick must remain active");

    aether_transform_matrix matrix{};
    require(aether_transition_runtime_transform_override(runtime, node, &matrix) == AETHER_TRUE,
        "transform: override missing at halfway");
    require(near(matrix.m11, 0.9f), "transform: m11 interpolated at halfway");
    require(near(matrix.m22, 0.9f), "transform: m22 interpolated at halfway");

    require(aether_transition_runtime_tick(runtime, 500) == AETHER_FALSE,
        "transform: completed transition must report no active");
    require(aether_transition_runtime_transform_override(runtime, node, &matrix) == AETHER_TRUE,
        "transform: finished override must still serve the end matrix");
    require(near(matrix.m11, 1.0f), "transform: finished m11 must be the end value");

    aether_computed_style_free(&before);
    aether_computed_style_free(&after);
    aether_transition_runtime_destroy(runtime);
}

/* No duration, no property bits, and unchanged values must not animate. */
static void test_no_transition(aether_node_t node) {
    aether_transition_runtime_t runtime = nullptr;
    require(aether_transition_runtime_create(&runtime) == AETHER_OK, "none: runtime create failed");

    aether_computed_style_t before = aether_computed_style_default();
    before.opacity = 1.0f;
    aether_computed_style_t after = before;
    after.opacity = 0.0f;

    // duration 0 => nothing starts.
    before.transition_properties = AETHER_TRANSITION_PROPERTY_OPACITY;
    before.transition_duration = 0.0f;
    after.transition_properties = AETHER_TRANSITION_PROPERTY_OPACITY;
    after.transition_duration = 0.0f;
    require(aether_transition_runtime_observe(runtime, node, &before, &after, 0) == AETHER_OK,
        "none: observe (zero duration) failed");
    require(aether_transition_runtime_has_active(runtime) == AETHER_FALSE,
        "none: zero duration must not start a transition");

    // properties = none => nothing starts even with a duration.
    before.transition_properties = 0;
    after.transition_properties = 0;
    before.transition_duration = 0.3f;
    after.transition_duration = 0.3f;
    require(aether_transition_runtime_observe(runtime, node, &before, &after, 0) == AETHER_OK,
        "none: observe (none property) failed");
    require(aether_transition_runtime_has_active(runtime) == AETHER_FALSE,
        "none: transition-property: none must not start a transition");

    // Value unchanged => nothing starts.
    after.opacity = 1.0f;
    after.transition_properties = AETHER_TRANSITION_PROPERTY_OPACITY;
    require(aether_transition_runtime_observe(runtime, node, &before, &after, 0) == AETHER_OK,
        "none: observe (unchanged value) failed");
    require(aether_transition_runtime_has_active(runtime) == AETHER_FALSE,
        "none: unchanged value must not start a transition");

    float opacity = -1.f;
    require(aether_transition_runtime_opacity_override(runtime, node, &opacity) == AETHER_FALSE,
        "none: no override may exist without an active transition");

    aether_transition_runtime_destroy(runtime);
}

/* delay: duration 200ms, delay 100ms. */
static void test_delay(aether_node_t node) {
    aether_transition_runtime_t runtime = nullptr;
    require(aether_transition_runtime_create(&runtime) == AETHER_OK, "delay: runtime create failed");

    aether_computed_style_t before = aether_computed_style_default();
    before.opacity = 1.0f;
    before.transition_properties = AETHER_TRANSITION_PROPERTY_OPACITY;
    before.transition_duration = 0.2f;
    before.transition_delay = 0.1f;
    before.transition_timing = AETHER_TRANSITION_TIMING_LINEAR;
    aether_computed_style_t after = before;
    after.opacity = 0.0f;

    require(aether_transition_runtime_observe(runtime, node, &before, &after, 0) == AETHER_OK,
        "delay: observe failed");
    require(aether_transition_runtime_tick(runtime, 50) == AETHER_TRUE,
        "delay: during the delay the transition is still active");
    float opacity = -1.f;
    require(aether_transition_runtime_opacity_override(runtime, node, &opacity) == AETHER_TRUE,
        "delay: override missing while waiting");
    require(near(opacity, 1.0f), "delay: value must hold the from value before the delay elapses");

    require(aether_transition_runtime_tick(runtime, 200) == AETHER_TRUE,
        "delay: halfway after the delay must still be active");
    require(aether_transition_runtime_opacity_override(runtime, node, &opacity) == AETHER_TRUE,
        "delay: override missing at 200ms");
    require(near(opacity, 0.5f), "delay: linear interpolation after the delay");

    aether_transition_runtime_destroy(runtime);
}

/* ease-in is slower than linear at half progress. */
static void test_ease_in(aether_node_t node) {
    aether_transition_runtime_t runtime = nullptr;
    require(aether_transition_runtime_create(&runtime) == AETHER_OK, "ease-in: runtime create failed");

    aether_computed_style_t before = aether_computed_style_default();
    before.opacity = 0.0f;
    before.transition_properties = AETHER_TRANSITION_PROPERTY_OPACITY;
    before.transition_duration = 0.4f;
    before.transition_timing = AETHER_TRANSITION_TIMING_EASE_IN;
    aether_computed_style_t after = before;
    after.opacity = 1.0f;

    require(aether_transition_runtime_observe(runtime, node, &before, &after, 0) == AETHER_OK,
        "ease-in: observe failed");
    require(aether_transition_runtime_tick(runtime, 200) == AETHER_TRUE,
        "ease-in: halfway tick must remain active");
    float opacity = -1.f;
    require(aether_transition_runtime_opacity_override(runtime, node, &opacity) == AETHER_TRUE,
        "ease-in: override missing at halfway");
    require(opacity > 0.05f && opacity < 0.5f,
        "ease-in: eased value at 50% progress must trail the linear value");

    aether_transition_runtime_destroy(runtime);
}

/* Display-list consumption: a transform transition from `transform: none`
   must still push the interpolated matrix. */
static void test_display_list_transform_override() {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t style_runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t layout_engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t display_list = nullptr;
    aether_transition_runtime_t transitions = nullptr;

    require(aether_doc_create(&doc) == AETHER_OK, "display-list: document creation failed");
    require(aether_doc_parse_html(doc, "<body><div id=\"box\"></div></body>") == AETHER_OK,
        "display-list: document parsing failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200.f, 100.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &style_runtime) == AETHER_OK,
        "display-list: style runtime creation failed");
    int64_t stylesheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(style_runtime,
        "body { display: block; margin: 0; } #box { display: block; width: 100px; height: 50px; "
        "background-color: rgb(255, 0, 0); transition: transform 0.4s linear; }",
        0, "transition-display-list.css", &stylesheet, &rule_count) == AETHER_OK && rule_count == 2,
        "display-list: stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(style_runtime, aether_doc_get_body(doc), &updates);
    require(update_count > 0, "display-list: style resolution failed");
    aether_node_t box = aether_doc_get_element_by_id(doc, "box");
    require(box != nullptr, "display-list: box element lookup failed");
    aether_style_updates_free(updates, update_count);
    require(aether_style_runtime_create_resolved_style_map(style_runtime, doc, &styles) == AETHER_OK,
        "display-list: resolved style map creation failed");
    require(aether_layout_create(measure_text, nullptr, &layout_engine) == AETHER_OK,
        "display-list: layout engine creation failed");
    require(aether_layout_layout(layout_engine, doc, styles, 200.f, 100.f, nullptr, nullptr, &layout) == AETHER_OK,
        "display-list: layout execution failed");
    require(aether_display_list_builder_create(measure_text, nullptr, &builder) == AETHER_OK,
        "display-list: builder creation failed");
    require(aether_transition_runtime_create(&transitions) == AETHER_OK,
        "display-list: transition runtime creation failed");
    aether_display_list_builder_set_transition_runtime(builder, transitions);

    // #box has `transform: none` in its computed style; transition it to scale(2).
    aether_computed_style_t before = aether_computed_style_default();
    before.transition_properties = AETHER_TRANSITION_PROPERTY_TRANSFORM;
    before.transition_duration = 0.4f;
    before.transition_timing = AETHER_TRANSITION_TIMING_LINEAR;
    aether_computed_style_t after = before;
    after.transforms = make_scale_list(2.0f);

    require(aether_transition_runtime_observe(transitions, box, &before, &after, 0) == AETHER_OK,
        "display-list: observe failed");
    require(aether_transition_runtime_tick(transitions, 200) == AETHER_TRUE,
        "display-list: halfway tick must remain active");
    require(aether_display_list_build(builder, layout, nullptr, &display_list) == AETHER_OK,
        "display-list: build failed");

    const int32_t command_count = aether_display_list_get_count(display_list);
    aether_transform_matrix push_matrix{};
    bool found_push = false;
    for (int32_t index = 0; index < command_count; ++index) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(display_list, index, &command) == AETHER_OK,
            "display-list: command read failed");
        if (command.tag == AETHER_PAINT_PUSH_TRANSFORM) {
            push_matrix = command.data.push_transform.matrix;
            found_push = true;
        }
        aether_paint_command_free(&command);
    }
    require(found_push, "display-list: transform transition must emit PUSH_TRANSFORM");
    require(near(push_matrix.m11, 1.5f), "display-list: push matrix must be the interpolated midpoint");

    aether_computed_style_free(&before);
    aether_computed_style_free(&after);
    aether_display_list_destroy(display_list);
    aether_display_list_builder_destroy(builder);
    aether_transition_runtime_destroy(transitions);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(layout_engine);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(style_runtime);
    aether_doc_destroy(doc);
}

static void test_shorthand_roundtrip();

int main() {
    aether_doc_t doc = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(doc, "<div id=\"a\"></div><div id=\"b\"></div>") == AETHER_OK,
        "document parsing failed");
    aether_node_t node_a = aether_doc_get_element_by_id(doc, "a");
    aether_node_t node_b = aether_doc_get_element_by_id(doc, "b");
    require(node_a != nullptr && node_b != nullptr, "element lookup failed");

    test_opacity_linear(node_a, node_b);
    test_transform_linear(node_a);
    test_no_transition(node_a);
    test_delay(node_a);
    test_ease_in(node_a);

    aether_doc_destroy(doc);

    test_shorthand_roundtrip();
    test_display_list_transform_override();

    return 0;
}

/* The `transition` shorthand must round-trip through the longhands: `transition:
   all 0.3s ease` must yield properties=OPACITY|TRANSFORM, duration=0.3, timing=EASE
   (regression: the shorthand used to write the numeric bitmask, which the
   transition-property longhand parser rejected, so properties stayed 0). */
static void test_shorthand_roundtrip() {
    aether_doc_t doc = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK, "shorthand: document creation failed");
    require(aether_doc_parse_html(doc, "<div id=\"box\"></div>") == AETHER_OK,
        "shorthand: document parsing failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200.f, 100.f, 1.f, 16.f, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "shorthand: style runtime creation failed");
    int64_t sheet = 0;
    require(aether_style_runtime_add_stylesheet(runtime,
        "#box { transition: all 0.3s ease; }",
        0, "shorthand.css", &sheet, nullptr) == AETHER_OK,
        "shorthand: stylesheet compilation failed");

    aether_node_t box = aether_doc_get_element_by_id(doc, "box");
    require(box != nullptr, "shorthand: box lookup failed");
    aether_style_update_t* updates = nullptr;
    const int32_t count = aether_style_runtime_resolve_subtree(runtime, box, &updates);
    require(count > 0, "shorthand: resolve failed");

    aether_computed_style_handle_t handle = nullptr;
    for (int32_t index = 0; index < count; ++index) {
        aether_node_t updated = nullptr;
        aether_computed_style_handle_t candidate = nullptr;
        require(aether_style_runtime_get_update(updates[index], &updated, &candidate, nullptr) == AETHER_OK,
            "shorthand: update read failed");
        if (updated == box) handle = candidate;
        else aether_computed_style_handle_destroy(candidate);
    }
    require(handle != nullptr, "shorthand: box style update missing");
    aether_style_updates_free(updates, count);

    aether_computed_style_t style{};
    require(aether_computed_style_copy(handle, &style) == AETHER_OK, "shorthand: style copy failed");
    aether_computed_style_handle_destroy(handle);

    require(style.transition_properties ==
            (AETHER_TRANSITION_PROPERTY_OPACITY | AETHER_TRANSITION_PROPERTY_TRANSFORM),
        "shorthand: transition: all must map to OPACITY|TRANSFORM");
    require(near(style.transition_duration, 0.3f), "shorthand: duration must be 0.3s");
    require(style.transition_timing == AETHER_TRANSITION_TIMING_EASE,
        "shorthand: timing must be ease");

    aether_computed_style_free(&style);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
}
