#pragma once
#include <aether/aether.h>
#include "contextual_math.h"
#include "intrinsic_size_keyword.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <cmath>
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <optional>
#include <span>
#include <utility>

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
    int32_t flex_order = 0;
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
    std::unordered_map<std::string, aether::css::math_tree> contextual_lengths;
    std::unordered_map<std::string, aether::css::intrinsic_size_keyword> intrinsic_keywords;
};
// A unit conversion observes only these numeric inputs. It never owns or
// copies a node's lists/dependency table, and cannot mutate the document.
struct measure_inputs_s {
    float self_font_size;
    float viewport_width, viewport_height;
    float initial_font_size, root_font_size;
};
inline measure_inputs_s measure_make_inputs(const measure_state_s& state, float self_font_size) {
    const auto& d = *state.document;
    return {self_font_size, d.viewport_width, d.viewport_height, d.initial_font_size, d.root_font_size};
}
inline uint32_t measure_property_dependencies(const measure_state_s& state, const std::string& property) {
    const auto found = state.property_dependencies.find(property);
    return found == state.property_dependencies.end() ? 0 : found->second;
}
inline void measure_set_property_dependencies(measure_state_s& state, const std::string& property, uint32_t flags) {
    // Absence is the canonical representation of zero, including replacement
    // of inherited/contextual flags by an absolute declaration.
    if (flags) state.property_dependencies.insert_or_assign(property, flags);
    else state.property_dependencies.erase(property);
}
inline void measure_recompute_dependencies(measure_state_s& state) noexcept {
    state.dependencies = 0;
    for (const auto& property : state.property_dependencies) state.dependencies |= property.second;
}

// No engaged vectors, dependency map or document owner is constructed for a
// probe. Only the selected property's writes become owned local output.
struct measure_property_delta_s {
    std::optional<int32_t> flex_order;
    std::optional<float> self_font_size;
    std::optional<measure_line_height_s> line_height;
    std::optional<aether_css_length> row_gap, column_gap;
    std::optional<std::vector<measure_background_position_s>> background_positions;
    std::optional<std::vector<measure_background_size_s>> background_sizes;
    std::optional<uint32_t> property_dependencies;
    // Absent key = unchanged; null tree = erase; non-null = replacement.
    std::unordered_map<std::string, aether::css::math_tree> contextual_lengths;
    std::unordered_map<std::string, aether::css::intrinsic_size_keyword> intrinsic_keywords;
};

// Ephemeral, deeply const reads. A view never escapes the operation retaining
// its base. Do not retain a list span across replacement of that local output.
class measure_read_view_s {
    const measure_state_s* base_;
    const measure_property_delta_s* delta_;
    const std::string* property_;
public:
    explicit measure_read_view_s(const measure_state_s& base) noexcept
        : base_(&base), delta_(nullptr), property_(nullptr) {}
    measure_read_view_s(const measure_state_s& base, const measure_property_delta_s& delta,
                       const std::string& property) noexcept
        : base_(&base), delta_(&delta), property_(&property) {}
    aether::css::math_tree tree(const std::string& property) const {
        if (delta_) {
            const auto changed = delta_->contextual_lengths.find(property);
            if (changed != delta_->contextual_lengths.end()) return changed->second;
        }
        const auto found = base_->contextual_lengths.find(property);
        return found == base_->contextual_lengths.end() ? nullptr : found->second;
    }
    aether::css::intrinsic_size_keyword intrinsic_keyword(const std::string& name) const {
        if (delta_) { const auto i = delta_->intrinsic_keywords.find(name);
            if (i != delta_->intrinsic_keywords.end()) return i->second; }
        const auto i = base_->intrinsic_keywords.find(name);
        return i == base_->intrinsic_keywords.end() ? aether::css::intrinsic_size_keyword::none : i->second;
    }
    int32_t flex_order() const noexcept {
        return delta_ && delta_->flex_order ? *delta_->flex_order : base_->flex_order;
    }
    float parent_font_size() const noexcept { return base_->parent_font_size; }
    float self_font_size() const noexcept {
        return delta_ && delta_->self_font_size ? *delta_->self_font_size : base_->self_font_size;
    }
    measure_line_height_s line_height() const noexcept {
        return delta_ && delta_->line_height ? *delta_->line_height : base_->line_height;
    }
    aether_css_length row_gap() const noexcept {
        return delta_ && delta_->row_gap ? *delta_->row_gap : base_->row_gap;
    }
    aether_css_length column_gap() const noexcept {
        return delta_ && delta_->column_gap ? *delta_->column_gap : base_->column_gap;
    }
    std::span<const measure_background_position_s> background_positions() const noexcept {
        return delta_ && delta_->background_positions ? *delta_->background_positions : base_->background_positions;
    }
    std::span<const measure_background_size_s> background_sizes() const noexcept {
        return delta_ && delta_->background_sizes ? *delta_->background_sizes : base_->background_sizes;
    }
    uint32_t property_dependencies(const std::string& property) const {
        if (delta_ && delta_->property_dependencies && property == *property_)
            return *delta_->property_dependencies;
        return measure_property_dependencies(*base_, property);
    }
    measure_inputs_s inputs() const { return measure_make_inputs(*base_, self_font_size()); }
    measure_inputs_s inputs_with_self_font(float font) const { return measure_make_inputs(*base_, font); }
};

// The applicator has exactly one destination: the exclusive candidate builder
// or a property-local delta over a const candidate. There is no mutable access
// to a borrowed base, and a probe never needs to restore the candidate.
class measure_apply_state_s {
    measure_state_s* target_;
    const measure_state_s* base_;
    measure_property_delta_s* delta_;
    const std::string* property_;
public:
    measure_apply_state_s(measure_state_s& target, const std::string& property) noexcept
        : target_(&target), base_(&target), delta_(nullptr), property_(&property) {}
    measure_apply_state_s(const measure_state_s& base, measure_property_delta_s& delta,
                          const std::string& property) noexcept
        : target_(nullptr), base_(&base), delta_(&delta), property_(&property) {}
    measure_apply_state_s(const measure_apply_state_s&) = delete;
    measure_apply_state_s& operator=(const measure_apply_state_s&) = delete;
    measure_read_view_s read() const noexcept {
        return delta_ ? measure_read_view_s(*base_, *delta_, *property_) : measure_read_view_s(*base_);
    }
    void set_tree(const std::string& property, aether::css::math_tree tree) {
        if (target_) {
            if (tree) target_->contextual_lengths.insert_or_assign(property, std::move(tree));
            else target_->contextual_lengths.erase(property);
        } else delta_->contextual_lengths.insert_or_assign(property, std::move(tree));
    }
    void set_intrinsic_keyword(const std::string& name, aether::css::intrinsic_size_keyword value) {
        if (target_) {
            if (value == aether::css::intrinsic_size_keyword::none) target_->intrinsic_keywords.erase(name);
            else target_->intrinsic_keywords.insert_or_assign(name, value);
        } else delta_->intrinsic_keywords.insert_or_assign(name, value);
    }
    uint32_t property_dependencies() const { return read().property_dependencies(*property_); }
    void set_property_dependencies(uint32_t flags) {
        if (target_) measure_set_property_dependencies(*target_, *property_, flags);
        else delta_->property_dependencies = flags;
    }
    void set_self_font_size(float value) noexcept {
        if (target_) target_->self_font_size = value; else delta_->self_font_size = value;
    }
    void set_line_height(measure_line_height_s value) noexcept {
        if (target_) target_->line_height = value; else delta_->line_height = value;
    }
    void set_flex_order(int32_t value) noexcept {
        if (target_) target_->flex_order = value; else delta_->flex_order = value;
    }
    void set_row_gap(aether_css_length value) noexcept {
        if (target_) target_->row_gap = value; else delta_->row_gap = value;
    }
    void set_column_gap(aether_css_length value) noexcept {
        if (target_) target_->column_gap = value; else delta_->column_gap = value;
    }
    void set_background_positions(std::vector<measure_background_position_s> values) noexcept {
        if (target_) target_->background_positions = std::move(values);
        else delta_->background_positions.emplace(std::move(values));
    }
    void set_background_sizes(std::vector<measure_background_size_s> values) noexcept {
        if (target_) target_->background_sizes = std::move(values);
        else delta_->background_sizes.emplace(std::move(values));
    }
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
inline aether_css_length measure_compute_length(aether_css_length v, const measure_inputs_s& state) {
    if (v.unit == AETHER_CSS_UNIT_AUTO || v.unit == AETHER_CSS_UNIT_PERCENT) return v;
    double px = v.value;
    double percent = 0;
    switch (v.unit) {
    case AETHER_CSS_UNIT_EM: px *= state.self_font_size; break;
    case AETHER_CSS_UNIT_REM: px *= state.root_font_size; break;
    case AETHER_CSS_UNIT_VW: px *= state.viewport_width / 100.; break;
    case AETHER_CSS_UNIT_VH: px *= state.viewport_height / 100.; break;
    case AETHER_CSS_UNIT_LINEAR:
        px += double(v.linear_em) * state.self_font_size + double(v.linear_rem) * state.root_font_size +
              double(v.linear_vw) * state.viewport_width / 100. + double(v.linear_vh) * state.viewport_height / 100.;
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
inline aether_css_length measure_compute_length(aether_css_length v, const measure_state_s& state) {
    if (v.unit == AETHER_CSS_UNIT_AUTO || v.unit == AETHER_CSS_UNIT_PERCENT) return v;
    return measure_compute_length(v, measure_make_inputs(state, state.self_font_size));
}
enum class measure_used_kind_e { resolved, automatic, deferred_percentage, unbounded, invalid };
struct measure_used_length_s { measure_used_kind_e kind; float px; };
inline bool measure_has_percentage(const measure_state_s* state, const char* property, aether_css_length value) {
    if (value.unit == AETHER_CSS_UNIT_PERCENT ||
        (value.unit == AETHER_CSS_UNIT_LINEAR && value.linear_percent != 0)) return true;
    if (!state || !property) return false;
    const auto tree = state->contextual_lengths.find(property);
    if (tree != state->contextual_lengths.end() && aether::css::math_has_percentage(tree->second)) return true;
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

inline void measure_apply_background_longhands(aether_computed_style_t& style, measure_read_view_s m) {
    const auto positions = m.background_positions();
    const auto sizes = m.background_sizes();
    for (int32_t i = 0; i < style.background_layers.count; ++i) {
        auto& layer = style.background_layers.items[i];
        const auto& position = positions[static_cast<size_t>(i) % positions.size()];
        const auto& size = sizes[static_cast<size_t>(i) % sizes.size()];
        layer.position_x = position.x; layer.position_y = position.y;
        layer.size_kind = size.kind; layer.size_x = size.x; layer.size_y = size.y;
    }
}

inline void measure_apply_background_longhands(aether_computed_style_t& style, const measure_state_s& m) {
    measure_apply_background_longhands(style, measure_read_view_s(m));
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
