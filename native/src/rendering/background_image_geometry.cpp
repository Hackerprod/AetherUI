#include "background_image_geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

struct axis_tile_s { float start; float length; };

float resolve_length(aether_css_length value, float reference,
                     float font_size, float root_font_size,
                     float viewport_width, float viewport_height) {
    switch (value.unit) {
        case AETHER_CSS_UNIT_PX: return value.value;
        case AETHER_CSS_UNIT_PERCENT: return reference * value.value / 100.f;
        case AETHER_CSS_UNIT_EM: return font_size * value.value;
        case AETHER_CSS_UNIT_REM: return root_font_size * value.value;
        case AETHER_CSS_UNIT_VW: return viewport_width * value.value / 100.f;
        case AETHER_CSS_UNIT_VH: return viewport_height * value.value / 100.f;
        case AETHER_CSS_UNIT_LINEAR:
            return value.value + reference * value.linear_percent / 100.f + font_size * value.linear_em +
                root_font_size * value.linear_rem + viewport_width * value.linear_vw / 100.f +
                viewport_height * value.linear_vh / 100.f;
        default: return std::numeric_limits<float>::quiet_NaN();
    }
}

float position_offset(aether_css_length value, float free_space,
                      float area_size, const aether_background_image_geometry_context_s& context) {
    if (value.unit == AETHER_CSS_UNIT_PERCENT) return free_space * value.value / 100.f;
    return resolve_length(value, area_size, context.font_size, context.root_font_size,
                          context.viewport_width, context.viewport_height);
}

bool resolve_image_size(const aether_background_image_geometry_context_s& context,
                        float& width, float& height) {
    const float source_width = context.image_size.width;
    const float source_height = context.image_size.height;
    const float area_width = context.paint_rect.width;
    const float area_height = context.paint_rect.height;
    if (!(area_width > 0.f && area_height > 0.f)) return false;
    if (context.has_intrinsic_size && !(source_width > 0.f && source_height > 0.f)) return false;

    if (context.size_kind == AETHER_BACKGROUND_SIZE_COVER ||
        context.size_kind == AETHER_BACKGROUND_SIZE_CONTAIN) {
        // A procedural CSS image has neither an intrinsic size nor ratio. Both cover and contain
        // therefore resolve to the background positioning area.
        if (!context.has_intrinsic_size) {
            width = area_width;
            height = area_height;
            return true;
        }
        const float scale_x = area_width / source_width;
        const float scale_y = area_height / source_height;
        const float scale = context.size_kind == AETHER_BACKGROUND_SIZE_COVER
            ? std::max(scale_x, scale_y) : std::min(scale_x, scale_y);
        width = source_width * scale;
        height = source_height * scale;
        return std::isfinite(width) && std::isfinite(height) && width > 0.f && height > 0.f;
    }

    const bool auto_x = context.size_x.unit == AETHER_CSS_UNIT_AUTO;
    const bool auto_y = context.size_y.unit == AETHER_CSS_UNIT_AUTO;
    if (auto_x && auto_y) {
        width = context.has_intrinsic_size ? source_width : area_width;
        height = context.has_intrinsic_size ? source_height : area_height;
    } else if (auto_x) {
        height = resolve_length(context.size_y, area_height, context.font_size, context.root_font_size,
                                context.viewport_width, context.viewport_height);
        width = context.has_intrinsic_size ? source_width * height / source_height : area_width;
    } else if (auto_y) {
        width = resolve_length(context.size_x, area_width, context.font_size, context.root_font_size,
                               context.viewport_width, context.viewport_height);
        height = context.has_intrinsic_size ? source_height * width / source_width : area_height;
    } else {
        width = resolve_length(context.size_x, area_width, context.font_size, context.root_font_size,
                               context.viewport_width, context.viewport_height);
        height = resolve_length(context.size_y, area_height, context.font_size, context.root_font_size,
                                context.viewport_width, context.viewport_height);
    }
    return std::isfinite(width) && std::isfinite(height) && width > 0.f && height > 0.f;
}

std::vector<axis_tile_s> resolve_axis(float area_start, float area_length, float image_length,
                                      float positioned_start, int32_t repeat) {
    std::vector<axis_tile_s> result;
    if (!(area_length > 0.f && image_length > 0.f)) return result;
    const float area_end = area_start + area_length;

    if (repeat == AETHER_BACKGROUND_NO_REPEAT) {
        result.push_back({positioned_start, image_length});
        return result;
    }

    if (repeat == AETHER_BACKGROUND_SPACE) {
        const int count = static_cast<int>(std::floor(area_length / image_length));
        if (count < 2) {
            result.push_back({positioned_start, image_length});
            return result;
        }
        const float gap = (area_length - count * image_length) / static_cast<float>(count - 1);
        for (int index = 0; index < count; ++index)
            result.push_back({area_start + index * (image_length + gap), image_length});
        return result;
    }

    if (repeat == AETHER_BACKGROUND_ROUND) {
        const int count = std::max(1, static_cast<int>(std::round(area_length / image_length)));
        const float rounded_length = area_length / static_cast<float>(count);
        for (int index = 0; index < count; ++index)
            result.push_back({area_start + index * rounded_length, rounded_length});
        return result;
    }

    const int first = static_cast<int>(std::floor((area_start - positioned_start) / image_length));
    for (float start = positioned_start + first * image_length; start < area_end; start += image_length) {
        if (start + image_length > area_start) result.push_back({start, image_length});
    }
    return result;
}

bool intersect(float first_start, float first_length, float second_start, float second_length,
               float& clipped_start, float& clipped_length) {
    clipped_start = std::max(first_start, second_start);
    const float end = std::min(first_start + first_length, second_start + second_length);
    clipped_length = end - clipped_start;
    return clipped_length > 0.f;
}

} // namespace

bool aether_background_image_resolve_geometry(
    const aether_background_image_geometry_context_s& context,
    std::vector<aether_background_image_tile_s>& tiles) {
    tiles.clear();
    float image_width = 0.f;
    float image_height = 0.f;
    if (!resolve_image_size(context, image_width, image_height)) return false;

    const float positioned_x = context.paint_rect.x + position_offset(
        context.position_x, context.paint_rect.width - image_width, context.paint_rect.width, context);
    const float positioned_y = context.paint_rect.y + position_offset(
        context.position_y, context.paint_rect.height - image_height, context.paint_rect.height, context);
    if (!std::isfinite(positioned_x) || !std::isfinite(positioned_y)) return false;

    const auto horizontal = resolve_axis(context.paint_rect.x, context.paint_rect.width, image_width,
                                         positioned_x, context.repeat_x);
    const auto vertical = resolve_axis(context.paint_rect.y, context.paint_rect.height, image_height,
                                       positioned_y, context.repeat_y);
    for (const auto& y : vertical) {
        float clipped_y = 0.f;
        float clipped_height = 0.f;
        if (!intersect(y.start, y.length, context.paint_rect.y, context.paint_rect.height, clipped_y, clipped_height)) continue;
        for (const auto& x : horizontal) {
            float clipped_x = 0.f;
            float clipped_width = 0.f;
            if (!intersect(x.start, x.length, context.paint_rect.x, context.paint_rect.width, clipped_x, clipped_width)) continue;
            const float source_width_base = context.has_intrinsic_size ? context.image_size.width : image_width;
            const float source_height_base = context.has_intrinsic_size ? context.image_size.height : image_height;
            const float source_x = (clipped_x - x.start) * source_width_base / x.length;
            const float source_y = (clipped_y - y.start) * source_height_base / y.length;
            const float source_width = clipped_width * source_width_base / x.length;
            const float source_height = clipped_height * source_height_base / y.length;
            tiles.push_back({{clipped_x, clipped_y, clipped_width, clipped_height},
                             {source_x, source_y, source_width, source_height},
                             {x.start, y.start, x.length, y.length}});
        }
    }
    return !tiles.empty();
}
