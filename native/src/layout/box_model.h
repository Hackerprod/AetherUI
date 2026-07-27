#pragma once

#include "aether/aether.h"

/* CSS 2.1 §8.3.1 adjoining vertical margins.  This is intentionally pure:
   DOM traversal decides whether margins are adjoining; this module decides the
   numerical used value and the next margin-box origin. */
float aether_collapse_adjoining_margins(float first, float second);
float aether_next_block_margin_box_y(float previous_margin_box_end,
                                     float previous_margin_bottom,
                                     float next_margin_top);

/* Resolves CSS box-model edge values without layout-tree dependencies. */
aether_thicknessf aether_resolve_box_edges(aether_css_edges edges, float reference,
                                           float font_size, float root_font_size,
                                           float viewport_width, float viewport_height,
                                           bool allow_negative);
aether_thicknessf aether_resolve_border_edges(const aether_computed_style_t& style,
                                              float reference, float root_font_size,
                                              float viewport_width, float viewport_height);

/* Decorations are structural separators for vertical margin adjacency. */
bool aether_box_has_before_decoration(aether_thicknessf padding, aether_thicknessf border);
bool aether_box_has_after_decoration(aether_thicknessf padding, aether_thicknessf border);

/* Used-value resolution for one horizontal or vertical pair of CSS auto
   margins. Callers retain the axis topology; this pure operation only
   distributes positive free space among the auto sides. */
typedef struct {
    float before;
    float after;
} aether_auto_margin_pair;

aether_auto_margin_pair aether_resolve_auto_margin_pair(
    float available_size, float used_size,
    float before, float after,
    bool before_is_auto, bool after_is_auto);
