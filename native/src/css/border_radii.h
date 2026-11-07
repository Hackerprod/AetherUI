#pragma once

#include "aether/aether.h"
#include <string>

/* CSS Backgrounds & Borders Level 3 §4.1, §4.5.
   This module owns the CSS border-radius value model and its used-value
   normalization.  It deliberately does not know about DOM or paint commands. */
bool aether_border_radii_try_parse(const std::string& value, aether_border_radii* out);
aether_corner_radii aether_border_radii_resolve(const aether_border_radii& radii,
                                                aether_rectf border_box, float font_size);
aether_corner_radii aether_border_radii_resolve(const aether_border_radii& radii,
    aether_rectf border_box, float font_size, float root_font_size, float viewport_width, float viewport_height);
aether_corner_radii aether_corner_radii_inset(aether_corner_radii radii,
                                              float left, float top, float right, float bottom);
bool aether_corner_radii_is_zero(aether_corner_radii radii);
