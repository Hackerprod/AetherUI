#include "scrollbar_geometry.h"
#include "layout_internal.h"

#include <algorithm>

namespace {

constexpr float scrollbar_thickness = 4.f;
constexpr float scrollbar_margin = 2.f;

bool scrollbar_is_visible(int32_t overflow, float scrollable_overflow) {
    // Scrollbars only exist for user-scrollable values: scroll always, auto when
    // overflowing; hidden/clip never publish chrome (§3.1).
    if (!aether_overflow_allows_user_scroll(overflow)) return false;
    return overflow == AETHER_OVERFLOW_SCROLL || scrollable_overflow > 0.f;
}

aether_rectf resolve_thumb(const aether_rectf& track, bool horizontal,
                           float scrollable_overflow, float scroll_offset) {
    const float thickness = horizontal ? track.height : track.width;
    const float track_length = horizontal ? track.width : track.height;
    const float content_extent = track_length + scrollable_overflow;
    const float thumb_length = std::min(track_length,
        std::max(std::min(24.f, track_length),
            content_extent > 0.f ? track_length * (track_length / content_extent) : track_length));
    const float available = std::max(0.f, track_length - thumb_length);
    const float progress = scrollable_overflow > 0.f
        ? std::clamp(scroll_offset / scrollable_overflow, 0.f, 1.f) : 0.f;
    const float position = (horizontal ? track.x : track.y) + available * progress;
    return horizontal
        ? aether_rectf{position, track.y, thumb_length, thickness}
        : aether_rectf{track.x, position, thickness, thumb_length};
}

}

aether_scrollbar_geometry_t aether_resolve_scrollbar_geometry(
    const aether_layout_box_s& box,
    aether_scroll_offset_t offset) {
    aether_scrollbar_geometry_t geometry{};
    const bool vertical = scrollbar_is_visible(box.style.overflow_y, box.scroll.scrollable_overflow_y);
    const bool horizontal = scrollbar_is_visible(box.style.overflow_x, box.scroll.scrollable_overflow_x);
    if (!vertical && !horizontal) return geometry;

    const auto& area = box.padding_box;
    const float reserved_vertical = vertical ? scrollbar_thickness + scrollbar_margin : 0.f;
    const float reserved_horizontal = horizontal ? scrollbar_thickness + scrollbar_margin : 0.f;
    geometry.vertical_visible = vertical ? AETHER_TRUE : AETHER_FALSE;
    geometry.horizontal_visible = horizontal ? AETHER_TRUE : AETHER_FALSE;
    geometry.vertical_track = {
        area.x + area.width - scrollbar_thickness - scrollbar_margin, area.y,
        scrollbar_thickness, std::max(0.f, area.height - reserved_horizontal)
    };
    geometry.horizontal_track = {
        area.x, area.y + area.height - scrollbar_thickness - scrollbar_margin,
        std::max(0.f, area.width - reserved_vertical), scrollbar_thickness
    };
    if (vertical) {
        geometry.vertical_thumb = resolve_thumb(geometry.vertical_track, false,
            box.scroll.scrollable_overflow_y, offset.y);
    }
    if (horizontal) {
        geometry.horizontal_thumb = resolve_thumb(geometry.horizontal_track, true,
            box.scroll.scrollable_overflow_x, offset.x);
    }
    if (vertical && horizontal) {
        geometry.corner = {geometry.vertical_track.x, geometry.horizontal_track.y,
            scrollbar_thickness, scrollbar_thickness};
    }
    return geometry;
}

aether_scrollbar_geometry_t aether_resolve_scrollbar_geometry(const aether_layout_box_s& box) {
    return aether_resolve_scrollbar_geometry(box,
        {box.scroll.scroll_offset_x, box.scroll.scroll_offset_y});
}
