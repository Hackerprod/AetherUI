#include "text_justify.h"

#include <cmath>

float aether_text_justify_extra_per_gap(float available_width, float line_width,
                                        int gap_count) {
    const float extra = available_width - line_width;
    if (gap_count <= 0 || !std::isfinite(extra) || extra <= 0.f) return 0.f;
    return extra / static_cast<float>(gap_count);
}

bool aether_text_line_should_justify(int32_t text_align, bool is_last_line,
                                     bool ends_with_forced_break, int gap_count,
                                     float available_width, float line_width) {
    return text_align == AETHER_TEXT_ALIGN_JUSTIFY &&
           !is_last_line &&
           !ends_with_forced_break &&
           aether_text_justify_extra_per_gap(available_width, line_width, gap_count) > 0.f;
}
