#include "intrinsic_sizing.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool same(float actual, float expected) { return std::isfinite(actual) && std::isfinite(expected) && std::fabs(actual - expected) < .001f; }

int main() {
    require(same(aether_intrinsic_min_from_unbreakable_fragments({50.f, 80.f, 30.f}), 80.f),
        "min-content must be the widest unbreakable fragment, not the whole text run");
    require(same(aether_intrinsic_min_from_unbreakable_fragments({}), 0.f),
        "empty text must have a zero min-content contribution");

    const std::vector<aether_inline_intrinsic_fragment_s> mixed_inline{
        {aether_inline_fragment_kind_e::word, 20.f},
        {aether_inline_fragment_kind_e::whitespace, 4.f},
        {aether_inline_fragment_kind_e::atomic, 40.f},
        {aether_inline_fragment_kind_e::whitespace, 4.f},
        {aether_inline_fragment_kind_e::word, 30.f},
    };
    const auto mixed_sizes = aether_measure_inline_intrinsic_sizes(mixed_inline);
    require(same(mixed_sizes.min_content, 40.f) && same(mixed_sizes.max_content, 98.f),
        "inline intrinsic sizing must retain text, whitespace, and atomic fragments in DOM order");
    const auto hard_break_sizes = aether_measure_inline_intrinsic_sizes({
        {aether_inline_fragment_kind_e::word, 60.f},
        {aether_inline_fragment_kind_e::forced_break, 0.f},
        {aether_inline_fragment_kind_e::word, 30.f},
    });
    require(same(hard_break_sizes.min_content, 60.f) && same(hard_break_sizes.max_content, 60.f),
        "a forced line break must split max-content lines while preserving each unbreakable minimum");

    const std::vector<float> cards{100.f, 70.f, 90.f};
    for (bool column : {false, true}) for (bool wrap : {false, true})
        for (auto mode : {aether_intrinsic_size_mode_e::min_content,
                          aether_intrinsic_size_mode_e::max_content}) {
            const float expected = column || (wrap && mode == aether_intrinsic_size_mode_e::min_content)
                ? 100.f : 300.f;
            require(same(aether_intrinsic_flex_main_contribution(cards, column, wrap, 20.f, mode), expected),
                    "row/column, wrap/nowrap, min/max finite matrix");
            require(same(aether_intrinsic_flex_main_contribution({}, column, wrap, 20.f, mode), 0), "empty has no gap");
            require(same(aether_intrinsic_flex_main_contribution({100}, column, wrap, 20.f, mode), 100), "singleton has no gap");
        }
    for (float gap : {0.f, -20.f})
        require(same(aether_intrinsic_flex_main_contribution(cards, false, true, gap,
            aether_intrinsic_size_mode_e::max_content), 260), "zero and negative gap policy");
    require(same(aether_intrinsic_flex_main_contribution({10.25f,20.5f}, false, true, 1.25f,
        aether_intrinsic_size_mode_e::max_content), 32), "fractional wrapping maximum");
    require(same(aether_intrinsic_flex_main_contribution({10.25f,20.5f}, false, true, 1.25f,
        aether_intrinsic_size_mode_e::min_content), 20.5f), "fractional wrapping minimum");
    return 0;
}
