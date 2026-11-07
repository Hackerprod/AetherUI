#pragma once
#include <aether/aether.h>
#include <memory>
#include <unordered_map>
#include <string>
#include <cmath>
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <vector>

struct document_measure_context_s {
    float viewport_width = 0, viewport_height = 0;
    float initial_font_size = 16, root_font_size = 16;
    uint64_t generation = 0, identity = 0;
};
enum class measure_line_height_kind_e { normal, number, absolute_css_px };
struct measure_line_height_s {
    measure_line_height_kind_e kind = measure_line_height_kind_e::normal;
    float value = 1.2f;
};
enum measure_dependency_e : uint32_t {
    MEASURE_SELF_FONT = 1, MEASURE_ROOT_FONT = 2, MEASURE_VIEWPORT = 4,
    MEASURE_PERCENTAGE = 8, MEASURE_PARENT = 16
};
struct measure_background_position_s {
    aether_css_length x{0, AETHER_CSS_UNIT_PERCENT, 0, 0, 0, 0, 0};
    aether_css_length y{0, AETHER_CSS_UNIT_PERCENT, 0, 0, 0, 0, 0};
};
struct measure_background_size_s {
    int32_t kind = AETHER_BACKGROUND_SIZE_AUTO;
    aether_css_length x = aether_css_length_auto();
    aether_css_length y = aether_css_length_auto();
};
struct measure_state_s {
    std::shared_ptr<const document_measure_context_s> document;
    float parent_font_size = 16, self_font_size = 16;
    measure_line_height_s line_height;
    aether_css_length row_gap = aether_css_length_zero();
    aether_css_length column_gap = aether_css_length_zero();
    // Longhand lists survive even when background-image has no painted layers.
    std::vector<measure_background_position_s> background_positions{measure_background_position_s{}};
    std::vector<measure_background_size_s> background_sizes{measure_background_size_s{}};
    uint32_t dependencies = 0;
    std::unordered_map<std::string, uint32_t> property_dependencies;
};
inline uint32_t measure_source_dependencies(std::string source) {
    for (auto& c : source) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    uint32_t flags = source.find('%') != std::string::npos ? MEASURE_PERCENTAGE : 0;
    if (source.find("rem") != std::string::npos) flags |= MEASURE_ROOT_FONT;
    // Scan unit tokens so rem does not accidentally imply self font.
    for (size_t p = 0; (p = source.find("em", p)) != std::string::npos; p += 2)
        if (!p || source[p - 1] != 'r') flags |= MEASURE_SELF_FONT;
    if (source.find("vw") != std::string::npos || source.find("vh") != std::string::npos) flags |= MEASURE_VIEWPORT;
    return flags;
}
inline aether_css_length measure_compute_length(aether_css_length v, const measure_state_s& state) {
    if (v.unit == AETHER_CSS_UNIT_AUTO || v.unit == AETHER_CSS_UNIT_PERCENT) return v;
    const auto& d = *state.document;
    double px = v.value;
    double percent = 0;
    switch (v.unit) {
    case AETHER_CSS_UNIT_EM: px *= state.self_font_size; break;
    case AETHER_CSS_UNIT_REM: px *= d.root_font_size; break;
    case AETHER_CSS_UNIT_VW: px *= d.viewport_width / 100.; break;
    case AETHER_CSS_UNIT_VH: px *= d.viewport_height / 100.; break;
    case AETHER_CSS_UNIT_LINEAR:
        px += double(v.linear_em) * state.self_font_size + double(v.linear_rem) * d.root_font_size +
              double(v.linear_vw) * d.viewport_width / 100. + double(v.linear_vh) * d.viewport_height / 100.;
        percent = v.linear_percent; break;
    default: break;
    }
    if (!std::isfinite(px) || std::abs(px) > std::numeric_limits<float>::max())
        throw std::overflow_error("Non-finite computed CSS length");
    auto result = aether_css_length_zero();
    result.unit = v.unit == AETHER_CSS_UNIT_LINEAR ? AETHER_CSS_UNIT_LINEAR : AETHER_CSS_UNIT_PX;
    result.value = static_cast<float>(px); result.linear_percent = static_cast<float>(percent);
    return result;
}
enum class measure_used_kind_e { resolved, automatic, deferred_percentage, unbounded, invalid };
struct measure_used_length_s { measure_used_kind_e kind; float px; };
inline bool measure_has_percentage(const measure_state_s* state, const char* property, aether_css_length value) {
    if (value.unit == AETHER_CSS_UNIT_PERCENT ||
        (value.unit == AETHER_CSS_UNIT_LINEAR && value.linear_percent != 0)) return true;
    if (!state || !property) return false;
    const auto found = state->property_dependencies.find(property);
    return found != state->property_dependencies.end() && (found->second & MEASURE_PERCENTAGE);
}
inline measure_used_length_s measure_resolve_used_length(aether_css_length v, float basis, bool definite,
                                                        bool has_percentage = false) {
    if (v.unit == AETHER_CSS_UNIT_AUTO) return {measure_used_kind_e::automatic, 0};
    if (v.unit != AETHER_CSS_UNIT_PX && v.unit != AETHER_CSS_UNIT_PERCENT && v.unit != AETHER_CSS_UNIT_LINEAR)
        return {measure_used_kind_e::invalid, 0};
    const double p = v.unit == AETHER_CSS_UNIT_PERCENT ? v.value : v.linear_percent;
    if ((p != 0 || v.unit == AETHER_CSS_UNIT_PERCENT || has_percentage) && !definite)
        return {measure_used_kind_e::deferred_percentage, 0};
    const double px = (v.unit == AETHER_CSS_UNIT_PERCENT ? 0 : v.value) + p * basis / 100.;
    if (!std::isfinite(px) || std::abs(px) > std::numeric_limits<float>::max()) return {measure_used_kind_e::invalid, 0};
    return {measure_used_kind_e::resolved, static_cast<float>(px)};
}
inline float measure_used_line_height(const measure_state_s* state, const aether_computed_style_t& flat) {
    if (!state) return flat.font_size * (flat.line_height > 0 ? flat.line_height : 1.2f);
    return state->line_height.kind == measure_line_height_kind_e::absolute_css_px ? state->line_height.value :
        state->self_font_size * (state->line_height.kind == measure_line_height_kind_e::normal ? 1.2f : state->line_height.value);
}

inline void measure_apply_background_longhands(aether_computed_style_t& style, const measure_state_s& m) {
    for (int32_t i = 0; i < style.background_layers.count; ++i) {
        auto& layer = style.background_layers.items[i];
        const auto& position = m.background_positions[static_cast<size_t>(i) % m.background_positions.size()];
        const auto& size = m.background_sizes[static_cast<size_t>(i) % m.background_sizes.size()];
        layer.position_x = position.x; layer.position_y = position.y;
        layer.size_kind = size.kind; layer.size_x = size.x; layer.size_y = size.y;
    }
}

// Computed values freeze font/viewport-dependent components once. Percentages
// remain symbolic until the geometry-specific consumer has its actual basis.
inline void measure_compute_paint_lengths(aether_computed_style_t& style, const measure_state_s& m) {
    measure_apply_background_longhands(style, m);
    const auto convert = [&](aether_css_length& v) { v = measure_compute_length(v, m); };
    for (int32_t i = 0; i < style.transforms.count; ++i) {
        if (style.transforms.items[i].kind == AETHER_TRANSFORM_TRANSLATE) {
            convert(style.transforms.items[i].x); convert(style.transforms.items[i].y);
        }
    }
    convert(style.transform_origin.x); convert(style.transform_origin.y);
    for (auto* corner : {&style.border_radii.top_left, &style.border_radii.top_right,
                         &style.border_radii.bottom_right, &style.border_radii.bottom_left}) {
        convert(corner->horizontal); convert(corner->vertical);
    }
    const auto gradient = [&](aether_linear_gradient& g) {
        for (int32_t i = 0; i < g.stop_count; ++i) if (g.stops[i].has_position) convert(g.stops[i].position);
    };
    for (int32_t i = 0; i < style.background_layers.count; ++i) {
        auto& layer = style.background_layers.items[i];
        convert(layer.position_x); convert(layer.position_y);
        convert(layer.size_x); convert(layer.size_y);
        gradient(layer.gradient);
    }
    gradient(style.text_gradient);
    for (int32_t i = 0; i < style.backdrop_filters.count; ++i)
        convert(style.backdrop_filters.items[i].length_amount);
}
