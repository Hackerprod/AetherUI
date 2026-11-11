#pragma once

#include <aether/aether.h>

// Geometry supported here is horizontal-tb/LTR. Keep physical axis, main
// reversal and wrapping independent; numeric enum order has no semantics.
struct flex_axis_s {
    bool horizontal;
    bool reverse;
};

inline flex_axis_s flex_axis(int32_t direction) {
    return {
        direction != AETHER_FLEX_DIRECTION_COLUMN && direction != AETHER_FLEX_DIRECTION_COLUMN_REVERSE,
        direction == AETHER_FLEX_DIRECTION_ROW_REVERSE || direction == AETHER_FLEX_DIRECTION_COLUMN_REVERSE
    };
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
