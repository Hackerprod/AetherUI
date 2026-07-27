#pragma once

#include <aether/aether.h>

enum class aether_border_side_t : int {
    top = 0,
    right = 1,
    bottom = 2,
    left = 3,
};

inline bool aether_border_style_occupies_space(int32_t style) {
    return style != AETHER_BORDER_STYLE_NONE && style != AETHER_BORDER_STYLE_HIDDEN;
}

inline bool aether_border_side_is_top_or_left(aether_border_side_t side) {
    return side == aether_border_side_t::top || side == aether_border_side_t::left;
}

inline aether_css_length& aether_border_width_for_side(aether_css_edges& edges, aether_border_side_t side) {
    switch (side) {
        case aether_border_side_t::top: return edges.top;
        case aether_border_side_t::right: return edges.right;
        case aether_border_side_t::bottom: return edges.bottom;
        case aether_border_side_t::left: return edges.left;
    }
    return edges.left;
}

inline aether_color_rgba& aether_border_color_for_side(aether_color_edges& edges, aether_border_side_t side) {
    switch (side) {
        case aether_border_side_t::top: return edges.top;
        case aether_border_side_t::right: return edges.right;
        case aether_border_side_t::bottom: return edges.bottom;
        case aether_border_side_t::left: return edges.left;
    }
    return edges.left;
}

inline int32_t& aether_border_style_for_side(aether_border_style_edges& edges, aether_border_side_t side) {
    switch (side) {
        case aether_border_side_t::top: return edges.top;
        case aether_border_side_t::right: return edges.right;
        case aether_border_side_t::bottom: return edges.bottom;
        case aether_border_side_t::left: return edges.left;
    }
    return edges.left;
}
