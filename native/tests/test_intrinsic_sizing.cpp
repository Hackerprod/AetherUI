#include "intrinsic_sizing.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool same(float actual, float expected) { return std::fabs(actual - expected) < .001f; }

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
    require(same(aether_intrinsic_flex_main_contribution(cards, false, false, 20.f,
                                                          aether_intrinsic_size_mode_e::min_content), 300.f),
        "a single-line row flex min-content contribution must sum children because it cannot wrap them");
    require(same(aether_intrinsic_flex_main_contribution(cards, false, false, 20.f,
                                                          aether_intrinsic_size_mode_e::max_content), 300.f),
        "a single-line row flex max-content contribution must sum children and gaps");
    require(same(aether_intrinsic_flex_main_contribution(cards, false, true, 20.f,
                                                          aether_intrinsic_size_mode_e::max_content), 100.f),
        "a wrapping row flex container must allow later children to form another line");
    require(same(aether_intrinsic_flex_main_contribution(cards, true, false, 20.f,
                                                          aether_intrinsic_size_mode_e::max_content), 100.f),
        "a column flex container's horizontal contribution must be its widest child");
    return 0;
}
