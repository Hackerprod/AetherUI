/* ── Scroll metrics — descendant-overflow accumulation ──────────────── */
#include "layout_internal.h"
#include "layout_scroll_bounds.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

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

// Contextual overflow is accumulated in each box's local coordinate space.
// Apply that box's clips before its transform can mix the two axes; only the
// final caller chooses which axes contribute to its own scroll range.
namespace {
using OverflowBounds = layout_scroll_bounds_s;


void check_coordinate(double coordinate) {
    if (!std::isfinite(coordinate) || std::abs(coordinate) > std::numeric_limits<float>::max())
        throw std::invalid_argument("nonrepresentable transformed overflow");
}

OverflowBounds project(const OverflowBounds& bounds, const aether_transform_matrix& matrix) {
    OverflowBounds result;
    if (bounds.empty) return result;
    for (int corner = 0; corner < 4; ++corner) {
        const double x = (corner & 1) ? bounds.right : bounds.left;
        const double y = (corner & 2) ? bounds.bottom : bounds.top;
        const double mapped_x = x * matrix.m11 + y * matrix.m21 + matrix.dx;
        const double mapped_y = x * matrix.m12 + y * matrix.m22 + matrix.dy;
        check_coordinate(mapped_x);
        check_coordinate(mapped_y);
        aether_scroll_unite(result, {mapped_x, mapped_y, mapped_x, mapped_y, false});
    }
    return result;
}


bool establishes_fixed_container(const aether_layout_box_s* box) {
    return (box->visual_sample_valid && box->visual_has_transform) ||
        (box->owner && box->element && box->owner->effective_fixed_container_nodes.count(box->element));
}

OverflowBounds subtree_bounds(const aether_layout_box_s* box, bool fixed_container) {
    OverflowBounds result = aether_scroll_own_bounds(box);
    fixed_container = fixed_container || establishes_fixed_container(box);
    for (const auto* child : box->children) {
        if (child->style.position == AETHER_POSITION_FIXED && !fixed_container) continue;
        // Clip each child contribution before union: two disjoint children
        // outside opposite clip edges must not manufacture a spanning rectangle.
        aether_scroll_unite(result, aether_scroll_clip_descendant(subtree_bounds(child, fixed_container), box));
    }
    if (box->visual_sample_valid && box->visual_has_transform) {
        // Scrollable overflow cannot shrink under a transform. The untransformed
        // and transformed local bounds are both propagated to the parent space.
        const auto transformed = project(result, box->visual_transform);
        aether_scroll_unite(result, transformed);
    }
    return result;
}

void accumulate_legacy(const aether_layout_box_s* parent, bool include_x, bool include_y,
    float& right, float& bottom) {
    for (const auto* child : parent->children) {
        const auto own = aether_scroll_own_bounds(child);
        if (include_x) right = std::max(right, float(own.right));
        if (include_y) bottom = std::max(bottom, float(own.bottom));
        const bool descend_x = include_x && child->style.overflow_x == AETHER_OVERFLOW_VISIBLE;
        const bool descend_y = include_y && child->style.overflow_y == AETHER_OVERFLOW_VISIBLE;
        if (descend_x || descend_y)
            accumulate_legacy(child, descend_x, descend_y, right, bottom);
    }
}
} // namespace

void accumulate_scrollable_descendant_extent(const aether_layout_box_s* parent,
    bool include_x, bool include_y, float& right, float& bottom) {
    if (!parent->visual_sample_valid) {
        accumulate_legacy(parent, include_x, include_y, right, bottom);
        return;
    }
    const bool fixed_container = establishes_fixed_container(parent);
    for (const auto* child : parent->children) {
        if (child->style.position == AETHER_POSITION_FIXED && !fixed_container) continue;
        const auto bounds = subtree_bounds(child, fixed_container);
        if (bounds.empty) continue;
        check_coordinate(bounds.right);
        check_coordinate(bounds.bottom);
        if (include_x) right = std::max(right, float(bounds.right));
        if (include_y) bottom = std::max(bottom, float(bounds.bottom));
    }
}
