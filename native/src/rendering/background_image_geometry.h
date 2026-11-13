#pragma once

#include <aether/aether.h>

#include <vector>

// Resolves CSS background-layer sizing, positioning, and repetition after layout has produced
// the painting area. Raster resources supply intrinsic dimensions; procedural images such as
// gradients deliberately do not. This stays in the core so every binding consumes identical
// tile geometry.
struct aether_background_image_geometry_context_s {
    aether_rectf paint_rect{};
    aether_sizef image_size{};
    bool has_intrinsic_size = true;
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
    // Zero preserves the legacy path; native capture supplies a bounded work budget.
    size_t max_tiles = 0;
};

struct aether_background_image_tile_s {
    aether_rectf destination{};
    aether_rectf source{};
    // Unclipped tile bounds. Procedural images use this as their coordinate space so a clipped
    // edge tile preserves the same gradient phase as the complete tile.
    aether_rectf tile_rect{};
};

// Returns false only when sizing or the painting area are invalid. On success, tiles are clipped
// to paint_rect, source coordinates are adjusted to the clipped portion, and tile_rect retains
// each original un-clipped tile.
bool aether_background_image_resolve_geometry(
    const aether_background_image_geometry_context_s& context,
    std::vector<aether_background_image_tile_s>& tiles);
