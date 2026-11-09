/* ── Scroll metrics — descendant-overflow accumulation ──────────────── */
#include "layout_internal.h"

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
struct OverflowBounds {
    double left = 0, top = 0, right = 0, bottom = 0;
    bool empty = true;
};

void unite(OverflowBounds& result, const OverflowBounds& contribution) {
    if (contribution.empty) return;
    if (result.empty) { result = contribution; return; }
    result.left = std::min(result.left, contribution.left);
    result.top = std::min(result.top, contribution.top);
    result.right = std::max(result.right, contribution.right);
    result.bottom = std::max(result.bottom, contribution.bottom);
}

OverflowBounds rectangle(aether_rectf rect) {
    return {rect.x, rect.y, double(rect.x) + rect.width, double(rect.y) + rect.height, false};
}

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
        unite(result, {mapped_x, mapped_y, mapped_x, mapped_y, false});
    }
    return result;
}

OverflowBounds clip_descendant(OverflowBounds bounds, const aether_layout_box_s* owner) {
    if (bounds.empty) return bounds;
    const auto& clip = owner->padding_box;
    if (owner->style.overflow_x != AETHER_OVERFLOW_VISIBLE) {
        bounds.left = std::max(bounds.left, double(clip.x));
        bounds.right = std::min(bounds.right, double(clip.x) + clip.width);
    }
    if (owner->style.overflow_y != AETHER_OVERFLOW_VISIBLE) {
        bounds.top = std::max(bounds.top, double(clip.y));
        bounds.bottom = std::min(bounds.bottom, double(clip.y) + clip.height);
    }
    bounds.empty = bounds.right < bounds.left || bounds.bottom < bounds.top;
    return bounds;
}

bool establishes_fixed_container(const aether_layout_box_s* box) {
    return (box->visual_sample_valid && box->visual_has_transform) ||
        (box->owner && box->element && box->owner->effective_fixed_container(box->element));
}

OverflowBounds subtree_bounds(const aether_layout_box_s* box, bool fixed_container) {
    const bool positioned = box->kind == LAYOUT_BOX_ELEMENT &&
        (box->style.position == AETHER_POSITION_ABSOLUTE || box->style.position == AETHER_POSITION_FIXED);
    OverflowBounds result = rectangle(positioned ? box->border_box : box->margin_box);
    fixed_container = fixed_container || establishes_fixed_container(box);
    for (const auto* child : box->children) {
        if (child->style.position == AETHER_POSITION_FIXED && !fixed_container) continue;
        // Clip each child contribution before union: two disjoint children
        // outside opposite clip edges must not manufacture a spanning rectangle.
        unite(result, clip_descendant(subtree_bounds(child, fixed_container), box));
    }
    if (box->visual_sample_valid && box->visual_has_transform) {
        // Scrollable overflow cannot shrink under a transform. The untransformed
        // and transformed local bounds are both propagated to the parent space.
        const auto transformed = project(result, box->visual_transform);
        unite(result, transformed);
    }
    return result;
}

void accumulate_legacy(const aether_layout_box_s* parent, bool include_x, bool include_y,
    float& right, float& bottom) {
    for (const auto* child : parent->children) {
        const bool positioned = child->kind == LAYOUT_BOX_ELEMENT &&
            (child->style.position == AETHER_POSITION_ABSOLUTE || child->style.position == AETHER_POSITION_FIXED);
        const auto& own = positioned ? child->border_box : child->margin_box;
        if (include_x) right = std::max(right, own.x + own.width);
        if (include_y) bottom = std::max(bottom, own.y + own.height);
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
