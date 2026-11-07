#include "box_model.h"
#include "../css/border_utils.h"
#include <algorithm>
#include <cmath>

static float resolve_edge(aether_css_length value, float reference, float font_size,
                          float root_font_size, float viewport_width, float viewport_height,
                          bool allow_negative) {
    const float resolved = aether_css_length_resolve(value, reference, font_size, root_font_size,
                                                     viewport_width, viewport_height);
    if (!std::isfinite(resolved)) return 0.f;
    return allow_negative ? resolved : std::max(0.f, resolved);
}

float aether_collapse_adjoining_margins(float first, float second) {
    return std::max({0.f, first, second}) + std::min({0.f, first, second});
}

float aether_next_block_margin_box_y(float previous_margin_box_end,
                                     float previous_margin_bottom,
                                     float next_margin_top) {
    const float collapsed = aether_collapse_adjoining_margins(previous_margin_bottom, next_margin_top);
    // Convert the border-to-border collapsed distance to the next margin-box origin.
    return previous_margin_box_end - previous_margin_bottom + collapsed - next_margin_top;
}

aether_thicknessf aether_resolve_box_edges(aether_css_edges edges, float reference,
                                           float font_size, float root_font_size,
                                           float viewport_width, float viewport_height,
                                           bool allow_negative) {
    return {resolve_edge(edges.left, reference, font_size, root_font_size, viewport_width, viewport_height, allow_negative),
            resolve_edge(edges.top, reference, font_size, root_font_size, viewport_width, viewport_height, allow_negative),
            resolve_edge(edges.right, reference, font_size, root_font_size, viewport_width, viewport_height, allow_negative),
            resolve_edge(edges.bottom, reference, font_size, root_font_size, viewport_width, viewport_height, allow_negative)};
}

aether_thicknessf aether_resolve_border_edges(const aether::style::StyleScalars& style,
                                              float reference, float root_font_size,
                                              float viewport_width, float viewport_height) {
    auto edges = aether_resolve_box_edges(style.border_width, reference, style.font_size,
                                          root_font_size, viewport_width, viewport_height, false);
    if (!aether_border_style_occupies_space(style.border_styles.left)) edges.left = 0;
    if (!aether_border_style_occupies_space(style.border_styles.top)) edges.top = 0;
    if (!aether_border_style_occupies_space(style.border_styles.right)) edges.right = 0;
    if (!aether_border_style_occupies_space(style.border_styles.bottom)) edges.bottom = 0;
    return edges;
}

bool aether_box_has_before_decoration(aether_thicknessf padding, aether_thicknessf border) {
    return padding.top > 0.f || border.top > 0.f;
}

bool aether_box_has_after_decoration(aether_thicknessf padding, aether_thicknessf border) {
    return padding.bottom > 0.f || border.bottom > 0.f;
}

aether_auto_margin_pair aether_resolve_auto_margin_pair(
    float available_size, float used_size,
    float before, float after,
    bool before_is_auto, bool after_is_auto) {
    if (before_is_auto) before = 0.f;
    if (after_is_auto) after = 0.f;
    const int auto_count = static_cast<int>(before_is_auto) + static_cast<int>(after_is_auto);
    const float free = available_size - used_size - before - after;
    if (auto_count == 0 || !std::isfinite(free) || free <= 0.f) return {before, after};
    const float share = free / static_cast<float>(auto_count);
    if (before_is_auto) before = share;
    if (after_is_auto) after = share;
    return {before, after};
}

aether_thicknessf aether_resolve_border_edges(const aether_computed_style_t& style, float reference, float root_font_size, float viewport_width, float viewport_height) {
    return aether_resolve_border_edges(aether::style::StyleScalars::copy_from(style), reference, root_font_size, viewport_width, viewport_height);
}
