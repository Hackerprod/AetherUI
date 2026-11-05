/* ── Scroll metrics — descendant-overflow accumulation ──────────────── */
#include "layout_internal.h"

#include <algorithm>

/* Overflow classification (CSS Overflow 3.1). Three separate predicates: a
   box with a scroll-container value accumulates scrollable overflow and may
   hold programmatic offsets (auto/scroll/hidden); only auto/scroll accept
   user interaction; clip is a pure clip that never scrolls. */
bool aether_overflow_is_scroll_container(int32_t overflow) {
    return overflow == AETHER_OVERFLOW_AUTO || overflow == AETHER_OVERFLOW_SCROLL ||
           overflow == AETHER_OVERFLOW_HIDDEN;
}

bool aether_overflow_allows_user_scroll(int32_t overflow) {
    return overflow == AETHER_OVERFLOW_AUTO || overflow == AETHER_OVERFLOW_SCROLL;
}

bool aether_overflow_is_clip(int32_t overflow) {
    return overflow == AETHER_OVERFLOW_CLIP;
}

// A scroll container owns the overflow of descendants whose corresponding
// axis remains visible. A nested clipping/scrolling container terminates that
// axis' contribution, while its own border/margin box still contributes.
void accumulate_scrollable_descendant_extent(const aether_layout_box_s* parent,
                                             bool include_x,
                                             bool include_y,
                                             float& right,
                                             float& bottom) {
    for (const auto* child : parent->children) {
        // Positioned margins are algebraic spacing, not scrollable area: negative
        // margins may hide border overflow and positive margins may add empty space.
        // Text fragments can copy an owner's style without themselves being out of flow.
        const bool child_is_out_of_flow = child->kind == LAYOUT_BOX_ELEMENT && is_out_of_flow(child->style);
        const auto& own_extent = child_is_out_of_flow ? child->border_box : child->margin_box;
        if (include_x) right = std::max(right, own_extent.x + own_extent.width);
        if (include_y) bottom = std::max(bottom, own_extent.y + own_extent.height);

        const bool descend_x = include_x && child->style.overflow_x == AETHER_OVERFLOW_VISIBLE;
        const bool descend_y = include_y && child->style.overflow_y == AETHER_OVERFLOW_VISIBLE;
        if (descend_x || descend_y) {
            accumulate_scrollable_descendant_extent(child, descend_x, descend_y, right, bottom);
        }
    }
}
