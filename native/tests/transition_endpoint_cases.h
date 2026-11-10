#ifndef AETHER_TRANSITION_ENDPOINT_CASES_H
#define AETHER_TRANSITION_ENDPOINT_CASES_H

#include <array>
#include <bit>
#include <cfenv>
#include <limits>

// Public-API checks, including all six matrix coefficients, stay live in Release.
namespace transition_endpoint_cases {
constexpr int opacity = AETHER_TRANSITION_PROPERTY_OPACITY;
constexpr int transform = AETHER_TRANSITION_PROPERTY_TRANSFORM;
constexpr int both = opacity | transform;
constexpr int64_t lo = std::numeric_limits<int64_t>::min();
constexpr int64_t hi = std::numeric_limits<int64_t>::max();
constexpr float large = std::numeric_limits<float>::max();
constexpr aether_transform_matrix from{.9f, .7f, -.9f, -.7f, 1000000.9f, -1000000.9f};
constexpr aether_transform_matrix to{.1f, .2f, -.1f, -.2f, .1f, -.1f};
constexpr aether_transform_matrix extreme_from{large, -large, large, -large, large, -large};
constexpr aether_transform_matrix extreme_to{-large, large, -large, large, -large, large};
constexpr int timings[] = {AETHER_TRANSITION_TIMING_LINEAR, AETHER_TRANSITION_TIMING_EASE,
    AETHER_TRANSITION_TIMING_EASE_IN, AETHER_TRANSITION_TIMING_EASE_OUT,
    AETHER_TRANSITION_TIMING_EASE_IN_OUT};

struct Style {
    aether_computed_style_t value = aether_computed_style_default();
    aether_transform_operation operation{};
    Style(int props, float o, aether_transform_matrix m, int timing,
          float duration = 1.f, float delay = 0.f) {
        value.opacity = o;
        value.transition_properties = props;
        value.transition_timing = timing;
        value.transition_duration = duration;
        value.transition_delay = delay;
        value.transform_origin = {aether_css_length_zero(), aether_css_length_zero()};
        operation.kind = AETHER_TRANSFORM_MATRIX;
        operation.matrix_a = m.m11; operation.matrix_b = m.m12;
        operation.matrix_c = m.m21; operation.matrix_d = m.m22;
        operation.matrix_e = m.dx; operation.matrix_f = m.dy;
        // The default style owns no allocations; this input list is borrowed.
        value.transforms = {&operation, 1};
    }
    Style(const Style&) = delete;
    Style& operator=(const Style&) = delete;
};

static std::array<float, 6> coefficients(aether_transform_matrix m) {
    return {m.m11, m.m12, m.m21, m.m22, m.dx, m.dy};
}
static void exact(float actual, float expected, const char* message) {
    require(std::bit_cast<uint32_t>(actual) == std::bit_cast<uint32_t>(expected), message);
}
static void check(aether_transition_runtime_t runtime, aether_node_t node, int props,
                  float o, aether_transform_matrix m, bool present = true) {
    float actual = -9.f;
    aether_transform_matrix matrix{};
    const bool has_o = aether_transition_runtime_opacity_override(runtime, node, &actual);
    const bool has_m = aether_transition_runtime_transform_override(runtime, node, &matrix);
    require(has_o == (present && (props & opacity)), "endpoint: opacity presence");
    require(has_m == (present && (props & transform)), "endpoint: matrix presence");
    if (has_o) exact(actual, o, "endpoint: opacity bits must match");
    if (has_m) {
        const auto actual_values = coefficients(matrix), expected = coefficients(m);
        for (size_t i = 0; i < actual_values.size(); ++i)
            exact(actual_values[i], expected[i], "endpoint: matrix coefficient bits must match");
    }
}
static void active(aether_transition_runtime_t runtime, bool expected) {
    require(aether_transition_runtime_has_active(runtime) == (expected ? AETHER_TRUE : AETHER_FALSE),
        "endpoint: integer completion state");
}
static void tick(aether_transition_runtime_t runtime, int64_t now, bool expected) {
    require(aether_transition_runtime_tick(runtime, now) == (expected ? AETHER_TRUE : AETHER_FALSE),
        "endpoint: integer tick completion");
    active(runtime, expected);
}
static void install(aether_transition_runtime_t runtime, aether_node_t node,
                    const Style& a, const Style& b, int64_t now,
                    aether_status_t status = AETHER_OK) {
    require(aether_transition_runtime_observe(runtime, node, &a.value, &b.value, now) == status,
        "endpoint: observe status");
}

static void boundaries(aether_node_t node, int props, int timing) {
    for (bool extremes : {false, true}) for (float delay : {0.f, .125f, -.125f}) {
        const auto a_matrix = extremes ? extreme_from : from;
        const auto b_matrix = extremes ? extreme_to : to;
        const int64_t delay_ms = delay > 0 ? 125 : delay < 0 ? -125 : 0;
        for (int64_t epoch : {lo + 126, int64_t{0}, hi - 1125}) {
            RuntimeFixture f;
            Style a(props, .9f, a_matrix, timing), b(props, .1f, b_matrix, timing, 1.f, delay);
            install(f.runtime, node, a, b, epoch);
            active(f.runtime, true);
            if (delay >= 0) check(f.runtime, node, props, .9f, a_matrix);
            tick(f.runtime, epoch + delay_ms - 1, true);
            check(f.runtime, node, props, .9f, a_matrix);
            tick(f.runtime, epoch + delay_ms, true);
            check(f.runtime, node, props, .9f, a_matrix);
            tick(f.runtime, epoch + delay_ms + 1000, false);
            check(f.runtime, node, props, .1f, b_matrix);
            tick(f.runtime, hi, false);
            check(f.runtime, node, props, .1f, b_matrix);
            tick(f.runtime, lo, false);
            check(f.runtime, node, props, .1f, b_matrix);
        }
        // Installation with an exhausted negative delay must copy the endpoint.
        RuntimeFixture f;
        Style a(props, .9f, a_matrix, timing), b(props, .1f, b_matrix, timing, 1.f, -2.f);
        install(f.runtime, node, a, b, lo);
        active(f.runtime, false);
        check(f.runtime, node, props, .1f, b_matrix);
    }
    // Opacity also preserves a signed zero, without a tolerance or an arithmetic lerp.
    if (props & opacity) {
        RuntimeFixture f;
        Style a(opacity, -0.f, from, timing), b(opacity, .9f, to, timing);
        install(f.runtime, node, a, b, 0);
        check(f.runtime, node, opacity, -0.f, from);
        Style c(opacity, -0.f, from, timing);
        install(f.runtime, node, b, c, 1000);
        tick(f.runtime, 2000, false);
        check(f.runtime, node, opacity, -0.f, from);
    }
}

static void long_duration(aether_node_t node, int props, int timing) {
    // In round-to-nearest, D-1 and D collapse to the same binary32 progress.
    // Exercise the other modes as well; none can decide lifetime from progress.
    const int original = std::fegetround();
    require(original != -1, "endpoint: rounding mode available");
    for (int mode : {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO}) {
        require(std::fesetround(mode) == 0, "endpoint: set rounding mode");
        for (float seconds : {32768.f, 0x1.0624dcp53f}) {
            volatile float input = seconds;
            const int64_t duration = static_cast<int64_t>(input * 1000.f);
            for (float delay : {0.f, .125f, -.125f})
            for (int64_t epoch : {lo + 125, int64_t{0}, hi - duration - 125}) {
                const int64_t delay_ms = delay > 0 ? 125 : delay < 0 ? -125 : 0;
                RuntimeFixture f;
                Style a(props, .9f, from, timing), b(props, .1f, to, timing, input, delay);
                install(f.runtime, node, a, b, epoch);
                tick(f.runtime, epoch + duration + delay_ms - 1, true);
                // A premature finished entry would not follow this rewind.
                tick(f.runtime, epoch + delay_ms, true);
                check(f.runtime, node, props, .9f, from);
                tick(f.runtime, epoch + duration + delay_ms, false);
                check(f.runtime, node, props, .1f, to);
                tick(f.runtime, lo, false);
                check(f.runtime, node, props, .1f, to);
            }
        }
    }
    require(std::fesetround(FE_TONEAREST) == 0, "endpoint: nearest for negative delay witness");
    // Float duration truncates to 20000004ms; double delay truncates to
    // -20000003ms. Installation is at D-1, although its float progress is 1.
    volatile float seconds = 20000.004f;
    require(static_cast<int64_t>(seconds * 1000.f) == 20000004 &&
        static_cast<int64_t>(static_cast<double>(-seconds) * 1000.) == -20000003,
        "endpoint: negative delay witness conversions");
    for (int64_t epoch : {lo, int64_t{0}, hi - 1}) {
        RuntimeFixture f;
        Style a(props, .9f, from, timing), b(props, .1f, to, timing, seconds, -seconds);
        install(f.runtime, node, a, b, epoch);
        active(f.runtime, true);
        tick(f.runtime, epoch, true);
        tick(f.runtime, epoch + 1, false);
        check(f.runtime, node, props, .1f, to);
        tick(f.runtime, lo, false);
        check(f.runtime, node, props, .1f, to);
    }
    require(std::fesetround(original) == 0, "endpoint: restore rounding mode");
}

static void interruption(aether_node_t node, int props, int timing) {
    for (int64_t epoch : {lo, int64_t{0}, hi - 2125}) {
        for (int64_t offset : {int64_t{0}, int64_t{125}, int64_t{1125}}) {
            RuntimeFixture f;
            Style a(props, .9f, extreme_from, timing);
            Style b(props, .1f, extreme_to, timing, 1.f, .125f);
            Style c(props, .7f, from, timing);
            install(f.runtime, node, a, b, epoch);
            // No tick: interruption and transform preflight sample the boundary.
            install(f.runtime, node, b, c, epoch + offset);
            active(f.runtime, true);
            check(f.runtime, node, props, offset == 1125 ? .1f : .9f,
                  offset == 1125 ? extreme_to : extreme_from);
            tick(f.runtime, epoch + offset + 1000, false);
            check(f.runtime, node, props, .7f, from);
        }
    }
    // Retargeting an active transform to its exact sampled endpoint needs no
    // installation, even with invalid unused times. Opacity retains its own rule.
    RuntimeFixture f;
    Style a(transform, .9f, extreme_from, timing), b(transform, .1f, extreme_to, timing);
    Style c(transform, .1f, extreme_to, timing, std::numeric_limits<float>::infinity());
    c.value.transition_delay = std::numeric_limits<float>::quiet_NaN();
    install(f.runtime, node, a, b, 0);
    install(f.runtime, node, a, c, 1000);
    active(f.runtime, false);
    check(f.runtime, node, transform, 0.f, to, false);

    // Rejected preflight must not persist even an exact endpoint sample.
    RuntimeFixture g;
    Style start(both, .9f, extreme_from, timing), end(both, .1f, extreme_to, timing);
    Style bad(both, .7f, from, timing, std::numeric_limits<float>::infinity());
    install(g.runtime, node, start, end, 0);
    install(g.runtime, node, end, bad, 1000, AETHER_ERROR_INVALID_STATE);
    active(g.runtime, true);
    check(g.runtime, node, both, .9f, extreme_from);
    tick(g.runtime, 1000, false);
    check(g.runtime, node, both, .1f, extreme_to);
}

static void run(aether_node_t node) {
    for (int props : {opacity, transform, both}) for (int timing : timings) {
        boundaries(node, props, timing);
        long_duration(node, props, timing);
        interruption(node, props, timing);
    }
}
} // namespace transition_endpoint_cases
#endif
