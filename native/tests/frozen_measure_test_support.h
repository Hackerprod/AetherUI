#pragma once
#include "../src/css/frozen_style.h"
#include "../src/css/measure_state.h"
#include <aether/aether_contextual_style.h>
#include <stdexcept>

namespace frozen_measure_test {
// Test-only independent logical snapshot. No production reader thaws measures,
// and no pointer in this copy aliases the immutable source's storage.
inline measure_state_s snapshot(const aether::style::FrozenMeasures& source) {
    measure_state_s result;
    const auto& s = source.scalars(); const auto& d = source.document().values();
    result.document = std::make_shared<const document_measure_context_s>(
        document_measure_context_s{d.viewport_width, d.viewport_height, d.initial_font_size,
            d.root_font_size, d.generation, d.identity});
    result.parent_font_size = s.parent_font_size; result.self_font_size = s.self_font_size;
    switch (s.line_height_kind) {
    case aether::style::FrozenLineHeightKind::normal: result.line_height.kind = measure_line_height_kind_e::normal; break;
    case aether::style::FrozenLineHeightKind::number: result.line_height.kind = measure_line_height_kind_e::number; break;
    case aether::style::FrozenLineHeightKind::absolute_css_px: result.line_height.kind = measure_line_height_kind_e::absolute_css_px; break;
    }
    result.line_height.value = s.line_height_value;
    result.row_gap = s.row_gap; result.column_gap = s.column_gap; result.dependencies = s.dependencies;
    result.background_positions.clear(); result.background_sizes.clear();
    for (const auto& p : source.background_positions()) result.background_positions.push_back({p.x,p.y});
    for (const auto& p : source.background_sizes()) result.background_sizes.push_back({p.kind,p.x,p.y});
    for (const auto& p : source.property_dependencies()) {
        const auto name = p.name();
        if (!result.property_dependencies.emplace(std::string(name.c_str(), name.size()), p.flags()).second)
            throw std::runtime_error("duplicate frozen dependency in test snapshot");
    }
    return result;
}
inline float used_line_height(aether_computed_style_handle_t handle) {
    float value = 0;
    if (aether_computed_style_contextual_metrics(handle, &value, nullptr, nullptr, nullptr, nullptr, nullptr) != AETHER_OK)
        throw std::runtime_error("public contextual metrics failed");
    return value;
}
inline bool has_percentage(const aether::style::FrozenMeasures* state, const char* property, aether_css_length value) {
    if (value.unit == AETHER_CSS_UNIT_PERCENT ||
        (value.unit == AETHER_CSS_UNIT_LINEAR && value.linear_percent != 0)) return true;
    return state && property && (state->dependency(property) & MEASURE_PERCENTAGE);
}
}
