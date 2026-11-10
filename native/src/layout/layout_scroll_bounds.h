#pragma once
#include "layout_types.h"
#include <limits>
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
