#include "flex_resolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float epsilon = 0.0001f;

float non_negative(float value) {
    return std::isfinite(value) ? std::max(0.f, value) : 0.f;
}

float clamp_main_size(float value, const aether_flex_item_input_s& item) {
    const float minimum = std::isfinite(item.min_main_size)
        ? non_negative(item.min_main_size)
        : aether_flex_resolve_automatic_minimum(item.automatic_minimum);
    const float maximum = std::isfinite(item.max_main_size)
        ? std::max(minimum, item.max_main_size)
        : std::numeric_limits<float>::infinity();
    return std::clamp(std::max(0.f, value), minimum, maximum);
}

float outer_size(float main_size, const aether_flex_item_input_s& item) {
    return main_size + non_negative(item.outer_main_size);
}

float line_outer_size(const std::vector<aether_flex_item_input_s>& items,
                      const std::vector<aether_flex_item_result_s>& results,
                      const aether_flex_line_s& line, bool use_target, float gap) {
    float total = gap * static_cast<float>(line.item_indices.empty() ? 0 : line.item_indices.size() - 1);
    for (const size_t index : line.item_indices) {
        const auto& result = results[index];
        total += outer_size(use_target ? result.target_main_size : result.flex_base_size, items[index]);
    }
    return total;
}

void resolve_line(const std::vector<aether_flex_item_input_s>& items,
                  std::vector<aether_flex_item_result_s>& results,
                  const aether_flex_line_s& line, float available_main_size, float gap) {
    if (!std::isfinite(available_main_size)) return;

    const bool using_grow = line.hypothetical_outer_main_size < available_main_size - epsilon;
    for (const size_t index : line.item_indices) {
        auto& result = results[index];
        result.target_main_size = result.flex_base_size;
        const float factor = using_grow ? items[index].flex_grow
                                        : items[index].flex_shrink * result.flex_base_size;
        // Flexbox §9.7's initial freeze applies the hypothetical size, not the
        // flex base. This matters for flex:1 (0% base) whose automatic minimum
        // is positive: in a negative-free-space line it is inflexible because
        // scaled shrink is zero, but it must retain that minimum.
        const bool violates_hypothetical = using_grow
            ? result.flex_base_size > result.hypothetical_main_size + epsilon
            : result.flex_base_size < result.hypothetical_main_size - epsilon;
        result.frozen = factor <= epsilon || violates_hypothetical;
        if (result.frozen) result.target_main_size = result.hypothetical_main_size;
    }

    const float initial_free_space = available_main_size -
        line_outer_size(items, results, line, false, gap);

    while (true) {
        float remaining_free_space = available_main_size;
        remaining_free_space -= gap * static_cast<float>(line.item_indices.empty() ? 0 : line.item_indices.size() - 1);
        float factor_sum = 0.f;
        int unfrozen_count = 0;
        for (const size_t index : line.item_indices) {
            const auto& item = items[index];
            const auto& result = results[index];
            remaining_free_space -= outer_size(result.frozen ? result.target_main_size : result.flex_base_size, item);
            if (!result.frozen) {
                ++unfrozen_count;
                factor_sum += using_grow ? item.flex_grow : item.flex_shrink * result.flex_base_size;
            }
        }
        if (unfrozen_count == 0) return;

        if (using_grow && factor_sum < 1.f) {
            const float scaled_initial_free_space = initial_free_space * factor_sum;
            if (std::fabs(scaled_initial_free_space) < std::fabs(remaining_free_space)) {
                remaining_free_space = scaled_initial_free_space;
            }
        }

        if (factor_sum <= epsilon) {
            for (const size_t index : line.item_indices) results[index].frozen = true;
            return;
        }

        float total_violation = 0.f;
        std::vector<float> unclamped(items.size(), 0.f);
        for (const size_t index : line.item_indices) {
            auto& result = results[index];
            if (result.frozen) continue;
            const auto& item = items[index];
            const float factor = using_grow ? item.flex_grow : item.flex_shrink * result.flex_base_size;
            const float candidate = using_grow
                ? result.flex_base_size + remaining_free_space * factor / factor_sum
                : result.flex_base_size - std::fabs(remaining_free_space) * factor / factor_sum;
            unclamped[index] = candidate;
            result.target_main_size = clamp_main_size(candidate, item);
            total_violation += result.target_main_size - candidate;
        }

        if (std::fabs(total_violation) <= epsilon) {
            for (const size_t index : line.item_indices) results[index].frozen = true;
            return;
        }

        bool froze_any = false;
        for (const size_t index : line.item_indices) {
            auto& result = results[index];
            if (result.frozen) continue;
            const float violation = result.target_main_size - unclamped[index];
            if ((total_violation > 0.f && violation > epsilon) ||
                (total_violation < 0.f && violation < -epsilon)) {
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
        result.hypothetical_main_size = clamp_main_size(result.flex_base_size, items[index]);
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
