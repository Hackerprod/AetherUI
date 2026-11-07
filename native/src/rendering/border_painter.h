#pragma once

#include "aether/aether.h"
#include <vector>
#include "../css/style_read_view.h"

/* CSS Backgrounds & Borders Level 3 §3, §4.2, §4.4.
   Paints only the border ring. Backgrounds, DOM traversal, and display-list
   orchestration deliberately remain outside this module. */
void aether_paint_border(const aether_computed_style_t& style, aether_rectf border_box,
                         aether_corner_radii outer_radii,
                         std::vector<aether_paint_command_t>& commands);

void aether_paint_border(const aether_computed_style_t& style, aether_rectf box,
    aether_corner_radii radii, const aether_thicknessf& used_border,
    std::vector<aether_paint_command_t>& out);

void aether_paint_border(const aether::style::StyleScalars& style, aether_rectf box,
    aether_corner_radii radii, std::vector<aether_paint_command_t>& out);
void aether_paint_border(const aether::style::StyleScalars& style, aether_rectf box,
    aether_corner_radii radii, const aether_thicknessf& used_border,
    std::vector<aether_paint_command_t>& out);
