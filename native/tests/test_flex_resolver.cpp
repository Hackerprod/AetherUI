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
    return 0;
}
