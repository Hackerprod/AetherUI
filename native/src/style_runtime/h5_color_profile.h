#pragma once
#include "../css/color_rgba_internal.h"
#include "../css/css_length_internal.h"
#include "css_component_values.h"
#include "shadow_values.h"
#include <cmath>

// Admission is a whole declaration, not an allow-list of function names.
// Geometry-dependent shadow support remains checked by native preparation.
inline bool h5_rgba_declaration_supported(const std::string& property, const std::string& source) {
    aether_color_rgba color{};
    if (property == "color" || property == "background-color" ||
        property == "border-top-color" || property == "border-right-color" ||
        property == "border-bottom-color" || property == "border-left-color")
        return aether::css::parse_legacy_rgba_strict(source, color);
    if (property != "box-shadow") return false;
    bool valid = false;
    const auto groups = css_split_component_delimited(source, ',', &valid);
    if (!valid || groups.empty() || groups.size() > 32) return false;
    bool has_rgba = false;
    for (const auto& group : groups) {
        const auto parts = css_split_components(group, &valid);
        if (!valid) return false;
        for (const auto& part : parts) {
            if (aether::css::parse_legacy_rgba_strict(part, color)) { has_rgba = true; continue; }
            // No other functions, currentColor, inset, contextual units, or
            // named-color shortcuts are introduced by this narrow H5 gate.
            aether::css::calc_value_s literal;
            double original = 0;
            if (!aether::css::try_parse_numeric_literal(part, literal, &original) ||
                !std::isfinite(original)) return false;
            aether_css_length length{};
            if (!aether_css_length_try_parse(part.c_str(), &length) ||
                length.unit != AETHER_CSS_UNIT_PX || !std::isfinite(length.value)) return false;
        }
    }
    parsed_shadow_list parsed;
    shadow_storage_policy limits; limits.max_count = 32;
    if (!has_rgba || shadow_list_parse(source, true, parsed, limits) != shadow_value_error::none) return false;
    for (const auto& item : parsed.items) if (item.inset) return false;
    return true;
}
