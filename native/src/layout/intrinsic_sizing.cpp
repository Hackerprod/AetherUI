#include "intrinsic_sizing.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

float aether_intrinsic_min_from_unbreakable_fragments(const std::vector<float>& fragment_widths) {
    float result = 0.f;
    for (const float width : fragment_widths) {
        if (std::isfinite(width)) result = std::max(result, std::max(0.f, width));
    }
    return result;
}

aether_inline_intrinsic_sizes_s aether_measure_inline_intrinsic_sizes(
    const std::vector<aether_inline_intrinsic_fragment_s>& fragments) {
    aether_inline_intrinsic_sizes_s result{};
    float line_width = 0.f;
    float pending_whitespace = 0.f;
    bool has_line_content = false;

    for (const auto& fragment : fragments) {
        const float width = std::isfinite(fragment.width) ? std::max(0.f, fragment.width) : 0.f;
        if (fragment.kind == aether_inline_fragment_kind_e::forced_break) {
            result.max_content = std::max(result.max_content, line_width);
            line_width = 0.f;
            pending_whitespace = 0.f;
            has_line_content = false;
            continue;
        }
        if (fragment.kind == aether_inline_fragment_kind_e::whitespace) {
            if (has_line_content) pending_whitespace = width;
            continue;
        }

        if (has_line_content) line_width += pending_whitespace;
        line_width += width;
        pending_whitespace = 0.f;
        has_line_content = true;
        result.min_content = std::max(result.min_content, width);
    }
    result.max_content = std::max(result.max_content, line_width);
    return result;
}

float aether_intrinsic_flex_main_contribution(
    const std::vector<float>& child_contributions,
    bool column_direction,
    bool wraps,
    float gap,
    aether_intrinsic_size_mode_e mode) {
    // A nowrap row retains signed outer contributions until after every gap.
    // Accumulate in double so intermediate float overflow cannot erase a later
    // negative margin; reject a non-finite input before any clamp can hide it.
    if (!column_direction && !wraps) {
        if (!std::isfinite(gap))
            throw std::overflow_error("Non-finite intrinsic flex gap");
        double total = 0.0;
        for (const float width : child_contributions) {
            if (!std::isfinite(width))
                throw std::overflow_error("Non-finite intrinsic flex contribution");
            total += width;
        }
        if (!child_contributions.empty())
            total += static_cast<double>(std::max(0.f, gap)) * (child_contributions.size() - 1);
        if (!std::isfinite(total) || total > std::numeric_limits<float>::max())
            throw std::overflow_error("Intrinsic flex sum overflow");
        return static_cast<float>(std::max(0.0, total));
    }
    if (child_contributions.empty()) return 0.f;
    const auto largest = *std::max_element(child_contributions.begin(), child_contributions.end());
    // Horizontal column sizing and a wrapping row's minimum use the widest
    // contribution. Row max-content keeps all items and gaps on one line;
    // a nowrap row also retains that sum for its minimum.
    if (column_direction || (wraps && mode == aether_intrinsic_size_mode_e::min_content))
        return std::max(0.f, largest);

    float result = 0.f;
    for (const float width : child_contributions) result += std::max(0.f, width);
    return result + std::max(0.f, gap) * static_cast<float>(child_contributions.size() - 1);
}

bool aether_intrinsic_flex_needs_hypothetical(
    float contribution, float base, bool base_is_definite,
    float flex_grow, float flex_shrink) {
    if (!std::isfinite(contribution) || !std::isfinite(flex_grow) ||
        !std::isfinite(flex_shrink) || (base_is_definite && !std::isfinite(base)))
        throw std::overflow_error("Non-finite intrinsic flex selection");
    return base_is_definite &&
        ((contribution > base && flex_grow == 0.f) ||
         (contribution < base && flex_shrink == 0.f));
}
