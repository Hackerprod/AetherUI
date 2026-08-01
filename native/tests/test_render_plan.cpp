#include <aether/aether.h>

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

aether_render_plan_t evaluate(const aether_render_plan_input_t& input) {
    aether_render_plan_t plan{};
    const aether_status_t status = aether_render_plan_evaluate(&input, &plan);
    require(status == AETHER_OK, "render plan should evaluate");
    return plan;
}

void style_impact_maps_to_minimal_invalidation() {
    require(aether_visual_invalidation_from_style_impact(AETHER_STYLE_IMPACT_PAINT) ==
                AETHER_VISUAL_INVALIDATION_PAINT_CHUNKS,
        "paint impact should invalidate paint chunks only");
    require(aether_visual_invalidation_from_style_impact(AETHER_STYLE_IMPACT_COMPOSITE) ==
                AETHER_VISUAL_INVALIDATION_PAINT_CHUNKS,
        "composite impact should invalidate paint chunks only");
    require(aether_visual_invalidation_from_style_impact(AETHER_STYLE_IMPACT_LAYOUT) ==
                AETHER_VISUAL_INVALIDATION_LAYOUT,
        "layout impact should invalidate layout");
    require(aether_visual_invalidation_from_style_impact(AETHER_STYLE_IMPACT_TEXT_LAYOUT) ==
                AETHER_VISUAL_INVALIDATION_LAYOUT,
        "text layout impact should invalidate layout");
    require(aether_visual_invalidation_from_style_impact(AETHER_STYLE_IMPACT_CURSOR |
                                                        AETHER_STYLE_IMPACT_HITTEST) ==
                AETHER_VISUAL_INVALIDATION_NONE,
        "cursor and hit-test impact should not rebuild visuals");
}

void scroll_invalidation_rebuilds_after_patched_chunks() {
    require(aether_visual_invalidation_for_scroll(AETHER_FALSE) ==
                AETHER_VISUAL_INVALIDATION_SCROLL,
        "plain scroll should invalidate scroll only");
    require(aether_visual_invalidation_for_scroll(AETHER_TRUE) ==
                (AETHER_VISUAL_INVALIDATION_SCROLL |
                 AETHER_VISUAL_INVALIDATION_DISPLAY_LIST),
        "scroll after patched chunks should rebuild canonical display list");
}

void pure_scroll_uses_scroll_composition() {
    aether_render_plan_input_t input{};
    input.invalidation = AETHER_VISUAL_INVALIDATION_SCROLL;
    input.display_list_available = AETHER_TRUE;
    input.renderer_supports_incremental = AETHER_TRUE;

    const aether_render_plan_t plan = evaluate(input);
    require(plan.scroll_only_render == AETHER_TRUE, "pure scroll should be scroll-only");
    require(plan.use_scroll_composition == AETHER_TRUE, "pure scroll should enable scroll composition");
    require(plan.allow_incremental_render == AETHER_FALSE, "pure scroll should not use paint-chunk incremental path");
}

void mixed_scroll_and_display_list_disables_fast_paths() {
    aether_render_plan_input_t input{};
    input.invalidation = AETHER_VISUAL_INVALIDATION_SCROLL |
                         AETHER_VISUAL_INVALIDATION_DISPLAY_LIST;
    input.display_list_available = AETHER_TRUE;
    input.renderer_supports_incremental = AETHER_TRUE;

    const aether_render_plan_t plan = evaluate(input);
    require(plan.scroll_only_render == AETHER_FALSE, "mixed scroll should not be scroll-only");
    require(plan.use_scroll_composition == AETHER_FALSE, "mixed scroll should not use retained scroll composition");
    require(plan.allow_incremental_render == AETHER_FALSE, "mixed scroll should not use incremental paint chunks");
    require(plan.requires_display_list == AETHER_TRUE, "mixed scroll should rebuild display list");
}

void stable_visuals_allow_incremental_renderer() {
    aether_render_plan_input_t input{};
    input.display_list_available = AETHER_TRUE;
    input.renderer_supports_incremental = AETHER_TRUE;

    const aether_render_plan_t plan = evaluate(input);
    require(plan.allow_incremental_render == AETHER_TRUE, "stable frame should allow incremental renderer");
    require(plan.use_scroll_composition == AETHER_FALSE, "stable non-scroll frame should not use scroll composition");
}

void unavailable_display_list_blocks_fast_paths() {
    aether_render_plan_input_t input{};
    input.renderer_supports_incremental = AETHER_TRUE;

    const aether_render_plan_t plan = evaluate(input);
    require(plan.allow_incremental_render == AETHER_FALSE, "missing display list blocks incremental renderer");
    require(plan.use_scroll_composition == AETHER_FALSE, "missing display list blocks scroll composition");
}

} // namespace

int main() {
    style_impact_maps_to_minimal_invalidation();
    scroll_invalidation_rebuilds_after_patched_chunks();
    pure_scroll_uses_scroll_composition();
    mixed_scroll_and_display_list_disables_fast_paths();
    stable_visuals_allow_incremental_renderer();
    unavailable_display_list_blocks_fast_paths();
    return 0;
}
