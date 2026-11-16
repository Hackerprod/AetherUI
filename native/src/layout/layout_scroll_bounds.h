#pragma once
#include "layout_types.h"
#include <limits>
#include <algorithm>
#include <stdexcept>

// A closed segment is not empty. In particular, hanging text can have no
// inline extent but its full height still contributes after an affine map.
struct layout_scroll_bounds_s {
    double left = 0., top = 0., right = 0., bottom = 0.;
    bool empty = true;
};

inline void aether_check_scroll_coordinate(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw std::invalid_argument("nonrepresentable scroll proxy");
}

inline layout_scroll_bounds_s aether_scroll_own_bounds(const aether_layout_box_s* box) {
    const auto& proxy = box->scroll_proxy;
    if (proxy.valid && box->kind == LAYOUT_BOX_TEXT) {
        const double dx = double(box->content_box.x) - double(proxy.reference_content_x);
        const double top = double(box->content_box.y);
        // Check before addition: a huge y can round away a negative height.
        if (box->content_box.height < 0.f)
            throw std::invalid_argument("inverted scroll proxy height");
        const layout_scroll_bounds_s bounds{
            proxy.left + dx, top, proxy.right + dx, top + double(box->content_box.height), false};
        aether_check_scroll_coordinate(bounds.left);
        aether_check_scroll_coordinate(bounds.top);
        aether_check_scroll_coordinate(bounds.right);
        aether_check_scroll_coordinate(bounds.bottom);
        if (bounds.left > bounds.right || bounds.top > bounds.bottom)
            throw std::invalid_argument("inverted scroll proxy");
        return bounds;
    }
    const bool positioned = box->kind == LAYOUT_BOX_ELEMENT &&
        (box->style.position == AETHER_POSITION_ABSOLUTE || box->style.position == AETHER_POSITION_FIXED);
    const auto& rect = positioned ? box->border_box : box->margin_box;
    return {rect.x, rect.y, double(rect.x) + rect.width, double(rect.y) + rect.height, false};
}

// Shared exact contextual operations; legacy selection and traversal unchanged.
inline void aether_scroll_unite(layout_scroll_bounds_s& result, const layout_scroll_bounds_s& contribution) {
    if (contribution.empty) return;
    if (result.empty) { result = contribution; return; }
    result.left = std::min(result.left, contribution.left);
    result.top = std::min(result.top, contribution.top);
    result.right = std::max(result.right, contribution.right);
    result.bottom = std::max(result.bottom, contribution.bottom);
}

inline layout_scroll_bounds_s aether_scroll_clip_descendant(layout_scroll_bounds_s bounds, const aether_rectf& clip,int overflow_x,int overflow_y) {
    if (bounds.empty) return bounds;
    if (overflow_x != AETHER_OVERFLOW_VISIBLE) {
        bounds.left = std::max(bounds.left, double(clip.x));
        bounds.right = std::min(bounds.right, double(clip.x) + clip.width);
    }
    if (overflow_y != AETHER_OVERFLOW_VISIBLE) {
        bounds.top = std::max(bounds.top, double(clip.y));
        bounds.bottom = std::min(bounds.bottom, double(clip.y) + clip.height);
    }
    bounds.empty = bounds.right < bounds.left || bounds.bottom < bounds.top;
    return bounds;
}

inline layout_scroll_bounds_s aether_scroll_clip_descendant(layout_scroll_bounds_s bounds,const aether_layout_box_s* owner){return aether_scroll_clip_descendant(bounds,owner->padding_box,owner->style.overflow_x,owner->style.overflow_y); }
