#pragma once

#include <aether/aether.h>

// horizontal-tb: writing direction and flex reversal are independent.
// Physical margins are never swapped; reflect entire margin boxes.
struct flex_axis_s {
    bool horizontal;
    bool reverse;
    bool logical_reverse;
    bool cross_reverse;
};

inline flex_axis_s flex_axis(int32_t direction, bool rtl = false) {
    const bool horizontal = direction != AETHER_FLEX_DIRECTION_COLUMN && direction != AETHER_FLEX_DIRECTION_COLUMN_REVERSE;
    const bool logical_reverse = direction == AETHER_FLEX_DIRECTION_ROW_REVERSE || direction == AETHER_FLEX_DIRECTION_COLUMN_REVERSE;
    return {horizontal, logical_reverse != (horizontal && rtl), logical_reverse, !horizontal && rtl};
}

// Distance from flex main-start. Negative free space is deliberate overflow.
inline float flex_main_alignment_offset(int32_t alignment, float free, bool reverse) {
    switch (alignment) {
        case AETHER_JUSTIFY_CENTER: return free / 2.f;
        case AETHER_JUSTIFY_FLEX_END: return free;
        case AETHER_JUSTIFY_LOGICAL_START: return reverse ? free : 0.f;
        case AETHER_JUSTIFY_LOGICAL_END: return reverse ? 0.f : free;
        default: return 0.f; // flex-start; space-between's single-item fallback.
    }
}

inline float flex_main_position(float origin, float extent, float distance,
                                float outer_size, bool reverse) {
    return reverse ? origin + extent - distance - outer_size : origin + distance;
}
