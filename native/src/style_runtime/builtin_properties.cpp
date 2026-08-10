#include "style_property_registry_internal.h"
#include "../css/border_utils.h"
#include "../css/border_radii.h"
#include "../css/background_layers.h"
#include "../css/backdrop_filters.h"
#include "../css/linear_gradient.h"
#include "../css/transforms.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

// ═══ Parse helpers ═════════════════════════════════════════════

static bool bi_is_global_keyword(const std::string& s) {
    return str_iequals(s, "initial") || str_iequals(s, "inherit") ||
           str_iequals(s, "unset")   || str_iequals(s, "revert");
}

static std::vector<std::string> bi_split_ws(const std::string& s) {
    std::vector<std::string> result;
    std::string current;
    int32_t depth = 0;
    char quote = '\0';
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (quote != '\0') {
            if (c == '\\' && i + 1 < s.size()) { current += c; current += s[++i]; continue; }
            if (c == quote) quote = '\0';
            current += c; continue;
        }
        if (c == '\'' || c == '"') { quote = c; current += c; continue; }
        if (c == '(' || c == '[' || c == '{') { ++depth; current += c; continue; }
        if (c == ')' || c == ']' || c == '}') { depth = std::max(0, depth - 1); current += c; continue; }
        if (depth == 0 && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
            if (!current.empty()) { result.push_back(current); current.clear(); }
            continue;
        }
        current += c;
    }
    if (!current.empty()) result.push_back(current);
    return result;
}

static bool bi_try_keyword(const char* src, const char* const* keywords, int count, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    for (int i = 0; i < count; ++i) {
        if (str_iequals(t, keywords[i])) { out = keywords[i]; return true; }
    }
    return false;
}

static bool bi_try_length_or_auto(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    if (str_iequals(t, "auto")) { out = "auto"; return true; }
    aether_css_length l;
    if (aether_css_length_try_parse(t.c_str(), &l) && l.unit != AETHER_CSS_UNIT_AUTO) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_non_negative_length(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    aether_css_length l;
    if (aether_css_length_try_parse(t.c_str(), &l) && l.unit != AETHER_CSS_UNIT_AUTO) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_size(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    if (str_iequals(t, "auto")) { out = "auto"; return true; }
    aether_css_length l;
    if (aether_css_length_try_parse(t.c_str(), &l) && l.unit != AETHER_CSS_UNIT_AUTO) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_flex_basis(const char* source, std::string& output) {
    const std::string value = str_trim(source ? source : "");
    if (str_iequals(value, "content")) { output = "content"; return true; }
    return bi_try_size(source, output);
}

static bool bi_try_max_size(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    if (str_iequals(t, "auto") || str_iequals(t, "none")) { out = t; return true; }
    aether_css_length l;
    if (aether_css_length_try_parse(t.c_str(), &l) && l.unit != AETHER_CSS_UNIT_AUTO) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_length(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    aether_css_length l;
    if (aether_css_length_try_parse(t.c_str(), &l) && l.unit != AETHER_CSS_UNIT_AUTO) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_color(const char* src, std::string& out) {
    if (!src || !*src) return false;
    aether_color_rgba c;
    std::string t = str_trim(src);
    if (str_iequals(t, "currentcolor")) { out = "currentColor"; return true; }
    if (aether_color_rgba_try_parse(t.c_str(), &c)) { out = t; return true; }
    return false;
}

static bool bi_try_non_negative_number(const char* src, std::string& out) {
    if (!src || !*src) return false;
    char* end = nullptr;
    float v = std::strtof(src, &end);
    if (end == src) return false;
    while (*end == ' ') ++end;
    if (*end != '\0') return false;
    if (v < 0.0f) return false;
    out = str_trim(src);
    return true;
}

static bool bi_try_integer(const char* src, std::string& out) {
    if (!src || !*src) return false;
    char* end = nullptr;
    long v = std::strtol(src, &end, 10);
    if (end == src) return false;
    while (*end == ' ') ++end;
    if (*end != '\0') return false;
    out = std::to_string(v);
    return true;
}

static bool bi_try_nullable_integer(const char* src, std::string& out) {
    std::string t = str_trim(src);
    if (t.empty()) { out = ""; return true; }
    return bi_try_integer(src, out);
}

static bool bi_try_opacity(const char* src, std::string& out) {
    if (!src || !*src) return false;
    char* end = nullptr;
    float v = std::strtof(src, &end);
    if (end == src) return false;
    while (*end == ' ') ++end;
    if (*end != '\0') return false;
    if (v < 0.0f || v > 1.0f) return false;
    out = std::to_string(v);
    return true;
}

/* CSS <time>: an optional sign-less number followed by s or ms. A bare zero
   is the only unitless form the spec permits (0 is valid without a unit). */
static bool bi_try_time(const char* src, float& seconds) {
    if (!src || !*src) return false;
    const std::string t = str_trim(src);
    if (t.empty()) return false;
    char* end = nullptr;
    const float value = std::strtof(t.c_str(), &end);
    if (end == t.c_str() || value < 0.0f) return false;
    const std::string unit = str_trim(end);
    if (unit.empty()) {
        if (value != 0.0f) return false;
        seconds = 0.0f;
        return true;
    }
    if (str_iequals(unit, "s")) { seconds = value; return true; }
    if (str_iequals(unit, "ms")) { seconds = value / 1000.0f; return true; }
    return false;
}

/* transition-property longhand: only the v1 keywords map to bits. Any other
   property name is a valid CSS transition-property that names no animatable
   property in this engine, so it normalizes to the empty bitmask (0). */
static bool bi_try_transition_property_keyword(const char* src, std::string& out) {
    if (!src || !*src) return false;
    const std::string t = str_trim(src);
    if (str_iequals(t, "opacity")) { out = std::to_string(AETHER_TRANSITION_PROPERTY_OPACITY); return true; }
    if (str_iequals(t, "transform")) { out = std::to_string(AETHER_TRANSITION_PROPERTY_TRANSFORM); return true; }
    if (str_iequals(t, "all")) {
        out = std::to_string(AETHER_TRANSITION_PROPERTY_OPACITY | AETHER_TRANSITION_PROPERTY_TRANSFORM);
        return true;
    }
    if (str_iequals(t, "none")) { out = "0"; return true; }
    return false;
}

static bool bi_try_transition_timing(const char* src, std::string& out) {
    if (!src || !*src) return false;
    const std::string t = str_trim(src);
    if (str_iequals(t, "linear")) { out = std::to_string(AETHER_TRANSITION_TIMING_LINEAR); return true; }
    if (str_iequals(t, "ease")) { out = std::to_string(AETHER_TRANSITION_TIMING_EASE); return true; }
    if (str_iequals(t, "ease-in")) { out = std::to_string(AETHER_TRANSITION_TIMING_EASE_IN); return true; }
    if (str_iequals(t, "ease-out")) { out = std::to_string(AETHER_TRANSITION_TIMING_EASE_OUT); return true; }
    if (str_iequals(t, "ease-in-out")) { out = std::to_string(AETHER_TRANSITION_TIMING_EASE_IN_OUT); return true; }
    return false;
}

static bool bi_try_transition_time_text(const char* src, std::string& out) {
    float seconds = 0.0f;
    if (!bi_try_time(src, seconds)) return false;
    out = std::to_string(seconds);
    return true;
}

static bool bi_try_font_weight(const char* src, std::string& out) {
    std::string t = str_trim(src);
    static const char* kw[] = {
        "thin","extra-light","light","normal","medium","semi-bold","bold","extra-bold","black"
    };
    static const int weights[] = {100,200,300,400,500,600,700,800,900};
    for (int i = 0; i < 9; ++i) {
        if (str_iequals(t, kw[i])) { out = std::to_string(weights[i]); return true; }
    }
    char* end = nullptr;
    long v = std::strtol(src, &end, 10);
    if (end == src) return false;
    while (*end == ' ') ++end;
    if (*end != '\0') return false;
    if (v < 100 || v > 900) return false;
    out = std::to_string(v);
    return true;
}

static bool bi_try_font_size(const char* src, std::string& out) {
    std::string t = str_trim(src);
    static const char* sizes[] = {
        "xx-small","x-small","small","medium","large","x-large","xx-large","smaller","larger"
    };
    for (auto& s : sizes) { if (str_iequals(t, s)) { out = t; return true; } }
    aether_css_length l;
    if (aether_css_length_try_parse(t.c_str(), &l) && l.unit != AETHER_CSS_UNIT_AUTO) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_font_family(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    if (t.empty()) return false;
    // The ABI carries one resolved family, rather than the CSS fallback list. Choose the first
    // family and normalize CSS quoting so platform text APIs receive a real family name instead
    // of the literal CSS token (for example, "'Segoe UI', sans-serif").
    auto families = split_top_level(t, ',');
    if (families.empty()) return false;
    out = str_trim(families.front());
    if (out.size() >= 2 && ((out.front() == '\'' && out.back() == '\'') ||
                            (out.front() == '"' && out.back() == '"'))) {
        out = out.substr(1, out.size() - 2);
    }
    return !out.empty();
}

static bool bi_try_line_height(const char* src, std::string& out) {
    std::string t = str_trim(src);
    if (str_iequals(t, "normal")) { out = "normal"; return true; }
    aether_css_length l;
    if (aether_css_length_try_parse(t.c_str(), &l) && l.unit != AETHER_CSS_UNIT_AUTO) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_border_width(const char* src, std::string& out) {
    std::string t = str_trim(src);
    static const char* kw[] = { "thin", "medium", "thick" };
    for (auto& k : kw) { if (str_iequals(t, k)) { out = t; return true; } }
    return bi_try_non_negative_length(src, out);
}

static bool bi_parse_border_width(const std::string& value, aether_css_length& out) {
    if (str_iequals(value, "thin")) { out = aether_css_length_zero(); out.value = 1.f; out.unit = AETHER_CSS_UNIT_PX; return true; }
    if (str_iequals(value, "medium")) { out = aether_css_length_zero(); out.value = 3.f; out.unit = AETHER_CSS_UNIT_PX; return true; }
    if (str_iequals(value, "thick")) { out = aether_css_length_zero(); out.value = 5.f; out.unit = AETHER_CSS_UNIT_PX; return true; }
    return aether_css_length_try_parse(value.c_str(), &out) != AETHER_FALSE;
}

static bool bi_try_border_style(const char* src, std::string& out) {
    std::string t = str_trim(src);
    static const char* styles[] = {
        "none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset"
    };
    for (auto& s : styles) { if (str_iequals(t, s)) { out = t; return true; } }
    return false;
}

static bool bi_try_border_radii(const char* src, std::string& out) {
    if (!src) return false;
    aether_border_radii radii;
    if (!aether_border_radii_try_parse(src, &radii)) return false;
    out = str_trim(src);
    return true;
}

static bool bi_try_gradient(const char* src, std::string& out) {
    std::string t = str_trim(src);
    if (str_starts_with_i(t, "linear-gradient")) { out = t; return true; }
    return false;
}

static property_apply_result_s bi_apply_gradient(aether_computed_style_t* st, const std::string& value);

static property_apply_result_s bi_apply_backdrop_filter(aether_computed_style_t* st, const std::string& value) {
    aether_backdrop_filter_list replacement{};
    aether_backdrop_filter_parse_error_e error{};
    if (!aether_backdrop_filter_list_try_parse(value, &replacement, &error)) {
        const char* message = error == aether_backdrop_filter_parse_error_e::unsupported_operation
            ? "The backdrop-filter function is not supported by the current compositor model."
            : error == aether_backdrop_filter_parse_error_e::unsupported_parameter
                ? "The backdrop-filter function has an unsupported parameter."
                : "The backdrop-filter value is syntactically invalid.";
        return property_apply_result_s::failure("AETHER-CSS-BACKDROP-FILTER", message);
    }
    aether_backdrop_filter_list_free(&st->backdrop_filters);
    st->backdrop_filters = replacement;
    return {};
}

static bool bi_try_background_image(const char* src, std::string& out) {
    const std::string value = str_trim(src ? src : "");
    if (str_iequals(value, "none")) { out = "none"; return true; }
    // Delegate to the real CSS parser so the compile-time validator accepts
    // exactly the same values the apply path accepts (single source of truth).
    aether_background_layer_list temp{};
    if (!aether_background_image_list_try_parse(value, &temp)) return false;
    aether_background_layer_list_free(&temp);
    out = value;
    return true;
}

static property_apply_result_s bi_apply_background_image(aether_computed_style_t* st, const std::string& value) {
    if (str_iequals(value, "none")) {
        aether_background_layer_list_free(&st->background_layers);
        return {};
    }
    if (str_starts_with_i(str_trim(value), "linear-gradient") && find_top_level(value, ',') < 0)
        return bi_apply_gradient(st, value);

    aether_background_layer_list replacement{};
    if (!aether_background_image_list_try_parse(value, &replacement)) {
        const auto parts = split_top_level(value, ',');
        for (const auto& part : parts) {
            if (str_starts_with_i(str_trim(part), "linear-gradient"))
                return property_apply_result_s::failure("AETHER-CSS-GRADIENT", "The linear-gradient value cannot be represented by the current gradient model.");
        }
        return property_apply_result_s::failure("AETHER-CSS-BACKGROUND-IMAGE-URL", "The url() background-image source is syntactically invalid.");
    }
    aether_background_layer_list_free(&st->background_layers);
    st->background_layers = replacement;
    return {};
}

static property_apply_result_s bi_apply_gradient(aether_computed_style_t* st, const std::string& value) {
    aether_linear_gradient gradient{};
    aether_linear_gradient_parse_info_s parse_info{};
    if (!aether_linear_gradient_try_parse(value.c_str(), &gradient, &parse_info)) {
        return property_apply_result_s::failure(
            "AETHER-CSS-GRADIENT",
            "The linear-gradient value cannot be represented by the current gradient model.");
    }
    aether_background_layer_list replacement{};
    replacement.items = static_cast<aether_background_layer*>(std::calloc(1, sizeof(aether_background_layer)));
    if (!replacement.items) {
        aether_linear_gradient_free(&gradient);
        return property_apply_result_s::failure("AETHER-CSS-GRADIENT", "Out of memory while creating the background layer.");
    }
    replacement.count = 1;
    auto& layer = replacement.items[0];
    layer.kind = AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT;
    layer.gradient = gradient;
    layer.position_x = aether_css_length_zero();
    layer.position_y = aether_css_length_zero();
    layer.size_kind = AETHER_BACKGROUND_SIZE_AUTO;
    layer.size_x = aether_css_length_auto();
    layer.size_y = aether_css_length_auto();
    layer.repeat_x = AETHER_BACKGROUND_REPEAT;
    layer.repeat_y = AETHER_BACKGROUND_REPEAT;
    aether_background_layer_list_free(&st->background_layers);
    st->background_layers = replacement;
    if (parse_info.uses_fixed_corner_direction) {
        return property_apply_result_s::warning(
            "AETHER-CSS-GRADIENT-APPROXIMATION",
            "Keyword corner directions use a fixed 45-degree vector until gradient direction can resolve against the final paint rectangle.");
    }
    return {};
}

static bool bi_split_shadow_top_level(const std::string& source, bool split_whitespace,
                                      std::vector<std::string>& output) {
    output.clear();
    std::string token;
    char quote = 0;
    int depth = 0;
    for (size_t index = 0; index < source.size(); ++index) {
        const char character = source[index];
        if (quote) {
            token += character;
            if (character == '\\' && index + 1 < source.size()) token += source[++index];
            else if (character == quote) quote = 0;
            continue;
        }
        if (character == '\'' || character == '"') { quote = character; token += character; continue; }
        if (character == '(') { ++depth; token += character; continue; }
        if (character == ')') {
            if (depth == 0) return false;
            --depth;
            token += character;
            continue;
        }
        const bool delimiter = depth == 0 &&
            (split_whitespace ? std::isspace(static_cast<unsigned char>(character)) : character == ',');
        if (!delimiter) { token += character; continue; }
        const std::string value = str_trim(token);
        if (value.empty()) return false;
        output.push_back(value);
        token.clear();
    }
    if (quote || depth != 0) return false;
    const std::string value = str_trim(token);
    if (value.empty()) return false;
    output.push_back(value);
    return true;
}

enum class shadow_parse_error_e {
    none,
    invalid,
    inset_not_allowed,
    spread_not_allowed,
};

static shadow_parse_error_e bi_parse_shadow_component(const std::string& source,
                                                      bool allow_inset, bool allow_spread,
                                                      aether_shadow& shadow,
                                                      bool& uses_current_color) {
    std::vector<std::string> tokens;
    if (!bi_split_shadow_top_level(source, true, tokens)) return shadow_parse_error_e::invalid;

    shadow = {};
    uses_current_color = true; // CSS default: the element's final currentColor.
    bool has_inset = false;
    bool has_color = false;
    int length_count = 0;
    for (const auto& token : tokens) {
        if (str_iequals(token, "inset")) {
            if (!allow_inset) return shadow_parse_error_e::inset_not_allowed;
            if (has_inset) return shadow_parse_error_e::invalid;
            has_inset = true;
            shadow.inset = AETHER_TRUE;
            continue;
        }
        if (str_iequals(token, "currentColor")) {
            if (has_color) return shadow_parse_error_e::invalid;
            has_color = true;
            uses_current_color = true;
            continue;
        }
        aether_color_rgba color;
        if (aether_color_rgba_try_parse(token.c_str(), &color)) {
            if (has_color) return shadow_parse_error_e::invalid;
            has_color = true;
            uses_current_color = false;
            shadow.color = color;
            continue;
        }
        aether_css_length length;
        if (!aether_css_length_try_parse(token.c_str(), &length) ||
            length.unit == AETHER_CSS_UNIT_AUTO || length.unit == AETHER_CSS_UNIT_PERCENT ||
            length_count == 4) return shadow_parse_error_e::invalid;
        if (!allow_spread && length_count == 3) return shadow_parse_error_e::spread_not_allowed;
        const float pixels = aether_css_length_resolve(length, 0, 16, 16, 1920, 1080);
        if (!std::isfinite(pixels) || (length_count == 2 && pixels < 0)) return shadow_parse_error_e::invalid;
        if (length_count == 0) shadow.offset_x = pixels;
        else if (length_count == 1) shadow.offset_y = pixels;
        else if (length_count == 2) shadow.blur_radius = pixels;
        else shadow.spread_radius = pixels;
        ++length_count;
    }
    return length_count >= 2 ? shadow_parse_error_e::none : shadow_parse_error_e::invalid;
}

static property_apply_result_s bi_apply_shadow_list(const std::string& value,
                                                    aether_shadow_list& destination,
                                                    std::vector<bool>& current_color_references,
                                                    bool allow_inset, bool allow_spread,
                                                    const char* code) {
    aether_shadow_list_free(&destination);
    current_color_references.clear();
    if (str_iequals(value, "none")) return {};

    std::vector<std::string> components;
    if (!bi_split_shadow_top_level(value, false, components) || components.empty()) {
        return property_apply_result_s::failure(code,
            "The shadow list value is syntactically invalid.");
    }

    std::vector<aether_shadow> parsed;
    std::vector<bool> references;
    parsed.reserve(components.size());
    references.reserve(components.size());
    for (const auto& component : components) {
        if (str_iequals(component, "none")) {
            return property_apply_result_s::failure(code,
                "The none keyword cannot be combined with another shadow.");
        }
        aether_shadow shadow{};
        bool uses_current_color = false;
        const auto error = bi_parse_shadow_component(component, allow_inset, allow_spread,
                                                     shadow, uses_current_color);
        if (error != shadow_parse_error_e::none) {
            if (error == shadow_parse_error_e::inset_not_allowed) {
                return property_apply_result_s::failure(code,
                    "The inset keyword is not valid for text-shadow.");
            }
            if (error == shadow_parse_error_e::spread_not_allowed) {
                return property_apply_result_s::failure(code,
                    "A spread radius is not valid for text-shadow.");
            }
            return property_apply_result_s::failure(code,
                allow_spread
                    ? "Each box shadow requires two to four lengths and an optional color or inset keyword."
                    : "Each text shadow requires two or three lengths and an optional color.");
        }
        parsed.push_back(shadow);
        references.push_back(uses_current_color);
    }

    const size_t bytes = sizeof(aether_shadow) * parsed.size();
    auto* items = static_cast<aether_shadow*>(std::malloc(bytes));
    if (!items) return property_apply_result_s::failure(code,
        "The shadow list could not be allocated.");
    std::memcpy(items, parsed.data(), bytes);
    destination = {items, static_cast<int32_t>(parsed.size())};
    current_color_references = std::move(references);
    return {};
}

static property_apply_result_s bi_apply_box_shadow(property_apply_context_s& context,
                                                   const std::string& value) {
    return bi_apply_shadow_list(value, context.style->box_shadows,
        context.color_resolution->box_shadow_uses_current_color, true, true,
        "AETHER-CSS-BOX-SHADOW");
}

static property_apply_result_s bi_apply_text_shadow(property_apply_context_s& context,
                                                    const std::string& value) {
    return bi_apply_shadow_list(value, context.style->text_shadows,
        context.color_resolution->text_shadow_uses_current_color, false, false,
        "AETHER-CSS-TEXT-SHADOW");
}

static bool bi_try_shadow(const char* src, std::string& out) {
    std::string t = str_trim(src);
    if (str_iequals(t, "none")) { out = "none"; return true; }
    out = t; return true;
}

static bool bi_try_transform(const char* src, std::string& out) {
    std::string t = str_trim(src);
    if (t.empty()) return false;
    // The checked applicator owns complete grammar validation so malformed
    // deferred var() values receive an explicit AETHER-CSS-TRANSFORM report.
    out = t;
    return true;
}

static bool bi_try_transform_origin(const char* src, std::string& out) {
    std::string t = str_trim(src);
    if (t.empty()) return false;
    out = t;
    return true;
}

static property_apply_result_s bi_apply_transform(aether_computed_style_t* style, const std::string& value) {
    aether_transform_list parsed{};
    if (!aether_transform_list_try_parse(value.c_str(), &parsed)) {
        return property_apply_result_s::failure("AETHER-CSS-TRANSFORM",
            "The transform value is not a supported 2D transform list.");
    }
    aether_transform_list_free(&style->transforms);
    style->transforms = parsed;
    return {};
}

static property_apply_result_s bi_apply_transform_origin(aether_computed_style_t* style, const std::string& value) {
    aether_transform_origin parsed{};
    if (!aether_transform_origin_try_parse(value.c_str(), &parsed)) {
        return property_apply_result_s::failure("AETHER-CSS-TRANSFORM-ORIGIN",
            "transform-origin accepts one or two 2D position components.");
    }
    style->transform_origin = parsed;
    return {};
}

// ═══ Install apply helpers ═════════════════════════════════════

static void apply_length_field(aether_css_length* field, const std::string& value) {
    if (value == "auto" || value.empty()) {
        *field = aether_css_length_auto();
    } else {
        aether_css_length l;
        if (aether_css_length_try_parse(value.c_str(), &l)) *field = l;
    }
}

// ═══ Register all builtin properties ══════════════════════════

void builtin_properties_register(style_property_registry_s* reg) {
    if (!reg) return;

    // ── Layout ─────────────────────────────────────────────────

    // display
    {
        static const char* kw[] = { "none", "block", "inline", "inline-block", "flex", "line-break",
                                    "table", "table-row", "table-cell" };
        static const int32_t vals[] = {
            AETHER_DISPLAY_NONE, AETHER_DISPLAY_BLOCK, AETHER_DISPLAY_INLINE,
            AETHER_DISPLAY_INLINE_BLOCK, AETHER_DISPLAY_FLEX, AETHER_DISPLAY_LINE_BREAK,
            AETHER_DISPLAY_TABLE, AETHER_DISPLAY_TABLE_ROW, AETHER_DISPLAY_TABLE_CELL
        };
        reg->register_property("display", false,
            STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
            "inline",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 9, v)) return false;
                for (int i = 0; i < 9; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) {
                st->display = std::atoi(v.c_str());
            });
    }

    // position
    {
        static const char* kw[] = { "static", "relative", "absolute", "fixed" };
        static const int32_t vals[] = {
            AETHER_POSITION_STATIC, AETHER_POSITION_RELATIVE,
            AETHER_POSITION_ABSOLUTE, AETHER_POSITION_FIXED
        };
        reg->register_property("position", false,
            STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
            "static",
            [](const char* s, std::string& o) -> bool { return bi_try_keyword(s, kw, 4, o); },
            [](aether_computed_style_t* st, const std::string& v) {
                for (int i = 0; i < 4; ++i) {
                    if (str_iequals(v, kw[i])) {
                        st->position = vals[i];
                        return;
                    }
                }
            });
    }

    // visibility
    {
        static const char* kw[] = { "visible", "hidden", "collapse" };
        reg->register_property("visibility", true,
            STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
            "visible",
            [](const char* s, std::string& o) -> bool { return bi_try_keyword(s, kw, 3, o); },
            [](aether_computed_style_t* st, const std::string& v) {
                st->visible = str_iequals(v, "visible") ? AETHER_TRUE : AETHER_FALSE;
            });
    }

    // box-sizing
    {
        static const char* kw[] = { "content-box", "border-box" };
        static const int32_t vals[] = { AETHER_BOX_SIZING_CONTENT_BOX, AETHER_BOX_SIZING_BORDER_BOX };
        reg->register_property("box-sizing", false,
            STYLE_IMPACT_LAYOUT,
            "content-box",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 2, v)) return false;
                o = std::to_string(vals[str_iequals(v, "border-box") ? 1 : 0]);
                return true;
            },
            [](aether_computed_style_t* st, const std::string& v) {
                st->box_sizing = std::atoi(v.c_str());
            });
    }

    // overflow-x
    {
        static const char* kw[] = { "visible", "hidden", "auto", "scroll" };
        static const int32_t vals[] = {
            AETHER_OVERFLOW_VISIBLE, AETHER_OVERFLOW_HIDDEN, AETHER_OVERFLOW_AUTO, AETHER_OVERFLOW_SCROLL
        };
        reg->register_property("overflow-x", false,
            STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
            "visible",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 4, v)) return false;
                for (int i = 0; i < 4; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) {
                st->overflow_x = std::atoi(v.c_str());
            });
    }

    // overflow-y
    {
        static const char* kw[] = { "visible", "hidden", "auto", "scroll" };
        static const int32_t vals[] = {
            AETHER_OVERFLOW_VISIBLE, AETHER_OVERFLOW_HIDDEN, AETHER_OVERFLOW_AUTO, AETHER_OVERFLOW_SCROLL
        };
        reg->register_property("overflow-y", false,
            STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
            "visible",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 4, v)) return false;
                for (int i = 0; i < 4; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) {
                st->overflow_y = std::atoi(v.c_str());
            });
    }

    // width
    reg->register_property("width", false,
        STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
        "auto", bi_try_size,
        [](aether_computed_style_t* st, const std::string& v) { apply_length_field(&st->width, v); });

    // height
    reg->register_property("height", false,
        STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
        "auto", bi_try_size,
        [](aether_computed_style_t* st, const std::string& v) { apply_length_field(&st->height, v); });

    // min-width
    reg->register_property("min-width", false,
        STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
        "0", bi_try_non_negative_length,
        [](aether_computed_style_t* st, const std::string& v) { apply_length_field(&st->min_width, v); });

    // min-height
    reg->register_property("min-height", false,
        STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
        "0", bi_try_non_negative_length,
        [](aether_computed_style_t* st, const std::string& v) { apply_length_field(&st->min_height, v); });

    // max-width
    reg->register_property("max-width", false,
        STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
        "none", bi_try_max_size,
        [](aether_computed_style_t* st, const std::string& v) { apply_length_field(&st->max_width, v); });

    // max-height
    reg->register_property("max-height", false,
        STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
        "none", bi_try_max_size,
        [](aether_computed_style_t* st, const std::string& v) { apply_length_field(&st->max_height, v); });

    // margin-top/right/bottom/left
    {
        auto mp = [](const char* s, std::string& o) -> bool { return bi_try_length_or_auto(s, o); };
        auto ma = [](int side) -> property_apply_fn {
            return [side](aether_computed_style_t* st, const std::string& v) {
                aether_css_length l;
                if (v == "auto" || v.empty()) l = aether_css_length_auto();
                else aether_css_length_try_parse(v.c_str(), &l);
                switch (side) {
                    case 0: st->margin.top = l; break;
                    case 1: st->margin.right = l; break;
                    case 2: st->margin.bottom = l; break;
                    case 3: st->margin.left = l; break;
                }
            };
        };
        reg->register_property("margin-top",    false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", mp, ma(0));
        reg->register_property("margin-right",  false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", mp, ma(1));
        reg->register_property("margin-bottom", false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", mp, ma(2));
        reg->register_property("margin-left",   false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", mp, ma(3));
    }

    // padding-top/right/bottom/left
    {
        auto pp = [](const char* s, std::string& o) -> bool { return bi_try_non_negative_length(s, o); };
        auto pa = [](int side) -> property_apply_fn {
            return [side](aether_computed_style_t* st, const std::string& v) {
                aether_css_length l;
                aether_css_length_try_parse(v.c_str(), &l);
                switch (side) {
                    case 0: st->padding.top = l; break;
                    case 1: st->padding.right = l; break;
                    case 2: st->padding.bottom = l; break;
                    case 3: st->padding.left = l; break;
                }
            };
        };
        reg->register_property("padding-top",    false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", pp, pa(0));
        reg->register_property("padding-right",  false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", pp, pa(1));
        reg->register_property("padding-bottom", false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", pp, pa(2));
        reg->register_property("padding-left",   false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", pp, pa(3));
    }

    // inset: top/right/bottom/left
    {
        auto ip = [](const char* s, std::string& o) -> bool { return bi_try_length_or_auto(s, o); };
        auto ia = [](int side) -> property_apply_fn {
            return [side](aether_computed_style_t* st, const std::string& v) {
                aether_css_length length;
                aether_css_length_try_parse(v.c_str(), &length);
                switch (side) {
                    case 0: st->inset.top = length; break;
                    case 1: st->inset.right = length; break;
                    case 2: st->inset.bottom = length; break;
                    case 3: st->inset.left = length; break;
                }
            };
        };
        reg->register_property("top",    false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "auto", ip, ia(0));
        reg->register_property("right",  false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "auto", ip, ia(1));
        reg->register_property("bottom", false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "auto", ip, ia(2));
        reg->register_property("left",   false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "auto", ip, ia(3));
    }

    // z-index
    reg->register_property("z-index", false, STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
        "", bi_try_nullable_integer,
        [](aether_computed_style_t* st, const std::string& v) {
            // bi_try_nullable_integer emits the decimal form of a signed
            // integer or the empty string for the auto/nullable case. CSS
            // "auto" fails that parse and is dropped at compile time, leaving
            // the default below, which matches the CSS initial value.
            st->z_index = 0;
            st->z_index_kind = AETHER_Z_INDEX_AUTO;
            if (v.empty()) return;
            char* end = nullptr;
            const long parsed = std::strtol(v.c_str(), &end, 10);
            if (end == v.c_str() || *end != '\0') return;
            st->z_index = static_cast<int32_t>(parsed);
            st->z_index_kind = AETHER_Z_INDEX_INTEGER;
        });

    // ── Flex ───────────────────────────────────────────────────

    // flex-direction
    {
        static const char* kw[] = { "row", "row-reverse", "column", "column-reverse" };
        static const int32_t vals[] = { 0, 0, 1, 1 };
        reg->register_property("flex-direction", false, STYLE_IMPACT_LAYOUT, "row",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 4, v)) return false;
                for (int i = 0; i < 4; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) { st->flex_direction = std::atoi(v.c_str()); });
    }

    // flex-wrap
    {
        static const char* kw[] = { "nowrap", "wrap", "wrap-reverse" };
        reg->register_property("flex-wrap", false, STYLE_IMPACT_LAYOUT, "nowrap",
            [](const char* s, std::string& o) -> bool { return bi_try_keyword(s, kw, 3, o); },
            [](aether_computed_style_t* st, const std::string& v) {
                st->flex_wrap = (str_iequals(v, "wrap") || str_iequals(v, "wrap-reverse")) ? AETHER_TRUE : AETHER_FALSE;
            });
    }

    // flex-grow
    reg->register_property("flex-grow", false, STYLE_IMPACT_LAYOUT, "0",
        bi_try_non_negative_number,
        [](aether_computed_style_t* st, const std::string& v) { st->flex_grow = std::strtof(v.c_str(), nullptr); });

    // flex-shrink
    reg->register_property("flex-shrink", false, STYLE_IMPACT_LAYOUT, "1",
        bi_try_non_negative_number,
        [](aether_computed_style_t* st, const std::string& v) { st->flex_shrink = std::strtof(v.c_str(), nullptr); });

    // flex-basis
    reg->register_property("flex-basis", false, STYLE_IMPACT_LAYOUT, "auto",
        bi_try_flex_basis,
        [](aether_computed_style_t* st, const std::string& v) {
            if (str_iequals(v, "content")) {
                st->flex_basis = { AETHER_FLEX_BASIS_CONTENT, aether_css_length_auto() };
                return;
            }
            aether_css_length length{};
            if (aether_css_length_try_parse(v.c_str(), &length) != AETHER_TRUE) return;
            st->flex_basis = {
                length.unit == AETHER_CSS_UNIT_AUTO ? AETHER_FLEX_BASIS_AUTO : AETHER_FLEX_BASIS_LENGTH,
                length
            };
        });

    // justify-content
    {
        static const char* kw[] = {
            "start","flex-start","end","flex-end","center","space-between","space-around","space-evenly"
        };
        static const int32_t vals[] = {
            AETHER_JUSTIFY_START,AETHER_JUSTIFY_START,AETHER_JUSTIFY_END,AETHER_JUSTIFY_END,
            AETHER_JUSTIFY_CENTER,AETHER_JUSTIFY_SPACE_BETWEEN,AETHER_JUSTIFY_CENTER,AETHER_JUSTIFY_CENTER
        };
        reg->register_property("justify-content", false, STYLE_IMPACT_LAYOUT, "start",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 8, v)) return false;
                for (int i = 0; i < 8; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) { st->justify_content = std::atoi(v.c_str()); });
    }

    // align-items
    {
        static const char* kw[] = { "stretch","start","flex-start","end","flex-end","center","baseline" };
        static const int32_t vals[] = {
            AETHER_ALIGN_STRETCH,AETHER_ALIGN_START,AETHER_ALIGN_START,
            AETHER_ALIGN_END,AETHER_ALIGN_END,AETHER_ALIGN_CENTER,AETHER_ALIGN_START
        };
        reg->register_property("align-items", false, STYLE_IMPACT_LAYOUT, "stretch",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 7, v)) return false;
                for (int i = 0; i < 7; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) { st->align_items = std::atoi(v.c_str()); });
    }

    // align-self
    {
        static const char* kw[] = { "auto","stretch","start","flex-start","end","flex-end","center","baseline" };
        static const int32_t vals[] = {
            AETHER_ALIGN_SELF_AUTO, AETHER_ALIGN_SELF_STRETCH, AETHER_ALIGN_SELF_START,
            AETHER_ALIGN_SELF_START, AETHER_ALIGN_SELF_END, AETHER_ALIGN_SELF_END,
            AETHER_ALIGN_SELF_CENTER, AETHER_ALIGN_SELF_START
        };
        reg->register_property("align-self", false, STYLE_IMPACT_LAYOUT, "auto",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 8, v)) return false;
                for (int i = 0; i < 8; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) { st->align_self = std::atoi(v.c_str()); });
    }

    // align-content
    {
        static const char* kw[] = {
            "stretch","start","flex-start","end","flex-end","center","space-between","space-around","space-evenly"
        };
        reg->register_property("align-content", false, STYLE_IMPACT_LAYOUT, "stretch",
            [](const char* s, std::string& o) -> bool { return bi_try_keyword(s, kw, 9, o); },
            [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });
    }

    // row-gap
    reg->register_property("row-gap", false, STYLE_IMPACT_LAYOUT, "0",
        bi_try_non_negative_length,
        [](aether_computed_style_t* st, const std::string& v) {
            aether_css_length l; aether_css_length_try_parse(v.c_str(), &l);
            st->gap = aether_css_length_resolve(l, 0.0f, 16.0f, 16.0f, 1920.0f, 1080.0f);
        });

    // column-gap
    reg->register_property("column-gap", false, STYLE_IMPACT_LAYOUT, "0",
        bi_try_non_negative_length,
        [](aether_computed_style_t* st, const std::string& v) {
            aether_css_length l; aether_css_length_try_parse(v.c_str(), &l);
            st->gap = aether_css_length_resolve(l, 0.0f, 16.0f, 16.0f, 1920.0f, 1080.0f);
        });

    // order
    reg->register_property("order", false, STYLE_IMPACT_LAYOUT, "0",
        bi_try_integer,
        [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });

    // ── Text ───────────────────────────────────────────────────

    // color
    reg->register_property("color", true, STYLE_IMPACT_PAINT, "#000000", bi_try_color,
        [](aether_computed_style_t* st, const std::string& v) {
            aether_color_rgba c; if (aether_color_rgba_try_parse(v.c_str(), &c)) st->color = c;
        });

    // -webkit-text-fill-color
    reg->register_property("-webkit-text-fill-color", true, STYLE_IMPACT_PAINT, "",
        bi_try_color,
        [](aether_computed_style_t* st, const std::string& v) {
            if (v.empty()) return;
            aether_color_rgba c; if (aether_color_rgba_try_parse(v.c_str(), &c)) st->color = c;
        });

    // text-fill-color (alias)
    reg->register_property("text-fill-color", true, STYLE_IMPACT_PAINT, "",
        bi_try_color,
        [](aether_computed_style_t* st, const std::string& v) {
            if (v.empty()) return;
            aether_color_rgba c; if (aether_color_rgba_try_parse(v.c_str(), &c)) st->color = c;
        });

    // font-family
    reg->register_property("font-family", true,
        STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_PAINT,
        "Segoe UI", bi_try_font_family,
        [](aether_computed_style_t* st, const std::string& v) {
            std::memset(st->font_family, 0, sizeof(st->font_family));
            std::strncpy(st->font_family, v.c_str(), sizeof(st->font_family) - 1);
        });

    // font-size
    reg->register_property("font-size", true,
        STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT,
        "16px", bi_try_font_size,
        [](aether_computed_style_t* st, const std::string& v) {
            aether_css_length l;
            if (aether_css_length_try_parse(v.c_str(), &l))
                st->font_size = aether_css_length_resolve(l, 0.0f, 16.0f, 16.0f, 1920.0f, 1080.0f);
        });

    // font-weight
    reg->register_property("font-weight", true,
        STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_PAINT,
        "400", bi_try_font_weight,
        [](aether_computed_style_t* st, const std::string& v) { st->font_weight = std::atoi(v.c_str()); });

    // font-style
    {
        static const char* kw[] = { "normal", "italic", "oblique" };
        reg->register_property("font-style", true,
            STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_PAINT,
            "normal",
            [](const char* s, std::string& o) -> bool { return bi_try_keyword(s, kw, 3, o); },
            [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });
    }

    // line-height
    reg->register_property("line-height", true,
        STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_LAYOUT,
        "1.2em", bi_try_line_height,
        [](aether_computed_style_t* st, const std::string& v) {
            aether_css_length l;
            if (aether_css_length_try_parse(v.c_str(), &l))
                st->line_height = aether_css_length_resolve(l, 0.0f, st->font_size, 16.0f, 1920.0f, 1080.0f);
        });

    // text-align
    {
        static const char* kw[] = { "start","end","left","right","center","justify" };
        static const int32_t vals[] = {
            AETHER_TEXT_ALIGN_START,AETHER_TEXT_ALIGN_END,AETHER_TEXT_ALIGN_LEFT,
            AETHER_TEXT_ALIGN_RIGHT,AETHER_TEXT_ALIGN_CENTER,AETHER_TEXT_ALIGN_JUSTIFY
        };
        reg->register_property("text-align", true,
            STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_PAINT,
            "start",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 6, v)) return false;
                for (int i = 0; i < 6; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) { st->text_align = std::atoi(v.c_str()); });
    }

    // text-transform
    {
        static const char* kw[] = { "none", "capitalize", "uppercase", "lowercase" };
        static const int32_t vals[] = {
            AETHER_TEXT_TRANSFORM_NONE, AETHER_TEXT_TRANSFORM_CAPITALIZE,
            AETHER_TEXT_TRANSFORM_UPPERCASE, AETHER_TEXT_TRANSFORM_LOWERCASE
        };
        reg->register_property("text-transform", true,
            STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_PAINT,
            "none",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 4, v)) return false;
                for (int i = 0; i < 4; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) {
                st->text_transform = std::atoi(v.c_str());
            });
    }

    // white-space
    {
        static const char* kw[] = { "normal", "nowrap", "pre", "pre-wrap", "pre-line" };
        static const int32_t vals[] = {
            AETHER_WHITE_SPACE_NORMAL, AETHER_WHITE_SPACE_NOWRAP,
            AETHER_WHITE_SPACE_PRE, AETHER_WHITE_SPACE_PRE_WRAP,
            AETHER_WHITE_SPACE_PRE_LINE};
        reg->register_property("white-space", true,
            STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_LAYOUT,
            "normal",
            [=](const char* s, std::string& o) -> bool {
                std::string keyword;
                if (!bi_try_keyword(s, kw, 5, keyword)) return false;
                for (int index = 0; index < 5; ++index) {
                    if (str_iequals(keyword, kw[index])) {
                        o = std::to_string(vals[index]);
                        return true;
                    }
                }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) { st->white_space = std::atoi(v.c_str()); });
    }

    // letter-spacing
    reg->register_property("letter-spacing", true,
        STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_LAYOUT,
        "0",
        [](const char* src, std::string& o) -> bool {
            std::string t = str_trim(src);
            if (str_iequals(t, "normal")) { o = "0"; return true; }
            return bi_try_length(src, o);
        },
        [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });

    // ── Paint ──────────────────────────────────────────────────

    // background-color
    reg->register_property("background-color", false, STYLE_IMPACT_PAINT,
        "transparent", bi_try_color,
        [](aether_computed_style_t* st, const std::string& v) {
            aether_color_rgba c; if (aether_color_rgba_try_parse(v.c_str(), &c)) st->background_color = c;
        });

    // Backdrop-filter is a non-inherited ordered operation list. The compositor v1
    // currently understands blur(); unsupported functions are diagnosed explicitly.
    reg->register_property_checked("backdrop-filter", false, STYLE_IMPACT_PAINT | STYLE_IMPACT_COMPOSITE,
        "none",
        [](const char* source, std::string& out) { out = str_trim(source ? source : ""); return !out.empty(); },
        [](aether_computed_style_t* st, const std::string& value) { return bi_apply_backdrop_filter(st, value); });
    reg->register_property_checked("-webkit-backdrop-filter", false, STYLE_IMPACT_PAINT | STYLE_IMPACT_COMPOSITE,
        "none",
        [](const char* source, std::string& out) { out = str_trim(source ? source : ""); return !out.empty(); },
        [](aether_computed_style_t* st, const std::string& value) { return bi_apply_backdrop_filter(st, value); });

    // background-image
    reg->register_property_checked("background-image", false, STYLE_IMPACT_PAINT,
        "none", bi_try_background_image,
        [](aether_computed_style_t* st, const std::string& v) -> property_apply_result_s {
            return bi_apply_background_image(st, v);
        });

    // These list-valued longhands deliberately apply after background-image.
    // CSS repeats the final supplied value when there are fewer values than layers.
    reg->register_property_checked("background-position", false, STYLE_IMPACT_PAINT,
        "0% 0%",
        [](const char* source, std::string& out) { out = str_trim(source ? source : ""); return !out.empty(); },
        [](aether_computed_style_t* st, const std::string& value) -> property_apply_result_s {
            std::vector<aether_background_position_s> positions;
            if (!aether_background_position_list_try_parse(value, positions))
                return property_apply_result_s::failure("AETHER-CSS-BACKGROUND-POSITION", "The background-position value is not supported by the current background layer model.");
            for (int32_t i = 0; i < st->background_layers.count; ++i) {
                const auto& position = positions[static_cast<size_t>(i) % positions.size()];
                st->background_layers.items[i].position_x = position.x;
                st->background_layers.items[i].position_y = position.y;
            }
            return {};
        });
    reg->register_property_checked("background-size", false, STYLE_IMPACT_PAINT,
        "auto auto",
        [](const char* source, std::string& out) { out = str_trim(source ? source : ""); return !out.empty(); },
        [](aether_computed_style_t* st, const std::string& value) -> property_apply_result_s {
            std::vector<aether_background_size_s> sizes;
            if (!aether_background_size_list_try_parse(value, sizes))
                return property_apply_result_s::failure("AETHER-CSS-BACKGROUND-SIZE", "The background-size value is not supported by the current background layer model.");
            for (int32_t i = 0; i < st->background_layers.count; ++i) {
                const auto& size = sizes[static_cast<size_t>(i) % sizes.size()];
                st->background_layers.items[i].size_kind = size.kind;
                st->background_layers.items[i].size_x = size.x;
                st->background_layers.items[i].size_y = size.y;
            }
            return {};
        });
    reg->register_property_checked("background-repeat", false, STYLE_IMPACT_PAINT,
        "repeat repeat",
        [](const char* source, std::string& out) { out = str_trim(source ? source : ""); return !out.empty(); },
        [](aether_computed_style_t* st, const std::string& value) -> property_apply_result_s {
            std::vector<aether_background_repeat_s> repeats;
            if (!aether_background_repeat_list_try_parse(value, repeats))
                return property_apply_result_s::failure("AETHER-CSS-BACKGROUND-REPEAT", "The background-repeat value is not supported by the current background layer model.");
            for (int32_t i = 0; i < st->background_layers.count; ++i) {
                const auto& repeat = repeats[static_cast<size_t>(i) % repeats.size()];
                st->background_layers.items[i].repeat_x = repeat.x;
                st->background_layers.items[i].repeat_y = repeat.y;
            }
            return {};
        });

    // opacity
    reg->register_property("opacity", false, STYLE_IMPACT_COMPOSITE | STYLE_IMPACT_HITTEST,
        "1", bi_try_opacity,
        [](aether_computed_style_t* st, const std::string& v) { st->opacity = std::strtof(v.c_str(), nullptr); });

    // -webkit-background-clip
    {
        auto bgclip_parser = [](const char* s, std::string& o) -> bool {
            std::string t = str_trim(s);
            if (str_iequals(t, "text")) { o = "text"; return true; }
            static const char* kw[] = { "border-box", "padding-box", "content-box", "text" };
            return bi_try_keyword(s, kw, 4, o);
        };
        auto bgclip_apply = [](aether_computed_style_t* st, const std::string& v) {
            if (str_iequals(v, "text") && st->background_layers.count == 1 &&
                st->background_layers.items &&
                st->background_layers.items[0].kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT) {
                aether_linear_gradient_free(&st->text_gradient);
                st->text_gradient = st->background_layers.items[0].gradient;
                st->background_layers.items[0].gradient = {};
                aether_background_layer_list_free(&st->background_layers);
                st->has_text_gradient = AETHER_TRUE;
                st->background_color = {0, 0, 0, 0};
            }
        };
        reg->register_property("-webkit-background-clip", false, STYLE_IMPACT_PAINT, "border-box",
            bgclip_parser, bgclip_apply);
        reg->register_property("background-clip", false, STYLE_IMPACT_PAINT, "border-box",
            bgclip_parser, bgclip_apply);
    }

    // border-width (top/right/bottom/left)
    {
        auto bwa = [](aether_border_side_t side) -> property_apply_fn {
            return [side](aether_computed_style_t* st, const std::string& v) {
                aether_css_length l = aether_css_length_zero();
                if (!bi_parse_border_width(v, l)) return;
                aether_border_width_for_side(st->border_width, side) = l;
            };
        };
        reg->register_property("border-top-width",    false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", bi_try_border_width, bwa(aether_border_side_t::top));
        reg->register_property("border-right-width",  false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", bi_try_border_width, bwa(aether_border_side_t::right));
        reg->register_property("border-bottom-width", false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", bi_try_border_width, bwa(aether_border_side_t::bottom));
        reg->register_property("border-left-width",   false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", bi_try_border_width, bwa(aether_border_side_t::left));
    }

    // border-style (top/right/bottom/left)
    {
        auto border_style = [](const std::string& value) -> int32_t {
            if (str_iequals(value, "hidden")) return AETHER_BORDER_STYLE_HIDDEN;
            if (str_iequals(value, "dotted")) return AETHER_BORDER_STYLE_DOTTED;
            if (str_iequals(value, "dashed")) return AETHER_BORDER_STYLE_DASHED;
            if (str_iequals(value, "solid")) return AETHER_BORDER_STYLE_SOLID;
            if (str_iequals(value, "double")) return AETHER_BORDER_STYLE_DOUBLE;
            if (str_iequals(value, "groove")) return AETHER_BORDER_STYLE_GROOVE;
            if (str_iequals(value, "ridge")) return AETHER_BORDER_STYLE_RIDGE;
            if (str_iequals(value, "inset")) return AETHER_BORDER_STYLE_INSET;
            if (str_iequals(value, "outset")) return AETHER_BORDER_STYLE_OUTSET;
            return AETHER_BORDER_STYLE_NONE;
        };
        auto bsa = [border_style](aether_border_side_t side) -> property_apply_fn {
            return [border_style, side](aether_computed_style_t* st, const std::string& v) {
                aether_border_style_for_side(st->border_styles, side) = border_style(v);
            };
        };
        reg->register_property("border-top-style",    false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT, "none", bi_try_border_style, bsa(aether_border_side_t::top));
        reg->register_property("border-right-style",  false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT, "none", bi_try_border_style, bsa(aether_border_side_t::right));
        reg->register_property("border-bottom-style", false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT, "none", bi_try_border_style, bsa(aether_border_side_t::bottom));
        reg->register_property("border-left-style",   false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT, "none", bi_try_border_style, bsa(aether_border_side_t::left));
    }

    // border-color (top/right/bottom/left)
    {
        auto bca = [](aether_border_side_t side) -> property_apply_context_fn {
            return [side](property_apply_context_s& context, const std::string& v) {
                if (str_iequals(v, "currentColor")) {
                    context.color_resolution->border_uses_current_color[static_cast<int>(side)] = true;
                    return;
                }
                aether_color_rgba color{};
                if (!aether_color_rgba_try_parse(v.c_str(), &color)) return;
                aether_border_color_for_side(context.style->border_colors, side) = color;
                context.color_resolution->border_uses_current_color[static_cast<int>(side)] = false;
            };
        };
        reg->register_property_with_context("border-top-color",    false, STYLE_IMPACT_PAINT, "currentColor", bi_try_color, bca(aether_border_side_t::top));
        reg->register_property_with_context("border-right-color",  false, STYLE_IMPACT_PAINT, "currentColor", bi_try_color, bca(aether_border_side_t::right));
        reg->register_property_with_context("border-bottom-color", false, STYLE_IMPACT_PAINT, "currentColor", bi_try_color, bca(aether_border_side_t::bottom));
        reg->register_property_with_context("border-left-color",   false, STYLE_IMPACT_PAINT, "currentColor", bi_try_color, bca(aether_border_side_t::left));
    }

    // CSS Backgrounds & Borders §4.1. The model retains all corner axes; resolution and
    // overlap normalization live in css/border_radii, not the property registry.
    reg->register_property("border-radius", false, STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
        "0", bi_try_border_radii,
        [](aether_computed_style_t* st, const std::string& v) { aether_border_radii_try_parse(v, &st->border_radii); });
    auto register_corner_radius = [&reg](const char* name, aether_corner_radius aether_border_radii::* member) {
        reg->register_property(name, false, STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST, "0", bi_try_border_radii,
            [member](aether_computed_style_t* st, const std::string& value) {
                aether_border_radii parsed;
                if (aether_border_radii_try_parse(value, &parsed)) st->border_radii.*member = parsed.top_left;
            });
    };
    register_corner_radius("border-top-left-radius", &aether_border_radii::top_left);
    register_corner_radius("border-top-right-radius", &aether_border_radii::top_right);
    register_corner_radius("border-bottom-right-radius", &aether_border_radii::bottom_right);
    register_corner_radius("border-bottom-left-radius", &aether_border_radii::bottom_left);

    // box-shadow
    reg->register_property_checked_with_context("box-shadow", false, STYLE_IMPACT_PAINT, "none",
        bi_try_shadow, bi_apply_box_shadow);
    reg->register_property_checked_with_context("text-shadow", true, STYLE_IMPACT_PAINT, "none",
        bi_try_shadow, bi_apply_text_shadow);

    // ── Interaction ────────────────────────────────────────────

    // cursor
    {
        static const char* kw[] = {
            "default","pointer","text","move","crosshair","wait","help","not-allowed",
            "grab","grabbing","ew-resize","col-resize","ns-resize","row-resize"
        };
        static const int32_t vals[] = {
            AETHER_CURSOR_DEFAULT,AETHER_CURSOR_POINTER,AETHER_CURSOR_TEXT,
            AETHER_CURSOR_DEFAULT,AETHER_CURSOR_DEFAULT,AETHER_CURSOR_DEFAULT,AETHER_CURSOR_DEFAULT,
            AETHER_CURSOR_DEFAULT,AETHER_CURSOR_DEFAULT,AETHER_CURSOR_DEFAULT,AETHER_CURSOR_DEFAULT,
            AETHER_CURSOR_DEFAULT,AETHER_CURSOR_DEFAULT,AETHER_CURSOR_DEFAULT
        };
        reg->register_property("cursor", true, STYLE_IMPACT_CURSOR, "default",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 14, v)) return false;
                for (int i = 0; i < 14; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) { st->cursor = std::atoi(v.c_str()); });
    }

    // pointer-events
    {
        static const char* kw[] = { "auto", "none" };
        reg->register_property("pointer-events", true, STYLE_IMPACT_HITTEST, "auto",
            [](const char* s, std::string& o) -> bool { return bi_try_keyword(s, kw, 2, o); },
            [](aether_computed_style_t* st, const std::string& v) {
                st->pointer_events = str_iequals(v, "none")
                    ? AETHER_POINTER_EVENTS_NONE
                    : AETHER_POINTER_EVENTS_AUTO;
            });
    }

    // ── Transforms ─────────────────────────────────────────────

    reg->register_property_checked("transform", false,
        STYLE_IMPACT_COMPOSITE | STYLE_IMPACT_HITTEST, "none", bi_try_transform,
        bi_apply_transform);

    reg->register_property_checked("transform-origin", false,
        STYLE_IMPACT_COMPOSITE | STYLE_IMPACT_HITTEST, "50% 50%", bi_try_transform_origin,
        bi_apply_transform_origin);

    // ── Transitions ─────────────────────────────────────────────
    // All four longhands are PAINT-impact only: transitions never relayout.
    // Engine initial values deliberately use the computed-style defaults
    // (none / 0s / 0s / linear) rather than the CSS-spec initials
    // (all / ease), so a bare declaration like `transition: 0.3s` still
    // requires an explicit property and timing before anything animates.
    reg->register_property("transition-property", false, STYLE_IMPACT_PAINT, "none",
        [](const char* s, std::string& o) -> bool {
            if (bi_try_transition_property_keyword(s, o)) return true;
            // v1: any other property name is a valid CSS transition target
            // that this engine cannot animate; normalize it to no-op (0).
            o = "0";
            return true;
        },
        [](aether_computed_style_t* st, const std::string& v) {
            st->transition_properties = std::atoi(v.c_str());
        });

    reg->register_property("transition-duration", false, STYLE_IMPACT_PAINT, "0s",
        bi_try_transition_time_text,
        [](aether_computed_style_t* st, const std::string& v) {
            st->transition_duration = std::strtof(v.c_str(), nullptr);
        });

    reg->register_property("transition-delay", false, STYLE_IMPACT_PAINT, "0s",
        bi_try_transition_time_text,
        [](aether_computed_style_t* st, const std::string& v) {
            st->transition_delay = std::strtof(v.c_str(), nullptr);
        });

    reg->register_property("transition-timing-function", false, STYLE_IMPACT_PAINT, "linear",
        bi_try_transition_timing,
        [](aether_computed_style_t* st, const std::string& v) {
            st->transition_timing = std::atoi(v.c_str());
        });

    // ── Shorthands ─────────────────────────────────────────────

    // Helper: box shorthand (1→4 expansion)
    auto box_shorthand = [](const char* source, const char* const targets[4], style_shorthand_writer_s& writer) -> bool {
        std::string t = str_trim(source);
        if (bi_is_global_keyword(t)) {
            for (int i = 0; i < 4; ++i) writer.set(targets[i], t.c_str());
            return true;
        }
        auto parts = bi_split_ws(t);
        if (parts.size() < 1 || parts.size() > 4) return false;
        std::string ex[4];
        switch (parts.size()) {
            case 1: ex[0]=ex[1]=ex[2]=ex[3]=parts[0]; break;
            case 2: ex[0]=ex[2]=parts[0]; ex[1]=ex[3]=parts[1]; break;
            case 3: ex[0]=parts[0]; ex[1]=ex[3]=parts[1]; ex[2]=parts[2]; break;
            case 4: ex[0]=parts[0]; ex[1]=parts[1]; ex[2]=parts[2]; ex[3]=parts[3]; break;
        }
        for (int i = 0; i < 4; ++i) writer.set(targets[i], ex[i].c_str());
        return true;
    };

    // margin shorthand
    {
        const char* t[4] = { "margin-top", "margin-right", "margin-bottom", "margin-left" };
        reg->register_shorthand("margin",
            [&box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            });
    }

    // padding shorthand
    {
        const char* t[4] = { "padding-top", "padding-right", "padding-bottom", "padding-left" };
        reg->register_shorthand("padding",
            [&box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            });
    }

    // inset shorthand
    {
        const char* t[4] = { "top", "right", "bottom", "left" };
        reg->register_shorthand("inset",
            [&box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            });
    }

    // border-width shorthand
    {
        const char* t[4] = { "border-top-width", "border-right-width", "border-bottom-width", "border-left-width" };
        reg->register_shorthand("border-width",
            [&box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            });
    }

    // border-style shorthand
    {
        const char* t[4] = { "border-top-style", "border-right-style", "border-bottom-style", "border-left-style" };
        reg->register_shorthand("border-style",
            [&box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            });
    }

    // border-color shorthand
    {
        const char* t[4] = { "border-top-color", "border-right-color", "border-bottom-color", "border-left-color" };
        reg->register_shorthand("border-color",
            [&box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            });
    }

    // overflow shorthand
    reg->register_shorthand("overflow",
        [](const char* source, style_shorthand_writer_s& writer) -> bool {
            std::string t = str_trim(source);
            if (bi_is_global_keyword(t)) {
                writer.set("overflow-x", t.c_str());
                writer.set("overflow-y", t.c_str());
                return true;
            }
            auto parts = bi_split_ws(t);
            if (parts.size() < 1 || parts.size() > 2) return false;
            static const char* ok[] = { "visible", "hidden", "clip", "auto", "scroll" };
            std::string v1;
            if (!bi_try_keyword(parts[0].c_str(), ok, 5, v1)) return false;
            if (parts.size() == 2) {
                std::string v2;
                if (!bi_try_keyword(parts[1].c_str(), ok, 5, v2)) return false;
                writer.set("overflow-x", v1.c_str());
                writer.set("overflow-y", v2.c_str());
            } else {
                writer.set("overflow-x", v1.c_str());
                writer.set("overflow-y", v1.c_str());
            }
            return true;
        });

    // gap shorthand
    reg->register_shorthand("gap",
        [](const char* source, style_shorthand_writer_s& writer) -> bool {
            std::string t = str_trim(source);
            if (bi_is_global_keyword(t)) {
                writer.set("row-gap", t.c_str());
                writer.set("column-gap", t.c_str());
                return true;
            }
            auto parts = bi_split_ws(t);
            if (parts.size() < 1 || parts.size() > 2) return false;
            std::string v1;
            if (!bi_try_non_negative_length(parts[0].c_str(), v1)) return false;
            if (parts.size() == 2) {
                std::string v2;
                if (!bi_try_non_negative_length(parts[1].c_str(), v2)) return false;
                writer.set("row-gap", v1.c_str());
                writer.set("column-gap", v2.c_str());
            } else {
                writer.set("row-gap", v1.c_str());
                writer.set("column-gap", v1.c_str());
            }
            return true;
        });

    // background shorthand
    reg->register_shorthand("background",
        [](const char* source, style_shorthand_writer_s& writer) -> bool {
            std::string t = str_trim(source);
            if (bi_is_global_keyword(t)) {
                writer.set("background-color", t.c_str());
                writer.set("background-image", t.c_str());
                writer.set("background-position", t.c_str());
                writer.set("background-size", t.c_str());
                writer.set("background-repeat", t.c_str());
                return true;
            }
            aether_background_shorthand_s parsed{};
            if (aether_background_shorthand_try_parse(t, parsed)) {
                writer.set("background-image", parsed.image.c_str());
                writer.set("background-position", parsed.position.c_str());
                writer.set("background-size", parsed.size.c_str());
                writer.set("background-repeat", parsed.repeat.c_str());
                writer.set("background-color", parsed.has_color ? parsed.color.c_str() : "transparent");
                return true;
            }
            {
                std::string g;
                if (bi_try_gradient(t.c_str(), g)) {
                    writer.set("background-image", g.c_str());

                    // A solid fallback color (the gradient's first stop) is written too, so a
                    // consumer that only paints background-color still gets a plausible base
                    // color — mirrors BuiltinStyleProperties.cs's "background" shorthand on the
                    // managed path exactly (see ExpandBackground there).
                    aether_computed_style_t temp{};
                    const auto gradient_result = bi_apply_gradient(&temp, g);
                    if (gradient_result.success && temp.background_layers.count == 1 &&
                        temp.background_layers.items &&
                        temp.background_layers.items[0].kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT &&
                        temp.background_layers.items[0].gradient.stop_count > 0) {
                        const auto& c = temp.background_layers.items[0].gradient.stops[0].color;
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "rgba(%d, %d, %d, %g)",
                            static_cast<int>(std::lround(c.r * 255.0f)),
                            static_cast<int>(std::lround(c.g * 255.0f)),
                            static_cast<int>(std::lround(c.b * 255.0f)),
                            static_cast<double>(c.a));
                        writer.set("background-color", buf);
                    } else {
                        writer.set("background-color", "transparent");
                    }
                    aether_background_layer_list_free(&temp.background_layers);

                    return true;
                }
            }
            std::string c;
            if (bi_try_color(source, c)) {
                writer.set("background-color", c.c_str());
                writer.set("background-image", "none");
                writer.set("background-position", "0% 0%");
                writer.set("background-size", "auto auto");
                writer.set("background-repeat", "repeat repeat");
                return true;
            }
            return false;
        });

    // border shorthand
    reg->register_shorthand("border",
        [](const char* source, style_shorthand_writer_s& writer) -> bool {
            std::string t = str_trim(source);
            static const char* sides[] = { "top", "right", "bottom", "left" };
            if (bi_is_global_keyword(t)) {
                for (auto& side : sides) {
                    std::string p = std::string("border-") + side + "-width";
                    writer.set(p.c_str(), t.c_str());
                    p = std::string("border-") + side + "-style";
                    writer.set(p.c_str(), t.c_str());
                    p = std::string("border-") + side + "-color";
                    writer.set(p.c_str(), t.c_str());
                }
                return true;
            }
            auto parts = bi_split_ws(t);
            if (parts.size() < 1 || parts.size() > 3) return false;
            std::string width = "medium", style = "none", color = "currentColor";
            bool wset = false, sset = false, cset = false;
            for (auto& part : parts) {
                if (!wset && bi_try_border_width(part.c_str(), width)) { wset = true; }
                else if (!sset && bi_try_border_style(part.c_str(), style)) { sset = true; }
                else if (!cset && bi_try_color(part.c_str(), color)) { cset = true; }
                else return false;
            }
            for (auto& side : sides) {
                std::string p = std::string("border-") + side + "-width";
                writer.set(p.c_str(), width.c_str());
                p = std::string("border-") + side + "-style";
                writer.set(p.c_str(), style.c_str());
                p = std::string("border-") + side + "-color";
                writer.set(p.c_str(), color.c_str());
            }
            return true;
        });

    // Per-side border shorthand. This is intentionally expanded through the same longhands as
    // `border`, so cascade precedence and invalid-value handling stay identical for every edge.
    for (const char* side : { "top", "right", "bottom", "left" }) {
        const std::string shorthand = std::string("border-") + side;
        reg->register_shorthand(shorthand.c_str(),
            [side](const char* source, style_shorthand_writer_s& writer) -> bool {
                std::string t = str_trim(source);
                const std::string prefix = std::string("border-") + side + "-";
                if (bi_is_global_keyword(t)) {
                    writer.set((prefix + "width").c_str(), t.c_str());
                    writer.set((prefix + "style").c_str(), t.c_str());
                    writer.set((prefix + "color").c_str(), t.c_str());
                    return true;
                }
                const auto parts = bi_split_ws(t);
                if (parts.empty() || parts.size() > 3) return false;
                std::string width = "medium", style = "none", color = "currentColor";
                bool width_set = false, style_set = false, color_set = false;
                for (const auto& part : parts) {
                    if (!width_set && bi_try_border_width(part.c_str(), width)) width_set = true;
                    else if (!style_set && bi_try_border_style(part.c_str(), style)) style_set = true;
                    else if (!color_set && bi_try_color(part.c_str(), color)) color_set = true;
                    else return false;
                }
                writer.set((prefix + "width").c_str(), width.c_str());
                writer.set((prefix + "style").c_str(), style.c_str());
                writer.set((prefix + "color").c_str(), color.c_str());
                return true;
            });
    }

    // flex shorthand
    reg->register_shorthand("flex",
        [](const char* source, style_shorthand_writer_s& writer) -> bool {
            std::string t = str_trim(source);
            if (bi_is_global_keyword(t)) {
                writer.set("flex-grow", t.c_str());
                writer.set("flex-shrink", t.c_str());
                writer.set("flex-basis", t.c_str());
                return true;
            }
            if (str_iequals(t, "none")) {
                writer.set("flex-grow", "0"); writer.set("flex-shrink", "0"); writer.set("flex-basis", "auto");
                return true;
            }
            if (str_iequals(t, "auto")) {
                writer.set("flex-grow", "1"); writer.set("flex-shrink", "1"); writer.set("flex-basis", "auto");
                return true;
            }
            if (str_iequals(t, "initial")) {
                writer.set("flex-grow", "0"); writer.set("flex-shrink", "1"); writer.set("flex-basis", "auto");
                return true;
            }
            auto parts = bi_split_ws(t);
            if (parts.size() < 1 || parts.size() > 3) return false;
            std::string grow_test;
            if (!bi_try_non_negative_number(parts[0].c_str(), grow_test)) return false;
            std::string grow = parts[0], shrink = "1", basis = "0%";
            if (parts.size() >= 2) {
                std::string shrink_test;
                if (bi_try_non_negative_number(parts[1].c_str(), shrink_test)) {
                    shrink = parts[1];
                    if (parts.size() == 3) basis = parts[2];
                } else {
                    basis = parts[1];
                    if (parts.size() == 3) return false;
                }
            }
            std::string basis_test;
            if (!bi_try_flex_basis(basis.c_str(), basis_test)) return false;
            writer.set("flex-grow", grow.c_str());
            writer.set("flex-shrink", shrink.c_str());
            writer.set("flex-basis", basis.c_str());
            return true;
        });

    // transition shorthand: a single transition, NO comma-separated lists in
    // v1. Components appear in any order: [property] [duration] [delay]
    // [timing-function]. Unspecified components reset to the engine initial
    // values (CSS shorthand reset semantics), so `transition: all 0.3s ease`
    // yields properties=OPACITY|TRANSFORM, duration=0.3s, delay=0s,
    // timing=EASE. The writer values are re-parseable CSS tokens so the
    // longhand parsers validate them through the normal cascade path.
    reg->register_shorthand("transition",
        [](const char* source, style_shorthand_writer_s& writer) -> bool {
            const auto format_time = [](float seconds) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%gs", static_cast<double>(seconds));
                return std::string(buffer);
            };
            std::string t = str_trim(source);
            if (bi_is_global_keyword(t)) {
                writer.set("transition-property", t.c_str());
                writer.set("transition-duration", t.c_str());
                writer.set("transition-delay", t.c_str());
                writer.set("transition-timing-function", t.c_str());
                return true;
            }
            const auto parts = bi_split_ws(t);
            if (parts.empty()) return false;
            std::string property_value = "none";
            std::string duration_value = "0s";
            std::string delay_value = "0s";
            std::string timing_value = "linear";
            bool has_property = false, has_duration = false, has_delay = false, has_timing = false;
            for (const auto& part : parts) {
                std::string keyword;
                if (!has_property && bi_try_transition_property_keyword(part.c_str(), keyword)) {
                    // Write the KEYWORD token, not the numeric bitmask: the
                    // transition-property longhand re-parses this value through
                    // the normal cascade path, and it only accepts keywords.
                    property_value = part;
                    has_property = true;
                    continue;
                }
                float seconds = 0.0f;
                if (bi_try_time(part.c_str(), seconds)) {
                    if (!has_duration) {
                        duration_value = format_time(seconds);
                        has_duration = true;
                    } else if (!has_delay) {
                        delay_value = format_time(seconds);
                        has_delay = true;
                    } else {
                        return false;
                    }
                    continue;
                }
                if (!has_timing && bi_try_transition_timing(part.c_str(), keyword)) {
                    // Keep the keyword token so the longhand re-parses it.
                    timing_value = part;
                    has_timing = true;
                    continue;
                }
                return false;
            }
            writer.set("transition-property", property_value.c_str());
            writer.set("transition-duration", duration_value.c_str());
            writer.set("transition-delay", delay_value.c_str());
            writer.set("transition-timing-function", timing_value.c_str());
            return true;
        });
}
