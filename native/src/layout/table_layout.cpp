#include "table_layout.h"

#include <algorithm>
#include <cmath>

std::vector<float> aether_resolve_auto_table_columns(
    const std::vector<aether_table_column_contribution_s>& columns,
    float available_width)
{
    std::vector<float> widths;
    widths.reserve(columns.size());
    float min_total = 0.f;
    float preferred_total = 0.f;
    for (const auto& column : columns) {
        const float minimum = std::max(0.f, column.min_content);
        const float preferred = std::max(minimum, column.max_content);
        widths.push_back(minimum);
        min_total += minimum;
        preferred_total += preferred;
    }
    if (widths.empty()) return widths;

    const float target = std::max(0.f, available_width);
    if (target <= min_total) return widths;

    float remaining = target - min_total;
    const float flexible_total = std::max(0.f, preferred_total - min_total);
    if (flexible_total > 0.f) {
        const float growth = std::min(remaining, flexible_total);
        for (size_t index = 0; index < columns.size(); ++index) {
            const float minimum = std::max(0.f, columns[index].min_content);
            const float preferred = std::max(minimum, columns[index].max_content);
            widths[index] += growth * ((preferred - minimum) / flexible_total);
        }
        remaining -= growth;
    }

    // CSS automatic tables distribute surplus width after all preferred
    // contributions are satisfied. Equal distribution is stable, symmetric,
    // and does not privilege source order.
    if (remaining > 0.f) {
        const float share = remaining / static_cast<float>(widths.size());
        for (auto& width : widths) width += share;
    }
    return widths;
}
