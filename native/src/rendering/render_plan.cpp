#include <aether/aether.h>

namespace {

constexpr uint32_t k_layout_invalidation =
    AETHER_VISUAL_INVALIDATION_LAYOUT;
constexpr uint32_t k_display_list_invalidation =
    AETHER_VISUAL_INVALIDATION_DISPLAY_LIST;
constexpr uint32_t k_paint_chunk_invalidation =
    AETHER_VISUAL_INVALIDATION_PAINT_CHUNKS;
constexpr uint32_t k_scroll_invalidation =
    AETHER_VISUAL_INVALIDATION_SCROLL;

constexpr uint32_t k_style_impact_layout =
    AETHER_STYLE_IMPACT_LAYOUT | AETHER_STYLE_IMPACT_TEXT_LAYOUT;
constexpr uint32_t k_style_impact_paint =
    AETHER_STYLE_IMPACT_PAINT | AETHER_STYLE_IMPACT_COMPOSITE;

bool is_true(aether_bool_t value) {
    return value != AETHER_FALSE;
}

} // namespace

extern "C" {

AETHER_API uint32_t aether_visual_invalidation_from_style_impact(uint32_t impact) {
    if ((impact & k_style_impact_layout) != 0u) {
        return k_layout_invalidation;
    }
    if ((impact & k_style_impact_paint) != 0u) {
        return k_paint_chunk_invalidation;
    }
    return AETHER_VISUAL_INVALIDATION_NONE;
}

AETHER_API uint32_t aether_visual_invalidation_for_scroll(aether_bool_t display_list_contains_patched_chunks) {
    uint32_t invalidation = k_scroll_invalidation;
    if (is_true(display_list_contains_patched_chunks)) {
        invalidation |= k_display_list_invalidation;
    }
    return invalidation;
}

AETHER_API aether_status_t aether_render_plan_evaluate(
    const aether_render_plan_input_t* input,
    aether_render_plan_t* out_plan)
{
    if (!input || !out_plan) return AETHER_ERROR_NULL_ARG;

    uint32_t normalized = input->invalidation;
    if (is_true(input->markup_dirty)) {
        normalized |= AETHER_VISUAL_INVALIDATION_STYLE;
    }

    const bool display_list_available = is_true(input->display_list_available);
    const bool has_pending_pointer_move = is_true(input->has_pending_pointer_move);
    const bool markup_dirty = is_true(input->markup_dirty);
    const bool renderer_supports_incremental = is_true(input->renderer_supports_incremental);

    const bool requires_style = (normalized & AETHER_VISUAL_INVALIDATION_STYLE) != 0u;
    const bool requires_layout = (normalized & AETHER_VISUAL_INVALIDATION_LAYOUT) != 0u;
    const bool requires_display_list = (normalized & AETHER_VISUAL_INVALIDATION_DISPLAY_LIST) != 0u;
    const bool requires_paint_chunks = (normalized & AETHER_VISUAL_INVALIDATION_PAINT_CHUNKS) != 0u;
    const bool scroll_invalidated = (normalized & AETHER_VISUAL_INVALIDATION_SCROLL) != 0u;

    const bool scroll_only =
        !has_pending_pointer_move &&
        !markup_dirty &&
        normalized == AETHER_VISUAL_INVALIDATION_SCROLL;

    const bool pointer_only =
        has_pending_pointer_move &&
        !markup_dirty &&
        normalized == AETHER_VISUAL_INVALIDATION_NONE;

    const bool allow_incremental =
        renderer_supports_incremental &&
        display_list_available &&
        !scroll_invalidated &&
        !scroll_only &&
        normalized == AETHER_VISUAL_INVALIDATION_NONE;

    out_plan->normalized_invalidation = normalized;
    out_plan->requires_style = requires_style ? AETHER_TRUE : AETHER_FALSE;
    out_plan->requires_layout = requires_layout ? AETHER_TRUE : AETHER_FALSE;
    out_plan->requires_display_list = requires_display_list ? AETHER_TRUE : AETHER_FALSE;
    out_plan->requires_paint_chunks = requires_paint_chunks ? AETHER_TRUE : AETHER_FALSE;
    out_plan->pointer_only_render = pointer_only ? AETHER_TRUE : AETHER_FALSE;
    out_plan->scroll_only_render = scroll_only ? AETHER_TRUE : AETHER_FALSE;
    out_plan->allow_incremental_render = allow_incremental ? AETHER_TRUE : AETHER_FALSE;
    out_plan->use_scroll_composition =
        (scroll_only && display_list_available) ? AETHER_TRUE : AETHER_FALSE;

    return AETHER_OK;
}

} // extern "C"
