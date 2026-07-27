#pragma once

#include <aether/aether.h>

/* Pure CSS Text alignment decisions. The layout tree supplies topology
   (which fragments are distributable spaces); this module only owns the
   numerical and line-termination rules. */
float aether_text_justify_extra_per_gap(float available_width, float line_width,
                                        int gap_count);

bool aether_text_line_should_justify(int32_t text_align, bool is_last_line,
                                     bool ends_with_forced_break, int gap_count,
                                     float available_width, float line_width);
