#pragma once

#include "aether/aether.h"
#include <vector>

/* CSS Backgrounds & Borders Level 3 §3, §4.2, §4.4.
   Paints only the border ring. Backgrounds, DOM traversal, and display-list
   orchestration deliberately remain outside this module. */
void aether_paint_border(const aether_computed_style_t& style, aether_rectf border_box,
                         aether_corner_radii outer_radii,
                         std::vector<aether_paint_command_t>& commands);
