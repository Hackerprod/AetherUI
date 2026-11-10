#include "flex_resolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float epsilon = 0.0001f;

float non_negative(float value) {
    return std::isfinite(value) ? std::max(0.f, value) : 0.f;
}

double clamp_main_size(double value, const aether_flex_item_input_s& item) {
    const double requested_minimum = std::isfinite(item.min_main_size)
        ? non_negative(item.min_main_size)
        : aether_flex_resolve_automatic_minimum(item.automatic_minimum);
    // Flexbox §§9.2, 9.7 floor the content box at zero when calculating the
    // hypothetical size and clamping targets, but never when finding the base.
    const double minimum = std::max(requested_minimum,
                                   static_cast<double>(non_negative(item.main_decoration_size)));
    const double maximum = std::isfinite(item.max_main_size)
        ? std::max(minimum, static_cast<double>(item.max_main_size))
        : std::numeric_limits<float>::infinity();
    return std::clamp(std::max(0.0, value), minimum, maximum);
}

float outer_size(float main_size, const aether_flex_item_input_s& item) {
    // outer_main_size represents explicit margins, which are legally signed
    // in CSS. Do not reuse non_negative(): it is intentionally correct for
    // flex bases and size constraints, but incorrect for margins.
    return main_size + (std::isfinite(item.outer_main_size) ? item.outer_main_size : 0.f);
}

double line_free_space(const std::vector<aether_flex_item_input_s>& items,
                       const std::vector<aether_flex_item_result_s>& results,
                       const aether_flex_line_s& line, float available_main_size, float gap) {
    double free_space = available_main_size;
    free_space -= static_cast<double>(gap) *
        static_cast<double>(line.item_indices.empty() ? 0 : line.item_indices.size() - 1);
    for (const size_t index : line.item_indices) {
        const auto& result = results[index];
        // Both initial and remaining space use the frozen target, or the
        // original base for an active item. Margins remain signed.
        free_space -= result.frozen ? result.target_main_size : result.flex_base_size;
        if (std::isfinite(items[index].outer_main_size)) free_space -= items[index].outer_main_size;
    }
    return free_space;
}

void resolve_line(const std::vector<aether_flex_item_input_s>& items,
                  std::vector<aether_flex_item_result_s>& results,
                  const aether_flex_line_s& line, float available_main_size, float gap) {
    if (!std::isfinite(available_main_size)) return;

    const bool using_grow = line.hypothetical_outer_main_size < available_main_size;
    for (const size_t index : line.item_indices) {
        auto& result = results[index];
        result.target_main_size = result.flex_base_size;
        const float factor = using_grow ? items[index].flex_grow
                                        : items[index].flex_shrink;
        // Flexbox §9.7's initial freeze applies the hypothetical size, not the
        // flex base. A zero base with a positive automatic minimum freezes
        // under shrink because the base is below the hypothetical size, not
        // because its scaled shrink weight is zero. The decoration floor also
        // freezes every negative inner base before shrink weights are summed.
        // Use strict comparisons: even a tiny negative inner base must freeze.
        const bool violates_hypothetical = using_grow
            ? result.flex_base_size > result.hypothetical_main_size
            : result.flex_base_size < result.hypothetical_main_size;
        result.frozen = factor <= 0.f || violates_hypothetical;
        if (result.frozen) result.target_main_size = result.hypothetical_main_size;
    }

    // This budget is measured after the initial freeze and never changes as
    // later min/max violations freeze more items.
    const double initial_free_space = line_free_space(items, results, line, available_main_size, gap);
    std::vector<double> violations(line.item_indices.size(), 0.0);

    while (true) {
        double remaining_free_space = line_free_space(items, results, line, available_main_size, gap);
        double factor_sum = 0.0;
        double weight_sum = 0.0;
        size_t unfrozen_count = 0;
        for (const size_t index : line.item_indices) {
            const auto& item = items[index];
            const auto& result = results[index];
            if (!result.frozen) {
                ++unfrozen_count;
                const double factor = using_grow ? item.flex_grow : item.flex_shrink;
                factor_sum += factor;
                weight_sum += using_grow ? factor : factor * result.inner_flex_base_size;
            }
        }
        if (unfrozen_count == 0) return;

        // The partial budget uses unscaled factors in BOTH modes. An active
        // zero-inner-base item still contributes its factor despite zero weight.
        if (factor_sum < 1.0) {
            const double scaled_initial_free_space = initial_free_space * factor_sum;
            if (std::fabs(scaled_initial_free_space) < std::fabs(remaining_free_space)) {
                remaining_free_space = scaled_initial_free_space;
            }
        }

        double total_violation = 0.0;
        for (size_t position = 0; position < line.item_indices.size(); ++position) {
            const size_t index = line.item_indices[position];
            auto& result = results[index];
            if (result.frozen) continue;
            const auto& item = items[index];
            const double weight = using_grow ? static_cast<double>(item.flex_grow)
                : static_cast<double>(item.flex_shrink) * result.inner_flex_base_size;
            // Zero weights leave the original base intact, then still clamp.
            // Factors/weights are not pixel distances: no geometric epsilon.
            const double share = weight_sum > 0.0 ? weight / weight_sum : 0.0;
            const double candidate = using_grow
                ? result.flex_base_size + remaining_free_space * share
                : result.flex_base_size - std::fabs(remaining_free_space) * share;
            const double clamped = clamp_main_size(candidate, item);
            // Measure only constraint violations, never float rounding when
            // publishing the target. Rounding alone must not freeze an item.
            violations[position] = clamped - candidate;
            result.target_main_size = static_cast<float>(clamped);
            total_violation += violations[position];
        }

        // A tiny real clamp still changes which factors participate in the
        // next partial budget. Treat only exact zero as no violation; an
        // epsilon here can turn a subpixel floor into a visible size error.
        if (total_violation == 0.0) {
            for (const size_t index : line.item_indices) results[index].frozen = true;
            return;
        }

        bool froze_any = false;
        for (size_t position = 0; position < line.item_indices.size(); ++position) {
            auto& result = results[line.item_indices[position]];
            if (result.frozen) continue;
            const double violation = violations[position];
            if ((total_violation > 0.0 && violation > 0.0) ||
                (total_violation < 0.0 && violation < 0.0)) {
                result.frozen = true;
                froze_any = true;
            }
        }
        if (!froze_any) {
            for (const size_t index : line.item_indices) results[index].frozen = true;
            return;
        }
    }
}

} // namespace

float aether_flex_resolve_base_size(const aether_flex_basis_resolution_input_s& input) {
    switch (input.kind) {
        case AETHER_FLEX_BASIS_LENGTH:
            if (input.length_is_definite && std::isfinite(input.resolved_length)) {
                return std::max(0.f, input.resolved_length);
            }
            return non_negative(input.content_main_size);
        case AETHER_FLEX_BASIS_CONTENT:
            return non_negative(input.content_main_size);
        case AETHER_FLEX_BASIS_AUTO:
        default:
            return std::isfinite(input.specified_main_size)
                ? std::max(0.f, input.specified_main_size)
                : non_negative(input.content_main_size);
    }
}

float aether_flex_resolve_automatic_minimum(
    const aether_flex_automatic_minimum_input_s& input) {
    if (input.is_scroll_container) return 0.f;
    float result = non_negative(input.content_size_suggestion);
    if (std::isfinite(input.specified_size_suggestion)) {
        result = std::min(result, non_negative(input.specified_size_suggestion));
    }
    if (std::isfinite(input.transferred_size_suggestion)) {
        result = std::min(result, non_negative(input.transferred_size_suggestion));
    }
    if (std::isfinite(input.max_main_size)) {
        result = std::min(result, non_negative(input.max_main_size));
    }
    return result;
}

aether_flex_resolve_result_s aether_resolve_flexible_lengths(
    const aether_flex_resolve_input_s& input) {
    aether_flex_resolve_result_s output{};
    if (!input.items) return output;
    const auto& items = *input.items;
    output.items.resize(items.size());
    for (size_t index = 0; index < items.size(); ++index) {
        auto& result = output.items[index];
        result.flex_base_size = aether_flex_resolve_base_size(items[index].basis);
        // A border-box basis smaller than its decoration has a negative inner
        // base (§9.2). Retain it; flooring the base here changes grow allocation.
        result.inner_flex_base_size = result.flex_base_size - non_negative(items[index].main_decoration_size);
        result.hypothetical_main_size = static_cast<float>(clamp_main_size(result.flex_base_size, items[index]));
        result.target_main_size = result.flex_base_size;
    }

    aether_flex_line_s current{};
    const bool can_wrap = input.wrap && std::isfinite(input.available_main_size);
    for (size_t index = 0; index < items.size(); ++index) {
        const float outer_hypothetical = outer_size(output.items[index].hypothetical_main_size, items[index]);
        const float separator = current.item_indices.empty() ? 0.f : std::max(0.f, input.gap);
        if (can_wrap && !current.item_indices.empty() &&
            current.hypothetical_outer_main_size + separator + outer_hypothetical > input.available_main_size + epsilon) {
            output.lines.push_back(std::move(current));
            current = {};
        }
        if (!current.item_indices.empty()) current.hypothetical_outer_main_size += std::max(0.f, input.gap);
        current.item_indices.push_back(index);
        current.hypothetical_outer_main_size += outer_hypothetical;
    }
    if (!current.item_indices.empty()) output.lines.push_back(std::move(current));

    for (const auto& line : output.lines) {
        resolve_line(items, output.items, line, input.available_main_size, std::max(0.f, input.gap));
    }
    return output;
}
