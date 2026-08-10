/* ── Scroll metrics — descendant-overflow accumulation ──────────────── */
#include "layout_internal.h"

#include <algorithm>

bool overflow_allows_user_scroll(int32_t overflow) {
    return overflow == AETHER_OVERFLOW_AUTO || overflow == AETHER_OVERFLOW_SCROLL;
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
        if (include_x) right = std::max(right, child->margin_box.x + child->margin_box.width);
        if (include_y) bottom = std::max(bottom, child->margin_box.y + child->margin_box.height);

        const bool descend_x = include_x && child->style.overflow_x == AETHER_OVERFLOW_VISIBLE;
        const bool descend_y = include_y && child->style.overflow_y == AETHER_OVERFLOW_VISIBLE;
        if (descend_x || descend_y) {
            accumulate_scrollable_descendant_extent(child, descend_x, descend_y, right, bottom);
        }
    }
}
