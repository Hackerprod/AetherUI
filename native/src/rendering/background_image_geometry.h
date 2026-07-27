#pragma once

#include <aether/aether.h>

#include <vector>

// Resolves CSS background-image sizing, positioning, and repetition after layout has produced
// the painting area and the binding has supplied intrinsic image dimensions. This stays in the
// core so every binding consumes identical tile geometry.
struct aether_background_image_geometry_context_s {
    aether_rectf paint_rect{};
    aether_sizef image_size{};
    aether_css_length position_x{};
    aether_css_length position_y{};
    int32_t size_kind = AETHER_BACKGROUND_SIZE_AUTO;
    aether_css_length size_x{};
    aether_css_length size_y{};
    int32_t repeat_x = AETHER_BACKGROUND_REPEAT;
    int32_t repeat_y = AETHER_BACKGROUND_REPEAT;
    float font_size = 16.f;
    float root_font_size = 16.f;
    float viewport_width = 0.f;
    float viewport_height = 0.f;
};

struct aether_background_image_tile_s {
    aether_rectf destination{};
    aether_rectf source{};
};

// Returns false only when intrinsic dimensions or the painting area are invalid. On success,
// tiles are clipped to paint_rect and source coordinates are adjusted to the clipped portion.
bool aether_background_image_resolve_geometry(
    const aether_background_image_geometry_context_s& context,
    std::vector<aether_background_image_tile_s>& tiles);
