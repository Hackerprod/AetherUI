#include "shadow_values.h"
#include "../css/measure_state.h"
#include "../css/css_length_internal.h"
#include "css_component_values.h"
#include "style_string_utils_internal.h"
#include <cmath>
#include <cstring>
#include <utility>

namespace {
static shadow_value_error parse_component(const std::string& source,
                                                      bool allow_inset, bool allow_spread,
                                                      aether_shadow& shadow,
                                                      bool& uses_current_color, const measure_inputs_s* measures) {
    std::vector<std::string> tokens;
    bool valid = false;
    tokens = css_split_components(source, &valid);
    if (!valid || tokens.empty()) return shadow_value_error::invalid;

    shadow = {};
    uses_current_color = true; // CSS default: the element's final currentColor.
    bool has_inset = false;
    bool has_color = false;
    int length_count = 0;
    bool lengths_closed = false;
    for (const auto& token : tokens) {
        if (str_iequals(token, "inset")) {
            if (!allow_inset) return shadow_value_error::inset_not_allowed;
            if (has_inset) return shadow_value_error::invalid;
            has_inset = true;
            if (length_count) lengths_closed = true;
            shadow.inset = AETHER_TRUE;
            continue;
        }
        if (str_iequals(token, "currentColor")) {
            if (has_color) return shadow_value_error::invalid;
            has_color = true;
            if (length_count) lengths_closed = true;
            uses_current_color = true;
            continue;
        }
        aether_color_rgba color;
        if (aether_color_rgba_try_parse(token.c_str(), &color)) {
            if (has_color) return shadow_value_error::invalid;
            has_color = true;
            if (length_count) lengths_closed = true;
            uses_current_color = false;
            shadow.color = color;
            continue;
        }
        // Color and inset may surround, but never split, the length group.
        if (lengths_closed) return shadow_value_error::invalid;
        aether_css_length length;
        if (!aether_css_length_try_parse(token.c_str(), &length) ||
            length.unit == AETHER_CSS_UNIT_AUTO || length.unit == AETHER_CSS_UNIT_PERCENT ||
            length_count == 4) return shadow_value_error::invalid;
        if (!allow_spread && length_count == 3) return shadow_value_error::spread_not_allowed;
        const bool calc = token.size() >= 5 && str_iequals(token.substr(0,5), "calc(");
        if (length_count == 2 && !calc) {
            aether::css::calc_value_s literal;
            double original_number = 0.0;
            if (!aether::css::try_parse_numeric_literal(token, literal, &original_number) ||
                original_number < 0.0) return shadow_value_error::invalid;
        }
        // A contextual runtime never borrows parser compatibility defaults.
        float pixels = 0;
        if (measures) {
            const auto used = measure_resolve_used_length(measure_compute_length(length, *measures), 0, false);
            if (used.kind != measure_used_kind_e::resolved) return shadow_value_error::invalid;
            pixels = used.px;
        } else pixels = aether_css_length_resolve(length, 0, 16, 16, 1920, 1080);
        if (!std::isfinite(pixels) || (length_count == 2 && pixels < 0 && !calc)) return shadow_value_error::invalid;
        if (length_count == 2) pixels = std::max(0.f, pixels);
        if (length_count == 0) shadow.offset_x = pixels;
        else if (length_count == 1) shadow.offset_y = pixels;
        else if (length_count == 2) shadow.blur_radius = pixels;
        else shadow.spread_radius = pixels;
        ++length_count;
    }
    return length_count >= 2 ? shadow_value_error::none : shadow_value_error::invalid;
}

}

shadow_value_error shadow_list_parse(const std::string& source, bool box,
    parsed_shadow_list& output, const shadow_storage_policy& storage, const measure_inputs_s* measures) {
    parsed_shadow_list parsed;
    const auto value = str_trim(source);
    if (str_iequals(value, "none")) {
        output = std::move(parsed);
        return shadow_value_error::none;
    }
    bool valid = false;
    const auto components = css_split_component_delimited(value, ',', &valid);
    if (!valid || components.empty()) return shadow_value_error::invalid;
    // Both public count representation and multiplication must be safe. The
    // smaller injectable limits exercise these guards without giant inputs.
    const size_t count = components.size();
    if (count > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
        count > std::numeric_limits<size_t>::max() / sizeof(aether_shadow) ||
        count > storage.max_count || count > storage.max_bytes / sizeof(aether_shadow))
        return shadow_value_error::too_many;
    parsed.items.reserve(count);
    parsed.uses_current_color.reserve(count);
    for (const auto& component : components) {
        aether_shadow shadow{};
        bool current_color = false;
        const auto error = parse_component(component, box, box, shadow, current_color, measures);
        if (error != shadow_value_error::none) return error;
        parsed.items.push_back(shadow);
        parsed.uses_current_color.push_back(current_color);
    }
    output = std::move(parsed);
    return shadow_value_error::none;
}

shadow_value_error shadow_list_apply(const std::string& source, bool box,
    aether_shadow_list& destination, std::vector<bool>& references,
    const shadow_storage_policy& storage, const measure_inputs_s* measures) {
    parsed_shadow_list parsed;
    const auto error = shadow_list_parse(source, box, parsed, storage, measures);
    if (error != shadow_value_error::none) return error;
    aether_shadow* replacement = nullptr;
    if (!parsed.items.empty()) {
        if (!storage.allocate || !storage.release) return shadow_value_error::allocation;
        replacement = static_cast<aether_shadow*>(storage.allocate(sizeof(aether_shadow) * parsed.items.size()));
        if (!replacement) return shadow_value_error::allocation;
        std::memcpy(replacement, parsed.items.data(), sizeof(aether_shadow) * parsed.items.size());
    }
    // No fallible operation remains after staging the allocation and flags.
    // Keep the old storage alive through validation and replacement allocation.
    if (!storage.release) return shadow_value_error::allocation;
    if (destination.items) storage.release(destination.items);
    destination = {replacement, static_cast<int32_t>(parsed.items.size())};
    references.swap(parsed.uses_current_color);
    return shadow_value_error::none;
}

const char* shadow_value_diagnostic_code(bool box) {
    return box ? "AETHER-CSS-BOX-SHADOW" : "AETHER-CSS-TEXT-SHADOW";
}
const char* shadow_value_error_message(shadow_value_error error, bool box) {
    switch (error) {
    case shadow_value_error::inset_not_allowed: return "The inset keyword is not valid for text-shadow.";
    case shadow_value_error::spread_not_allowed: return "A spread radius is not valid for text-shadow.";
    case shadow_value_error::too_many: return "The shadow list exceeds its storage limits.";
    case shadow_value_error::allocation: return "The shadow list could not be allocated.";
    default: return box
        ? "Each box shadow requires two to four lengths and an optional color or inset keyword."
        : "Each text shadow requires two or three lengths and an optional color.";
    }
}
