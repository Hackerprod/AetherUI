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

#include "transition_opacity_paint_cases.h"

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

/* These checks intentionally use require, not assert: Release is a supported
   validation configuration, including all calls that mutate the runtime. */
struct RuntimeFixture {
    aether_transition_runtime_t runtime = nullptr;
    RuntimeFixture() {
        require(aether_transition_runtime_create(&runtime) == AETHER_OK,
            "fixture: runtime creation failed");
    }
    ~RuntimeFixture() { aether_transition_runtime_destroy(runtime); }
    RuntimeFixture(const RuntimeFixture&) = delete;
    RuntimeFixture& operator=(const RuntimeFixture&) = delete;
};

static aether_computed_style_t opacity_style(float opacity, float delay = 0.f) {
    auto style = aether_computed_style_default();
    style.opacity = opacity;
    style.transition_properties = AETHER_TRANSITION_PROPERTY_OPACITY;
    style.transition_duration = 1.f;
    style.transition_delay = delay;
    style.transition_timing = AETHER_TRANSITION_TIMING_LINEAR;
    return style;
}

static void observe(aether_transition_runtime_t runtime, aether_node_t node,
                    const aether_computed_style_t& before,
                    const aether_computed_style_t& after, int64_t now) {
    require(aether_transition_runtime_observe(runtime, node, &before, &after, now) == AETHER_OK,
        "observe: valid styles must succeed");
}

static void check_opacity(aether_transition_runtime_t runtime, aether_node_t node,
                          float expected, float tolerance = .00001f) {
    float opacity = -9.f;
    require(aether_transition_runtime_opacity_override(runtime, node, &opacity) == AETHER_TRUE,
        "opacity: expected override missing");
    require(near(opacity, expected, tolerance), "opacity: incorrect override value");
}

static void check_no_opacity(aether_transition_runtime_t runtime, aether_node_t node) {
    float sentinel = -9.f;
    require(aether_transition_runtime_opacity_override(runtime, node, &sentinel) == AETHER_FALSE,
        "cancel: obsolete opacity override must disappear immediately");
    require(sentinel == -9.f, "cancel: absent override must leave output untouched");
}

static void test_opacity_cancellation_matrix(aether_node_t node) {
    for (int state = 0; state < 3; ++state) {
        for (int reason = 0; reason < 3; ++reason) {
            for (int changed = 0; changed < 2; ++changed) {
                RuntimeFixture fixture;
                const auto before = opacity_style(1.f, state == 0 ? 1.f : 0.f);
                auto target = before;
                target.opacity = 0.f;
                observe(fixture.runtime, node, before, target, 0);
                const int64_t now = state == 2 ? 1000 : 500;
                require(aether_transition_runtime_tick(fixture.runtime, now) ==
                    (state == 2 ? AETHER_FALSE : AETHER_TRUE), "matrix: unexpected initial activity");
                check_opacity(fixture.runtime, node, state == 0 ? 1.f : state == 1 ? .5f : 0.f);
                auto cancelled = target;
                cancelled.opacity = changed ? .75f : target.opacity;
                if (reason == 0) cancelled.transition_properties = 0;
                if (reason == 1) cancelled.transition_properties = AETHER_TRANSITION_PROPERTY_TRANSFORM;
                if (reason == 2) cancelled.transition_duration = 0.f;
                std::printf("cancel matrix: state=%d reason=%d changed=%d\n", state, reason, changed);
                observe(fixture.runtime, node, target, cancelled, now);
                check_no_opacity(fixture.runtime, node); // Deliberately no intervening tick.
                require(aether_transition_runtime_has_active(fixture.runtime) == AETHER_FALSE,
                    "matrix: cancelled sole channel must not keep frames active");
                observe(fixture.runtime, node, cancelled, cancelled, now);
                require(aether_transition_runtime_tick(fixture.runtime, 10000) == AETHER_FALSE,
                    "matrix: later tick must not resurrect cancelled activity");
                check_no_opacity(fixture.runtime, node);
            }
        }
    }
    // The C ABI can supply negative finite duration, independently of CSS parsing.
    RuntimeFixture fixture;
    const auto before = opacity_style(1.f);
    auto target = opacity_style(0.f);
    observe(fixture.runtime, node, before, target, 0);
    auto cancelled = target;
    cancelled.transition_duration = -1.f;
    observe(fixture.runtime, node, target, cancelled, 500);
    check_no_opacity(fixture.runtime, node);
    require(aether_transition_runtime_has_active(fixture.runtime) == AETHER_FALSE,
        "negative duration: channel must be cancelled");
}

static void test_opacity_cancellation_boundaries(aether_node_t node) {
    for (int boundary = 0; boundary < 3; ++boundary) {
        for (int tick_first = 0; tick_first < 2; ++tick_first) {
            RuntimeFixture fixture;
            const auto before = opacity_style(1.f, .25f);
            auto target = before;
            target.opacity = 0.f;
            observe(fixture.runtime, node, before, target, 100);
            const int64_t now = boundary == 0 ? 350 : boundary == 1 ? 1350 : 1500;
            if (tick_first) aether_transition_runtime_tick(fixture.runtime, now);
            auto cancelled = target;
            cancelled.transition_properties = 0;
            observe(fixture.runtime, node, target, cancelled, now);
            check_no_opacity(fixture.runtime, node);
            require(aether_transition_runtime_tick(fixture.runtime, now) == AETHER_FALSE,
                "boundary: cancelled channel must stay inactive");
            require(aether_transition_runtime_tick(fixture.runtime, 10000) == AETHER_FALSE,
                "boundary: subsequent tick must stay inactive");
            check_no_opacity(fixture.runtime, node);
        }
    }
    RuntimeFixture fixture;
    const auto before = opacity_style(1.f, -2.f);
    const auto target = opacity_style(0.f, -2.f);
    observe(fixture.runtime, node, before, target, 0);
    require(aether_transition_runtime_has_active(fixture.runtime) == AETHER_FALSE,
        "negative delay: complete-at-install entry must not be active");
    check_opacity(fixture.runtime, node, 0.f, 0.f);
    auto cancelled = target;
    cancelled.transition_duration = 0.f;
    observe(fixture.runtime, node, target, cancelled, 0);
    check_no_opacity(fixture.runtime, node);
}

static void test_opacity_cancellation_recovery(aether_node_t node) {
    RuntimeFixture fixture;
    for (int cycle = 0; cycle < 128; ++cycle) {
        const int64_t base = cycle * 10000;
        const auto before = opacity_style(1.f);
        const auto target = opacity_style(0.f);
        observe(fixture.runtime, node, before, target, base);
        aether_transition_runtime_tick(fixture.runtime, base + (cycle % 2 ? 1000 : 500));
        auto cancelled = target;
        cancelled.opacity = .25f;
        cancelled.transition_properties = 0;
        observe(fixture.runtime, node, target, cancelled, base + 1000);
        check_no_opacity(fixture.runtime, node);
        auto reenabled = cancelled;
        reenabled.transition_properties = AETHER_TRANSITION_PROPERTY_OPACITY;
        observe(fixture.runtime, node, cancelled, reenabled, base + 1000);
        check_no_opacity(fixture.runtime, node);
        require(aether_transition_runtime_has_active(fixture.runtime) == AETHER_FALSE,
            "recovery: enabling unchanged value must not revive a channel");
        auto next = reenabled;
        next.opacity = .75f;
        observe(fixture.runtime, node, reenabled, next, base + 2000);
        check_opacity(fixture.runtime, node, .25f, 0.f);
        require(aether_transition_runtime_tick(fixture.runtime, base + 2500) == AETHER_TRUE,
            "recovery: new transition must be active halfway");
        check_opacity(fixture.runtime, node, .5f);
        require(aether_transition_runtime_tick(fixture.runtime, base + 3000) == AETHER_FALSE,
            "recovery: new transition must finish normally");
        check_opacity(fixture.runtime, node, .75f, 0.f);
        auto stopped = next;
        stopped.transition_properties = 0;
        observe(fixture.runtime, node, next, stopped, base + 3000);
        check_no_opacity(fixture.runtime, node);
    }
}

static void test_opacity_cancellation_isolation(aether_node_t node, aether_node_t other) {
    for (int finished_transform = 0; finished_transform < 2; ++finished_transform) {
        for (int zero_duration = 0; zero_duration < 2; ++zero_duration) {
            RuntimeFixture fixture;
            auto before = opacity_style(1.f);
            before.transition_properties |= AETHER_TRANSITION_PROPERTY_TRANSFORM;
            before.transforms = make_scale_list(1.f);
            auto target = before;
            target.opacity = 0.f;
            target.transforms = make_scale_list(2.f);
            observe(fixture.runtime, node, before, target, 0);
            const auto other_before = opacity_style(1.f);
            const auto other_target = opacity_style(0.f);
            const int64_t start_other = finished_transform ? 1000 : 0;
            if (finished_transform) {
                require(aether_transition_runtime_tick(fixture.runtime, 1000) == AETHER_FALSE,
                    "isolation: completed channels must not count as active");
            }
            observe(fixture.runtime, other, other_before, other_target, start_other);
            const int64_t now = start_other + 500;
            aether_transition_runtime_tick(fixture.runtime, now);
            auto cancelled = target;
            cancelled.transition_properties = AETHER_TRANSITION_PROPERTY_TRANSFORM;
            if (zero_duration) cancelled.transition_duration = 0.f;
            observe(fixture.runtime, node, target, cancelled, now);
            check_no_opacity(fixture.runtime, node);
            check_opacity(fixture.runtime, other, .5f);
            aether_transform_matrix matrix{};
            require(aether_transition_runtime_transform_override(fixture.runtime, node, &matrix) ==
                (zero_duration ? AETHER_FALSE : AETHER_TRUE),
                "isolation: transform eligibility must be independent of opacity mask");
            if (!zero_duration) require(near(matrix.m11, finished_transform ? 2.f : 1.5f),
                "isolation: transform sample must be preserved");
            require(aether_transition_runtime_has_active(fixture.runtime) == AETHER_TRUE,
                "isolation: other node must keep frames active");
            aether_transition_runtime_tick(fixture.runtime, now + 250);
            check_opacity(fixture.runtime, other, .25f);
            if (!zero_duration) {
                require(aether_transition_runtime_transform_override(fixture.runtime, node, &matrix) == AETHER_TRUE,
                    "isolation: transform override lost after tick");
                require(near(matrix.m11, finished_transform ? 2.f : 1.75f),
                    "isolation: transform must keep its original clock");
            }
            require(aether_transition_runtime_tick(fixture.runtime, now + 500) == AETHER_FALSE,
                "isolation: survivors must finish at their original deadlines");
            check_no_opacity(fixture.runtime, node);
            check_opacity(fixture.runtime, other, 0.f, 0.f);
            if (!zero_duration) {
                require(aether_transition_runtime_transform_override(fixture.runtime, node, &matrix) == AETHER_TRUE &&
                    matrix.m11 == 2.f, "isolation: retained transform endpoint must be exact");
            }
            aether_computed_style_free(&before);
            aether_computed_style_free(&target);
        }
    }
}

static void test_opacity_unchanged_eligible(aether_node_t node) {
    for (int state = 0; state < 3; ++state) {
        RuntimeFixture fixture;
        const auto before = opacity_style(1.f, .25f);
        const auto target = opacity_style(0.f, .25f);
        observe(fixture.runtime, node, before, target, 0);
        const int64_t now = state == 0 ? 100 : state == 1 ? 750 : 1250;
        aether_transition_runtime_tick(fixture.runtime, now);
        auto unchanged = target;
        unchanged.font_size += 1.f;
        unchanged.transition_duration = 3.f;
        unchanged.transition_delay = 2.f;
        unchanged.transition_timing = AETHER_TRANSITION_TIMING_EASE_IN;
        observe(fixture.runtime, node, target, unchanged, now);
        check_opacity(fixture.runtime, node, state == 0 ? 1.f : state == 1 ? .5f : 0.f);
        require(aether_transition_runtime_has_active(fixture.runtime) ==
            (state == 2 ? AETHER_FALSE : AETHER_TRUE), "eligible: unchanged value must keep original activity");
        if (state < 2) {
            require(aether_transition_runtime_tick(fixture.runtime, 1000) == AETHER_TRUE,
                "eligible: unchanged value must not change clock");
            check_opacity(fixture.runtime, node, .25f);
        }
        require(aether_transition_runtime_tick(fixture.runtime, 1250) == AETHER_FALSE,
            "eligible: unchanged value must keep original endpoint time");
        check_opacity(fixture.runtime, node, 0.f, 0.f);
        auto tiny_change = unchanged;
        tiny_change.opacity = .00005f;
        observe(fixture.runtime, node, unchanged, tiny_change, 1300);
        check_opacity(fixture.runtime, node, 0.f, 0.f);
        require(aether_transition_runtime_has_active(fixture.runtime) == AETHER_FALSE,
            "epsilon: sub-epsilon eligible difference must not create a new transition");
    }
}

static void test_opacity_interruption(aether_node_t node) {
    const float delays[] = {0.f, .25f, -.25f, -2.f};
    for (float delay : delays) {
        RuntimeFixture fixture;
        const auto before = opacity_style(1.f);
        const auto target = opacity_style(0.f);
        observe(fixture.runtime, node, before, target, 0);
        auto next = opacity_style(1.f, delay);
        // No tick at 500: observe must sample the interrupted fade itself.
        observe(fixture.runtime, node, target, next, 500);
        check_opacity(fixture.runtime, node, delay < -1.f ? 1.f : delay < 0.f ? .625f : .5f);
        if (delay > -1.f) {
            const int64_t start = 500 + static_cast<int64_t>(delay * 1000.f);
            require(aether_transition_runtime_tick(fixture.runtime, start + 500) == AETHER_TRUE,
                "interruption: replacement must remain active halfway");
            check_opacity(fixture.runtime, node, .75f);
            require(aether_transition_runtime_tick(fixture.runtime, start + 1000) == AETHER_FALSE,
                "interruption: replacement must complete on schedule");
        } else require(aether_transition_runtime_has_active(fixture.runtime) == AETHER_FALSE,
            "interruption: sufficiently negative delay must finish at installation");
        check_opacity(fixture.runtime, node, 1.f, 0.f);
    }
    RuntimeFixture fixture;
    auto before = opacity_style(1.f);
    auto target = opacity_style(0.f);
    target.transition_timing = AETHER_TRANSITION_TIMING_EASE_IN;
    observe(fixture.runtime, node, before, target, 0);
    auto next = opacity_style(1.f);
    next.transition_timing = AETHER_TRANSITION_TIMING_EASE_OUT;
    observe(fixture.runtime, node, target, next, 500);
    const float from = 1.f - .315357f; // CSS cubic-bezier(.42, 0, 1, 1) at p=.5.
    check_opacity(fixture.runtime, node, from, .002f);
    aether_transition_runtime_tick(fixture.runtime, 1000);
    check_opacity(fixture.runtime, node, from + (1.f - from) * .684643f, .002f);
    require(aether_transition_runtime_tick(fixture.runtime, 1500) == AETHER_FALSE,
        "nonlinear interruption: replacement must finish");
    check_opacity(fixture.runtime, node, 1.f, 0.f);
}

static void test_opacity_invalid_arguments(aether_node_t node, aether_node_t other) {
    RuntimeFixture fixture;
    const auto before = opacity_style(1.f);
    auto target = opacity_style(0.f);
    observe(fixture.runtime, node, before, target, 0);
    aether_transition_runtime_tick(fixture.runtime, 500);
    auto cancelled = target;
    cancelled.transition_properties = 0;
    require(aether_transition_runtime_observe(nullptr, node, &target, &cancelled, 700) == AETHER_ERROR_NULL_ARG,
        "null: runtime observe must reject null");
    require(aether_transition_runtime_observe(fixture.runtime, nullptr, &target, &cancelled, 700) == AETHER_ERROR_NULL_ARG,
        "null: node observe must reject null");
    require(aether_transition_runtime_observe(fixture.runtime, node, nullptr, &cancelled, 700) == AETHER_ERROR_NULL_ARG,
        "null: before observe must reject null");
    require(aether_transition_runtime_observe(fixture.runtime, node, &target, nullptr, 700) == AETHER_ERROR_NULL_ARG,
        "null: after observe must reject null");
    check_opacity(fixture.runtime, node, .5f);
    require(aether_transition_runtime_has_active(fixture.runtime) == AETHER_TRUE,
        "null: failed observations must preserve prior activity");
    float sentinel = -9.f;
    require(aether_transition_runtime_opacity_override(nullptr, node, &sentinel) == AETHER_FALSE && sentinel == -9.f,
        "null: runtime lookup must preserve output");
    require(aether_transition_runtime_opacity_override(fixture.runtime, nullptr, &sentinel) == AETHER_FALSE && sentinel == -9.f,
        "null: node lookup must preserve output");
    require(aether_transition_runtime_opacity_override(fixture.runtime, node, nullptr) == AETHER_FALSE,
        "null: output lookup must reject null");
    check_no_opacity(fixture.runtime, other);
    check_opacity(fixture.runtime, node, .5f);
    require(aether_transition_runtime_tick(fixture.runtime, 1000) == AETHER_FALSE,
        "null: valid state must still finish normally");
    check_opacity(fixture.runtime, node, 0.f, 0.f);
}

static void test_shorthand_roundtrip();

#include "transition_time_cases.h"
#include "transition_endpoint_cases.h"

int main() {
    aether_doc_t doc = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(doc, "<div id=\"a\"></div><div id=\"b\"></div>") == AETHER_OK,
        "document parsing failed");
    aether_node_t node_a = aether_doc_get_element_by_id(doc, "a");
    aether_node_t node_b = aether_doc_get_element_by_id(doc, "b");
    require(node_a != nullptr && node_b != nullptr, "element lookup failed");

    transition_time_cases::run(node_a, node_b);
    transition_endpoint_cases::run(node_a);
    test_opacity_linear(node_a, node_b);
    test_transform_linear(node_a);
    test_no_transition(node_a);
    test_delay(node_a);
    test_ease_in(node_a);
    test_opacity_cancellation_matrix(node_a);
    test_opacity_cancellation_boundaries(node_a);
    test_opacity_cancellation_recovery(node_a);
    test_opacity_cancellation_isolation(node_a, node_b);
    test_opacity_unchanged_eligible(node_a);
    test_opacity_interruption(node_a);
    test_opacity_invalid_arguments(node_a, node_b);

    aether_doc_destroy(doc);

    test_shorthand_roundtrip();
    test_display_list_transform_override();
    test_display_list_opacity_cancellation();

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
