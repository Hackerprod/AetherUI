#include "../src/layout/flex_resolver.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool same(float actual, float expected) {
    return std::fabs(actual - expected) < 0.001f;
}

static aether_flex_item_input_s item(float base, float grow = 0.f, float shrink = 1.f,
                                      float minimum = 0.f,
                                      float maximum = std::numeric_limits<float>::quiet_NaN()) {
    aether_flex_item_input_s value{};
    value.basis = { AETHER_FLEX_BASIS_LENGTH, NAN, 0.f, base, true };
    value.flex_grow = grow;
    value.flex_shrink = shrink;
    value.min_main_size = minimum;
    value.max_main_size = maximum;
    return value;
}

static aether_flex_resolve_result_s resolve(std::vector<aether_flex_item_input_s>& items,
                                            float available, bool wrap = false, float gap = 0.f) {
    return aether_resolve_flexible_lengths({ &items, available, gap, wrap });
}

// Keep the regression group non-fatal until every scenario has run, so the
// unmodified resolver demonstrates each failure rather than only the first.
static int fractional_failures = 0;

static void check_case(const char* name, std::vector<aether_flex_item_input_s> items,
                       float available, const std::vector<float>& expected,
                       bool wrap = false, float gap = 0.f,
                       const std::vector<std::vector<size_t>>& expected_lines = {}) {
    const auto result = resolve(items, available, wrap, gap);
    bool passed = result.items.size() == expected.size();
    if (passed) {
        for (size_t index = 0; index < expected.size(); ++index) {
            const auto& actual = result.items[index];
            passed &= std::isfinite(actual.target_main_size) && actual.target_main_size >= 0.f;
            passed &= same(actual.target_main_size, expected[index]);
            passed &= actual.flex_base_size == items[index].basis.resolved_length;
            passed &= actual.frozen;
        }
    }
    if (!expected_lines.empty()) {
        passed &= result.lines.size() == expected_lines.size();
        if (result.lines.size() == expected_lines.size()) {
            for (size_t index = 0; index < expected_lines.size(); ++index) {
                passed &= result.lines[index].item_indices == expected_lines[index];
            }
        }
    }
    if (!passed) {
        ++fractional_failures;
        std::fprintf(stderr, "FAIL: %s; targets:", name);
        for (const auto& actual : result.items) std::fprintf(stderr, " %.9g", actual.target_main_size);
        std::fprintf(stderr, "; expected:");
        for (const float value : expected) std::fprintf(stderr, " %.9g", value);
        std::fprintf(stderr, "\n");
    }
}

static void test_fractional_contract() {
    // The eight original reproducers. Residual free space/overflow is intended.
    check_case("partial shrink equal", {item(100, 0, .25f), item(100, 0, .25f)}, 100, {75, 75});
    check_case("partial shrink weighted", {item(100, 0, .25f), item(200, 0, .25f)}, 150, {75, 150});
    check_case("single .9 shrink", {item(550, 0, .9f)}, 50, {100});
    check_case("two partial items", {item(75, 0, .25f), item(75, 0, .25f)}, 50, {50, 50});
    check_case("minimum freezes after partial shrink", {item(100, 0, .25f, 90), item(100, 0, .25f)}, 100, {90, 75});
    check_case("minimum freezes before initial budget", {item(50, 0, 1, 100), item(100, 0, .25f)}, 100, {100, 75});
    check_case("grow zero factor freezes before initial budget", {item(100, 0, 1, 0, 50), item(50, .5f)}, 200, {50, 100});
    check_case("zero base still contributes shrink factor", {item(0, 0, .25f), item(100, 0, .25f)}, 50, {0, 75});

    check_case("zero shrink sum", {item(100, 0, 0), item(100, 0, 0)}, 100, {100, 100});
    check_case("unit shrink sum", {item(100, 0, .5f), item(100, 0, .5f)}, 100, {50, 50});
    check_case("greater than unit shrink sum", {item(100, 0, .75f), item(100, 0, .75f)}, 100, {50, 50});
    check_case("different partial shrink factors", {item(100, 0, .25f), item(100, 0, .5f)}, 100, {75, 50});
    check_case("zero base and zero factor is inflexible", {item(0, 0, 0), item(100, 0, .25f)}, 50, {0, 87.5f});
    auto zero_weight = std::vector<aether_flex_item_input_s>{item(0, 0, .25f), item(0, 0, .25f)};
    zero_weight[0].outer_main_size = zero_weight[1].outer_main_size = 50;
    check_case("all shrink weights zero with margin overflow", zero_weight, 50, {0, 0});
    check_case("tiny grow factor is not geometric zero", {item(0, .00005f), item(0, .00005f)}, 20000, {1, 1});
    // Both expected sizes are exactly representable floats; retain absolute
    // tolerance rather than hiding visible errors with a large relative one.
    check_case("tiny shrink factor", {item(10000000, 0, .00005f), item(10000000, 0, .00005f)}, 0, {9999000, 9999000});
    check_case("no deficit exact fit", {item(100, 0, .25f), item(100, 0, .25f)}, 200, {100, 100});
    check_case("no deficit spare room", {item(100, 0, .25f), item(100, 0, .25f)}, 250, {100, 100});
    check_case("grow positive factor freezes at initial maximum", {item(100, 1, 1, 0, 50), item(50, .5f)}, 200, {50, 100});
    check_case("minimum dominates maximum", {item(100, 0, .25f, 90, 80), item(100, 0, .25f)}, 100, {90, 75});

    check_case("three shrink passes keep initial budget", {item(100, 0, .5f, 90), item(100, 0, .5f, 60), item(100, 0, .5f)}, 180, {90, 60, 40});
    check_case("three grow passes keep initial budget", {item(0, .5f, 1, 0, 10), item(0, .5f, 1, 0, 45), item(0, .5f)}, 120, {10, 45, 60});
    check_case("opposite violations only freeze total sign", {item(100, 0, 1, 90), item(100, 0, 1, 0, 40), item(100)}, 150, {90, 30, 30});
    check_case("cancelled violations freeze all", {item(100, 0, 1, 60), item(100, 0, 1, 0, 40), item(100)}, 150, {60, 40, 50});

    check_case("partial shrink counts gap once", {item(100, 0, .25f), item(100, 0, .25f)}, 100, {70, 70}, false, 20);
    auto signed_margins = std::vector<aether_flex_item_input_s>{item(100, 0, .25f), item(100, 0, .25f)};
    signed_margins[1].outer_main_size = -20;
    check_case("partial shrink keeps signed margin", signed_margins, 100, {80, 80});
    check_case("wrapped shrink isolates budget and gap", {item(200, 0, .25f), item(300, 0, .5f)}, 100, {175, 200}, true, 20, {{0}, {1}});
    check_case("wrapped grow isolates budget", {item(40, .25f), item(40, .25f), item(40, .25f)}, 100, {45, 45, 55}, true, 0, {{0, 1}, {2}});

    check_case("shrink keeps smaller remaining deficit", {item(100, 0, .25f, 0, 10), item(100, 0, .25f), item(100, 0, .25f)}, 150, {10, 70, 70});
    check_case("grow keeps smaller remaining room", {item(0, .25f, 1, 90), item(0, .25f), item(0, .25f)}, 120, {90, 15, 15});

    // 10000 +/- 1/3 rounds by more than the geometric epsilon in float.
    // Only the first item hits a real limit; the others must remain active.
    check_case("float rounding is not a maximum violation", {item(10000, 1, 1, 0, 10000), item(10000, 1), item(10000, 1)}, 30001, {10000, 10000.5f, 10000.5f});
    check_case("float rounding is not a minimum violation", {item(10000, 0, 1, 10000), item(10000), item(10000)}, 29999, {10000, 9999.5f, 9999.5f});

    // Promote weights before multiplying: the float products would overflow,
    // although the geometry and all published sizes remain representable.
    check_case("large shrink weights remain finite", {item(100, 0, 1e38f), item(200, 0, 1e38f)}, 150, {50, 100});
    check_case("large grow sum remains finite", {item(0, 2e38f), item(0, 2e38f)}, 100, {50, 50});

    std::vector<aether_flex_item_input_s> empty;
    const auto empty_result = resolve(empty, 100);
    require(empty_result.items.empty() && empty_result.lines.empty(), "empty items must produce empty output");
    const auto null_result = aether_resolve_flexible_lengths({nullptr, 100, 20, true});
    require(null_result.items.empty() && null_result.lines.empty(), "null items must produce empty output");
    auto indefinite_items = std::vector<aether_flex_item_input_s>{item(50, .25f, .25f, 100), item(100, .25f, .25f)};
    const auto indefinite = resolve(indefinite_items, NAN, true, 20);
    require(indefinite.lines.size() == 1 && same(indefinite.items[0].target_main_size, 50) &&
            same(indefinite.items[0].hypothetical_main_size, 100) &&
            same(indefinite.items[1].target_main_size, 100) &&
            !indefinite.items[0].frozen && !indefinite.items[1].frozen,
        "indefinite available size must preserve bases without distribution or wrapping");

    // Reuse exactly the same input vector across resolutions; no mutable state
    // from a previous invocation may affect a subsequent line or size.
    auto reused = std::vector<aether_flex_item_input_s>{item(100, 0, .25f), item(100, 0, .25f)};
    for (const float available : {100.f, 200.f, 50.f, 100.f}) {
        const auto result = resolve(reused, available);
        const float expected = available == 200 ? 100.f : available == 50 ? 62.5f : 75.f;
        if (!same(result.items[0].target_main_size, expected) || !same(result.items[1].target_main_size, expected)) {
            ++fractional_failures;
            std::fprintf(stderr, "FAIL: repeated resolution at %.9g\n", available);
        }
    }
    require(fractional_failures == 0, "fractional flex contract regressions must all pass");
}

int main() {
    require(same(aether_flex_resolve_base_size({ AETHER_FLEX_BASIS_AUTO, 75.f, 20.f, NAN, false }), 75.f),
        "auto flex-basis must use the definite main-size property");
    require(same(aether_flex_resolve_base_size({ AETHER_FLEX_BASIS_AUTO, NAN, 20.f, NAN, false }), 20.f),
        "auto flex-basis without a definite main-size must use content");
    require(same(aether_flex_resolve_base_size({ AETHER_FLEX_BASIS_CONTENT, 75.f, 20.f, NAN, false }), 20.f),
        "content flex-basis must ignore the main-size property");
    require(same(aether_flex_resolve_base_size({ AETHER_FLEX_BASIS_LENGTH, NAN, 20.f, 40.f, true }), 40.f),
        "a definite flex-basis length must be used directly");
    require(same(aether_flex_resolve_base_size({ AETHER_FLEX_BASIS_LENGTH, NAN, 20.f, NAN, false }), 20.f),
        "a percentage flex-basis in an indefinite container must fall back to content");
    require(same(aether_flex_resolve_automatic_minimum({ false, 80.f, 50.f, NAN, 70.f }), 50.f),
        "the automatic flex minimum must honor a smaller definite specified-size suggestion");
    require(same(aether_flex_resolve_automatic_minimum({ false, 80.f, NAN, 60.f, NAN }), 60.f),
        "the automatic flex minimum must honor a transferred-size suggestion when present");
    require(same(aether_flex_resolve_automatic_minimum({ true, 80.f, 50.f, 60.f, 70.f }), 0.f),
        "scroll containers must have a zero automatic flex minimum");

    {
        auto items = std::vector<aether_flex_item_input_s>{ item(50.f, 1.f), item(50.f, 1.f) };
        const auto result = resolve(items, 200.f);
        require(same(result.items[0].target_main_size, 100.f) && same(result.items[1].target_main_size, 100.f),
            "positive free space must distribute by flex-grow");
    }
    {
        auto items = std::vector<aether_flex_item_input_s>{ item(50.f, .25f), item(50.f, .25f) };
        const auto result = resolve(items, 200.f);
        require(same(result.items[0].target_main_size, 75.f) && same(result.items[1].target_main_size, 75.f),
            "a grow-factor sum below one must leave proportional free space undistributed");
    }
    {
        auto items = std::vector<aether_flex_item_input_s>{ item(100.f), item(200.f) };
        const auto result = resolve(items, 150.f);
        require(same(result.items[0].target_main_size, 50.f) && same(result.items[1].target_main_size, 100.f),
            "negative free space must use flex-shrink weighted by flex base size");
    }
    {
        auto first = item(0.f, 1.f, 1.f, std::numeric_limits<float>::quiet_NaN());
        auto second = item(0.f, 1.f, 1.f, std::numeric_limits<float>::quiet_NaN());
        first.automatic_minimum.content_size_suggestion = 100.f;
        second.automatic_minimum.content_size_suggestion = 70.f;
        auto items = std::vector<aether_flex_item_input_s>{ first, second };
        const auto result = resolve(items, 150.f);
        require(same(result.items[0].target_main_size, 100.f) && same(result.items[1].target_main_size, 70.f),
            "zero-basis flex items must freeze at their automatic minimum rather than collapse to zero under shrink");
    }
    {
        auto items = std::vector<aether_flex_item_input_s>{ item(100.f, 0.f, 1.f, 80.f), item(100.f) };
        const auto result = resolve(items, 100.f);
        require(same(result.items[0].target_main_size, 80.f) && same(result.items[1].target_main_size, 20.f),
            "a min-size violation must freeze that item and redistribute the remaining shrinkage");
    }
    {
        auto items = std::vector<aether_flex_item_input_s>{ item(50.f, 1.f, 1.f, 0.f, 60.f), item(50.f, 1.f) };
        const auto result = resolve(items, 200.f);
        require(same(result.items[0].target_main_size, 60.f) && same(result.items[1].target_main_size, 140.f),
            "a max-size violation must freeze that item and redistribute remaining growth");
    }
    {
        auto items = std::vector<aether_flex_item_input_s>{ item(60.f, 0.f, 1.f, 0.f, 30.f), item(70.f), item(40.f) };
        const auto result = resolve(items, 100.f, true);
        require(result.lines.size() == 2 && result.lines[0].item_indices.size() == 2 &&
                result.lines[1].item_indices.size() == 1,
            "wrapping must use clamped hypothetical sizes and keep an exact boundary on the current line");
    }
    {
        auto items = std::vector<aether_flex_item_input_s>{ item(20.f), item(20.f) };
        const auto result = resolve(items, 0.f);
        require(result.items[0].target_main_size >= 0.f && result.items[1].target_main_size >= 0.f,
            "shrink resolution must never manufacture negative main sizes");
    }
    {
        auto first = item(28.f);
        auto second = item(28.f);
        second.outer_main_size = -8.f;
        auto items = std::vector<aether_flex_item_input_s>{ first, second };
        const auto result = resolve(items, 48.f);
        require(same(result.items[0].target_main_size, 28.f) && same(result.items[1].target_main_size, 28.f),
            "negative main-axis margins must reduce outer size without shrinking fitting items");
    }
    {
        auto first = item(28.f);
        auto second = item(28.f);
        second.outer_main_size = -8.f;
        auto items = std::vector<aether_flex_item_input_s>{ first, second };
        const auto result = resolve(items, 40.f);
        require(same(result.items[0].target_main_size, 24.f) && same(result.items[1].target_main_size, 24.f),
            "negative margins must still participate in real flex shrink deficits");
    }
    {
        auto first = item(28.f);
        auto second = item(28.f);
        auto third = item(28.f);
        second.outer_main_size = -8.f;
        auto items = std::vector<aether_flex_item_input_s>{ first, second, third };
        const auto result = resolve(items, 48.f, true);
        require(result.lines.size() == 2 && result.lines[0].item_indices.size() == 2 &&
                result.lines[0].item_indices[0] == 0 && result.lines[0].item_indices[1] == 1,
            "wrapping must use signed outer margins when selecting flex lines");
    }
    {
        auto first = item(28.f);
        auto second = item(28.f);
        second.outer_main_size = 4.f;
        auto items = std::vector<aether_flex_item_input_s>{ first, second };
        const auto result = resolve(items, 60.f);
        require(same(result.items[0].target_main_size, 28.f) && same(result.items[1].target_main_size, 28.f),
            "positive main-axis margins must retain their existing outer-size behavior");
    }
    test_fractional_contract();
    return 0;
}
