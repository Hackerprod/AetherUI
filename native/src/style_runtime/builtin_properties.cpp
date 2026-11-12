#include "../css/computed_style_internal.h"
using namespace aether::style;
#include "style_property_registry_internal.h"
#include "style_runtime_types.h"
#include "builtin_property_parse_utils.h"
#include "css_single_identifier.h"
#include "transition_properties_internal.h"
#include "paused_animation_properties.h"
#include "shadow_values.h"
#include "css_component_values.h"
#include "gap_length_source.h"
#include "../primitives/ascii_text.h"
#include <limits>
#include <charconv>
#include "../css/css_numeric_conversion_internal.h"
#include <algorithm>
#include "../css/border_utils.h"
#include "../css/css_length_internal.h"
#include "../css/contextual_math.h"
#include "../css/border_radii.h"
#include "../css/background_layers.h"
#include "../css/backdrop_filters.h"
#include "../css/linear_gradient.h"
#include "../css/transforms.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

// ═══ Parse helpers ═════════════════════════════════════════════

static bool bi_try_keyword(const char* src, const char* const* keywords, int count, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    for (int i = 0; i < count; ++i) {
        if (str_iequals(t, keywords[i])) { out = keywords[i]; return true; }
    }
    return false;
}

// Standards CSS validation is intentionally separate from the public legacy parser.
static bool bi_css_length(const std::string& text, aether_css_length& length,
                          bool nonnegative = false, bool percentage = true) {
    aether::css::calc_value_s result;
    double original_number = 0.0;
    const bool calc = (text.size() >= 5 && str_iequals(text.substr(0, 5), "calc("));
    if (!(calc ? aether::css::try_parse_calc(text, result) :
                 aether::css::try_parse_numeric_literal(text, result, &original_number))) return false;
    if (nonnegative && !calc && original_number < 0.0) return false;
    if (result.kind == aether::css::calc_value_kind_e::number) {
        if (result.number != 0 || calc) return false;
        length = aether_css_length_zero(); return true;
    }
    length = result.length;
    if (!percentage && (measure_source_dependencies(text) & MEASURE_PERCENTAGE)) return false;
    return !nonnegative || calc || length.value >= 0;
}

static bool bi_try_length_or_auto(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    if (str_iequals(t, "auto")) { out = "auto"; return true; }
    aether_css_length l;
    if (bi_css_length(t, l, false)) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_non_negative_length(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    aether_css_length l;
    if (bi_css_length(t, l, true)) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_gap_length(const char* source, std::string& out) {
    if (!source) return false;
    std::string normalized;
    return gap_length_source(str_trim(source), normalized) &&
        bi_try_non_negative_length(normalized.c_str(), out);
}

static bool bi_try_size(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    if (str_iequals(t, "auto")) { out = "auto"; return true; }
    aether_css_length l;
    if (bi_css_length(t, l, true)) {
        out = t; return true;
    }
    return false;
}

// H4 admits deferred comparison math only in padding and flex-basis.
static bool bi_contextual_length(const std::string& value, aether::css::math_tree& tree) {
    css_component_cursor cursor(value);
    css_component_value component;
    if (!cursor.next(component) || component.token.kind != css_variable_token_kind::function) return false;
    return aether::css::parse_contextual_math(value, tree);
}
static bool bi_try_contextual_nonnegative(const char* source, std::string& output) {
    if (bi_try_non_negative_length(source, output)) return true;
    const std::string value = str_trim(source ? source : "");
    aether::css::math_tree tree;
    if (!bi_contextual_length(value, tree)) return false;
    output = value; return true;
}

static aether::css::intrinsic_size_keyword bi_intrinsic_keyword(const std::string& source) {
    std::string value;
    if (!css_single_identifier(source, value)) return aether::css::intrinsic_size_keyword::none;
    if (value == "min-content") return aether::css::intrinsic_size_keyword::min_content;
    if (value == "max-content") return aether::css::intrinsic_size_keyword::max_content;
    if (value == "fit-content") return aether::css::intrinsic_size_keyword::fit_content;
    return aether::css::intrinsic_size_keyword::none;
}
static bool bi_try_intrinsic_keyword(const char* source, std::string& output) {
    const auto keyword = bi_intrinsic_keyword(source ? source : "");
    if (keyword == aether::css::intrinsic_size_keyword::none) return false;
    output = keyword == aether::css::intrinsic_size_keyword::min_content ? "min-content" :
        keyword == aether::css::intrinsic_size_keyword::max_content ? "max-content" : "fit-content";
    return true;
}
static bool bi_try_flex_basis(const char* source, std::string& output) {
    if (bi_try_intrinsic_keyword(source, output)) return true;
    const std::string value = str_trim(source ? source : "");
    if (str_iequals(value, "content")) { output = "content"; return true; }
    if (bi_try_size(source, output)) return true;
    return bi_try_contextual_nonnegative(source, output);
}

static bool bi_try_max_size(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    if (str_iequals(t, "auto") || str_iequals(t, "none")) { out = t; return true; }
    aether_css_length l;
    if (bi_css_length(t, l, true)) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_length(const char* src, std::string& out) {
    if (!src || !*src) return false;
    std::string t = str_trim(src);
    aether_css_length l;
    if (bi_css_length(t, l, false)) {
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

// Flex factors require exactly one nonnegative CSS number before conversion.
// Keep float conversion native (no double rounding) and saturate only confirmed
// positive overflow; finite ERANGE, including subnormal underflow, stays valid.
static bool bi_flex_number_value(const std::string& source, float& result) {
    css_component_cursor cursor(source);
    css_component_value component, extra;
    if (!cursor.next(component) ||
        component.token.serialization != css_variable_serialization::number) return false;
    if (cursor.next(extra) || !cursor.valid()) return false;
    const std::string number = source.substr(component.begin,
        component.token.numeric_end - component.begin);
    bool nonzero = false;
    for (char digit : number) {
        if (digit == 'e' || digit == 'E') break;
        if (digit >= '1' && digit <= '9') nonzero = true;
    }
    // Exponent digits do not make -0e10 negative. Reject tiny negatives before
    // conversion can round them to zero, then canonicalize all genuine zeros.
    if (nonzero && number.front() == '-') return false;
    if (!nonzero) { result = 0.0f; return true; }
    aether::css::c_numeric_locale locale;
    const auto converted = locale.parse_float(number);
    float value;
    if (converted.valid) {
        value = converted.value;
        if (!std::isfinite(value) || value < 0) return false;
    } else if (converted.positive_overflow) {
        value = std::numeric_limits<float>::max();
    } else return false;
    if (value == 0) value = 0.0f;
    result = value;
    return true;
}

static bool bi_try_non_negative_number(const char* src, std::string& out) {
    if (!src) return false;
    const std::string source(src); // Preserve complete/interior aliases of out.
    float value;
    if (!bi_flex_number_value(source, value)) return false;
    char buffer[64];
    const auto formatted = std::to_chars(buffer, buffer + sizeof(buffer), value,
        std::chars_format::general, std::numeric_limits<float>::max_digits10);
    if (formatted.ec != std::errc{}) return false;
    std::string staged(buffer, formatted.ptr);
    out = std::move(staged);
    return true;
}

static property_apply_result_s bi_apply_flex_number(aether_computed_style_t* style,
        const std::string& source, float aether_computed_style_t::* member) {
    float value;
    if (!bi_flex_number_value(source, value))
        return property_apply_result_s::failure("AETHER-CSS-FLEX-NUMBER",
            "Flex factor requires one nonnegative CSS number.");
    style->*member = value;
    return {};
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

// Opacity alone accepts one CSS number/percentage, converted in a private C
// locale. Finite binary64 underflow is allowed; nonfinite overflow is not.
static bool bi_opacity_value(const std::string& source, float& result) {
    css_component_cursor cursor(source);
    css_component_value component, extra;
    if (!cursor.next(component)) return false;
    const auto category = component.token.serialization;
    const bool percentage = category == css_variable_serialization::percentage;
    if (category != css_variable_serialization::number && !percentage) return false;
    if (cursor.next(extra) || !cursor.valid()) return false;
    const std::string number = source.substr(component.begin,
        component.token.numeric_end - component.begin);
    aether::css::c_numeric_locale locale;
    const auto converted = locale.parse_double(number);
    if (!converted.valid) return false;
    const double scaled = percentage ? converted.value / 100.0 : converted.value;
    const double bounded = std::clamp(scaled, 0.0, 1.0);
    float value = static_cast<float>(bounded);
    if (value == 0) value = 0.0f; // Canonical positive zero, including underflow.
    if (!std::isfinite(value) || value < 0 || value > 1) return false;
    result = value;
    return true;
}

// Explicit inheritance copies the computed scalar, not accumulated paint alpha.
static void bi_inherit_opacity(aether_computed_style_t* style,
                               aether::style::ComputedStyleReadView parent) {
    style->opacity = parent.scalars().opacity;
}

static bool bi_try_opacity(const char* src, std::string& out) {
    if (!src) return false;
    // Runtime substitution can alias out, including an interior source pointer.
    const std::string source(src);
    float value;
    if (!bi_opacity_value(source, value)) return false;
    char buffer[64];
    const auto formatted = std::to_chars(buffer, buffer + sizeof(buffer), value,
        std::chars_format::general, std::numeric_limits<float>::max_digits10);
    if (formatted.ec != std::errc{}) return false;
    std::string staged(buffer, formatted.ptr);
    out = std::move(staged);
    return true;
}

static property_apply_result_s bi_apply_opacity(aether_computed_style_t* style,
                                               const std::string& source) {
    float value;
    if (!bi_opacity_value(source, value))
        return property_apply_result_s::failure("AETHER-CSS-OPACITY",
            "Opacity requires one finite CSS number or percentage.");
    style->opacity = value;
    return {};
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
    for (auto& s : sizes) { if (str_iequals(t, s)) { out = s; return true; } }
    aether_css_length l;
    if (bi_css_length(t, l, true)) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_font_family(const char* src, std::string& out) {
    if (!src || !*src) return false;
    const auto families = css_split_component_delimited(src, ',');
    if (families.empty()) return false;
    const auto& family = families.front();
    css_component_cursor cursor(family);
    css_component_value component;
    std::string value;
    while (cursor.next(component)) {
        const auto spelling = family.substr(component.begin, component.end - component.begin);
        css_variable_lexer decoder(spelling.data(), spelling.size());
        if (component.token.kind != css_variable_token_kind::ident) {
            if (!value.empty() || component.token.serialization != css_variable_serialization::string ||
                !decoder.decode_string(value) || value.empty() ||
                cursor.next(component) || !cursor.valid()) return false;
            out.swap(value);
            return true;
        }
        if (!value.empty()) value += ' ';
        if (!decoder.decode_identifier([](void* context, const char* bytes, size_t count) {
            static_cast<std::string*>(context)->append(bytes, count); return true;
        }, &value)) return false;
    }
    if (!cursor.valid() || value.empty()) return false;
    out.swap(value);
    return true;
}

// Line-height preserves the existing zero-valued NUMBER calc compatibility.
// Both validation and application use the same typed evaluation policy.
static bool bi_read_line_height(const std::string& source, aether::css::calc_value_s& result) {
    double original_number = 0.0;
    const bool calc = (source.size() >= 5 && str_iequals(source.substr(0, 5), "calc("));
    if (!(calc ? aether::css::try_parse_calc(source, result) :
                 aether::css::try_parse_numeric_literal(source, result, &original_number))) return false;
    if (!calc && original_number < 0.0) return false;
    if (result.kind == aether::css::calc_value_kind_e::number &&
        (!std::isfinite(result.number) || std::abs(result.number) > std::numeric_limits<float>::max())) return false;
    return calc || (result.kind == aether::css::calc_value_kind_e::number ? result.number >= 0 : result.length.value >= 0);
}

static bool bi_try_line_height(const char* src, std::string& out) {
    std::string t = str_trim(src);
    if (str_iequals(t, "normal")) { out = "normal"; return true; }
    aether::css::calc_value_s result;
    if (bi_read_line_height(t, result)) {
        out = t; return true;
    }
    return false;
}

static bool bi_try_border_width(const char* src, std::string& out) {
    std::string t = str_trim(src);
    static const char* kw[] = { "thin", "medium", "thick" };
    for (auto& k : kw) { if (str_iequals(t, k)) { out = t; return true; } }
    aether_css_length length;
    if (!bi_css_length(t, length, true, false)) return false;
    out = t; return true;
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
    for (const auto& axis : css_split_component_delimited(src, '/')) {
        for (const auto& component : css_split_components(axis)) {
            aether_css_length length;
            if (!bi_css_length(component, length, true)) return false;
        }
    }
    out = str_trim(src);
    return true;
}

// Physical corner longhands have a pair grammar, not the aggregate 1-4/slash grammar.
static bool bi_read_corner_radius(const std::string& source, aether_corner_radius& out) {
    bool valid = false;
    const auto components = css_split_components(source, &valid);
    if (!valid || components.empty() || components.size() > 2) return false;
    aether_corner_radius parsed{};
    if (!bi_css_length(components[0], parsed.horizontal, true)) return false;
    parsed.vertical = parsed.horizontal;
    if (components.size() == 2 && !bi_css_length(components[1], parsed.vertical, true)) return false;
    out = parsed;
    return true;
}

static bool bi_try_corner_radius(const char* source, std::string& out) {
    if (!source) return false;
    // Deferred resolution aliases source and out, including potentially interior pointers.
    const std::string owned = str_trim(source);
    aether_corner_radius parsed{};
    if (!bi_read_corner_radius(owned, parsed)) return false;
    out = owned;
    return true;
}

static bool bi_try_gradient(const char* src, std::string& out) {
    aether_linear_gradient gradient{};
    if (!aether_linear_gradient_try_parse(src, &gradient)) return false;
    aether_linear_gradient_free(&gradient);
    out = str_trim(src);
    return true;
}

static property_apply_result_s bi_apply_gradient(aether_computed_style_t* st, const std::string& value);

namespace {
struct backdrop_filter_staging {
    aether_backdrop_filter_list value{};
    ~backdrop_filter_staging() { aether_backdrop_filter_list_free(&value); }
    void publish(aether_computed_style_t* style) {
        aether_backdrop_filter_list_free(&style->backdrop_filters);
        style->backdrop_filters = value;
        value = {};
    }
};
}

static property_apply_result_s bi_backdrop_filter_error(aether_backdrop_filter_parse_error_e error) {
    const char* message = error == aether_backdrop_filter_parse_error_e::unsupported_operation
        ? "The backdrop-filter function is not supported by the current compositor model."
        : error == aether_backdrop_filter_parse_error_e::unsupported_parameter
            ? "The backdrop-filter function has an unsupported parameter."
            : "The backdrop-filter value is syntactically invalid.";
    return property_apply_result_s::failure("AETHER-CSS-BACKDROP-FILTER", message);
}

static bool bi_try_backdrop_filter(const char* source, std::string& out) {
    // Deferred validation can alias source and out. Preserve original inner trivia.
    const std::string owned = str_trim(source ? source : "");
    backdrop_filter_staging parsed;
    if (!aether_backdrop_filter_list_try_parse(owned, &parsed.value)) return false;
    out = owned;
    return true;
}

static property_apply_result_s bi_backdrop_filter_parse_failure(const char* source) {
    backdrop_filter_staging parsed;
    aether_backdrop_filter_parse_error_e error{};
    if (aether_backdrop_filter_list_try_parse(source ? source : "", &parsed.value, &error)) return {};
    return bi_backdrop_filter_error(error);
}

static property_apply_result_s bi_apply_backdrop_filter(property_apply_context_s& context,
                                                        const std::string& value) {
    backdrop_filter_staging parsed;
    aether_backdrop_filter_parse_error_e error{};
    if (!aether_backdrop_filter_list_try_parse(value, &parsed.value, &error))
        return bi_backdrop_filter_error(error);
    const auto inputs = context.measures->read().inputs();
    for (int32_t i = 0; i < parsed.value.count; ++i) {
        auto& length = parsed.value.items[i].length_amount;
        length = measure_compute_length(length, inputs);
        length.value = std::max(0.f, length.value);
    }
    parsed.publish(context.style);
    return {};
}

static void bi_inherit_backdrop_filter(aether_computed_style_t* style, ComputedStyleReadView parent) {
    const auto filters = parent.backdrop_filters();
    backdrop_filter_staging parsed;
    if (!filters.empty()) {
        if (filters.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
            filters.size() > std::numeric_limits<size_t>::max() / sizeof(aether_backdrop_filter))
            throw std::bad_alloc();
        parsed.value.items = static_cast<aether_backdrop_filter*>(
            std::malloc(filters.size() * sizeof(aether_backdrop_filter)));
        if (!parsed.value.items) throw std::bad_alloc();
        parsed.value.count = static_cast<int32_t>(filters.size());
        std::copy(filters.begin(), filters.end(), parsed.value.items);
    }
    parsed.publish(style);
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
    std::string single_gradient;
    if (bi_try_gradient(value.c_str(), single_gradient))
        return bi_apply_gradient(st, single_gradient);

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
        throw std::bad_alloc();
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

static property_apply_result_s bi_shadow_result(shadow_value_error error, bool box) {
    if (error == shadow_value_error::allocation) throw std::bad_alloc();
    if (error == shadow_value_error::none) return {};
    return property_apply_result_s::failure(shadow_value_diagnostic_code(box), shadow_value_error_message(error, box));
}

static property_apply_result_s bi_apply_box_shadow(property_apply_context_s& context,
                                                   const std::string& value) {
    const auto inputs = context.measures ? context.measures->read().inputs() : measure_inputs_s{};
    return bi_shadow_result(shadow_list_apply(value, true, context.style->box_shadows,
        context.color_resolution->box_shadow_uses_current_color, {}, context.measures ? &inputs : nullptr), true);
}

static property_apply_result_s bi_apply_text_shadow(property_apply_context_s& context,
                                                    const std::string& value) {
    const auto inputs = context.measures ? context.measures->read().inputs() : measure_inputs_s{};
    return bi_shadow_result(shadow_list_apply(value, false, context.style->text_shadows,
        context.color_resolution->text_shadow_uses_current_color, {}, context.measures ? &inputs : nullptr), false);
}

static bool bi_try_shadow(const char* source, std::string& output, bool box) {
    // Own the input before assigning output: deferred validation aliases them.
    const std::string value = str_trim(source ? source : "");
    parsed_shadow_list parsed;
    if (shadow_list_parse(value, box, parsed) != shadow_value_error::none) return false;
    if (!str_iequals(value, "none")) for (const auto& item : css_split_component_delimited(value, ',')) {
        unsigned index = 0;
        for (const auto& token : css_split_components(item)) {
            aether_color_rgba color;
            if (str_iequals(token,"inset") || str_iequals(token,"currentColor") || aether_color_rgba_try_parse(token.c_str(), &color)) continue;
            aether_css_length length;
            if (!bi_css_length(token, length, index == 2, false)) return false;
            ++index;
        }
    }
    output = value;
    return true;
}

static property_apply_result_s bi_shadow_parse_failure(const char* source, bool box) {
    parsed_shadow_list parsed;
    return bi_shadow_result(shadow_list_parse(source ? source : "", box, parsed), box);
}

static bool bi_try_transform(const char* src, std::string& out) {
    // Deferred validation may pass out.c_str() as src. Own it before writing out.
    const std::string value = str_trim(src ? src : "");
    aether_transform_list parsed{};
    if (!aether_transform_list_try_parse(value.c_str(), &parsed)) return false;
    aether_transform_list_free(&parsed);
    out = value;
    return true;
}

static property_apply_result_s bi_transform_parse_failure(const char*) {
    return property_apply_result_s::failure("AETHER-CSS-TRANSFORM",
        "The transform value is not a supported 2D transform list.");
}

static bool bi_try_transform_origin(const char* src, std::string& out) {
    // Deferred validation may pass out.c_str() as src. Own it before writing out.
    const std::string value = str_trim(src ? src : "");
    aether_transform_origin parsed{};
    if (!aether_transform_origin_try_parse(value.c_str(), &parsed)) return false;
    out = value;
    return true;
}

static property_apply_result_s bi_origin_parse_failure(const char*) {
    return property_apply_result_s::failure("AETHER-CSS-TRANSFORM-ORIGIN",
        "transform-origin accepts one or two supported 2D position components.");
}

static property_css_wide_e bi_origin_css_wide(const std::string& source) {
    css_component_cursor cursor(source);
    css_component_value component, extra;
    if (!cursor.next(component) || component.token.kind != css_variable_token_kind::ident ||
        cursor.next(extra) || !cursor.valid()) return property_css_wide_e::none;
    const std::string token = source.substr(component.begin, component.end - component.begin);
    std::string keyword;
    css_variable_lexer lexer(token.data(), token.size());
    if (!lexer.decode_identifier([](void* data, const char* bytes, size_t size) {
            static_cast<std::string*>(data)->append(bytes, size); return true;
        }, &keyword)) return property_css_wide_e::none;
    if (aether::ascii_iequals(keyword, "initial") || aether::ascii_iequals(keyword, "revert"))
        return property_css_wide_e::initial;
    if (aether::ascii_iequals(keyword, "inherit")) return property_css_wide_e::inherit;
    if (aether::ascii_iequals(keyword, "unset")) return property_css_wide_e::unset;
    // revert-layer is deliberately unsupported, not an alias for initial.
    return property_css_wide_e::none;
}

static aether_css_length bi_origin_freeze_em(aether_css_length length, float font_size) {
    const double em = length.unit == AETHER_CSS_UNIT_EM ? length.value :
        length.unit == AETHER_CSS_UNIT_LINEAR ? length.linear_em : 0.0;
    if (length.unit != AETHER_CSS_UNIT_EM && length.unit != AETHER_CSS_UNIT_LINEAR) return length;
    if (em == 0.0 && length.unit == AETHER_CSS_UNIT_LINEAR) return length;
    const double pixels = (length.unit == AETHER_CSS_UNIT_LINEAR ? length.value : 0.0) + em * font_size;
    // Compute in double and saturate at the finite storage boundary. Percent,
    // rem and viewport terms keep their shared context and are never rebased.
    const double bound = std::numeric_limits<float>::max();
    length.value = static_cast<float>(std::isnan(pixels) ? 0.0 : std::clamp(pixels, -bound, bound));
    if (length.unit == AETHER_CSS_UNIT_EM) length.unit = AETHER_CSS_UNIT_PX;
    else length.linear_em = 0.0f;
    return length;
}

static void bi_inherit_transform_origin(aether_computed_style_t* style,
                                        ComputedStyleReadView parent_view) {
    const auto& parent=parent_view.scalars();
    style->transform_origin = {bi_origin_freeze_em(parent.transform_origin.x, parent.font_size),
                               bi_origin_freeze_em(parent.transform_origin.y, parent.font_size)};
}

static property_apply_result_s bi_apply_transform(aether_computed_style_t* style, const std::string& value) {
    aether_transform_list parsed{};
    if (!aether_transform_list_try_parse(value.c_str(), &parsed)) {
        return bi_transform_parse_failure(value.c_str());
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
                if (!s || !css_single_identifier(s, v)) return false;
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

    // visibility: collapse intentionally uses the hidden presentation fallback
    // for every display. Table/flex geometric collapse is not implemented.
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
        static const char* kw[] = { "visible", "hidden", "auto", "scroll", "clip" };
        static const int32_t vals[] = {
            AETHER_OVERFLOW_VISIBLE, AETHER_OVERFLOW_HIDDEN, AETHER_OVERFLOW_AUTO,
            AETHER_OVERFLOW_SCROLL, AETHER_OVERFLOW_CLIP
        };
        reg->register_property("overflow-x", false,
            STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
            "visible",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 5, v)) return false;
                for (int i = 0; i < 5; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) {
                st->overflow_x = std::atoi(v.c_str());
            });
    }

    // overflow-y
    {
        static const char* kw[] = { "visible", "hidden", "auto", "scroll", "clip" };
        static const int32_t vals[] = {
            AETHER_OVERFLOW_VISIBLE, AETHER_OVERFLOW_HIDDEN, AETHER_OVERFLOW_AUTO,
            AETHER_OVERFLOW_SCROLL, AETHER_OVERFLOW_CLIP
        };
        reg->register_property("overflow-y", false,
            STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
            "visible",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 5, v)) return false;
                for (int i = 0; i < 5; ++i)
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
        auto pp = [](const char* s, std::string& o) -> bool { return bi_try_contextual_nonnegative(s, o); };
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
        static const int32_t vals[] = { AETHER_FLEX_DIRECTION_ROW, AETHER_FLEX_DIRECTION_ROW_REVERSE,
            AETHER_FLEX_DIRECTION_COLUMN, AETHER_FLEX_DIRECTION_COLUMN_REVERSE };
        reg->register_property("flex-direction", false, STYLE_IMPACT_LAYOUT, "row",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 4, v)) return false;
                for (int i = 0; i < 4; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) { st->flex_direction = std::atoi(v.c_str()); });
        reg->definitions.back().css_wide = bi_origin_css_wide;
        reg->definitions.back().inherit_explicit = [](aether_computed_style_t* st, ComputedStyleReadView parent) {
            st->flex_direction = parent.scalars().flex_direction;
        };
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
    reg->register_property_checked("flex-grow", false, STYLE_IMPACT_LAYOUT, "0",
        bi_try_non_negative_number,
        [](aether_computed_style_t* st, const std::string& v) {
            return bi_apply_flex_number(st, v, &aether_computed_style_t::flex_grow);
        });

    // flex-shrink
    reg->register_property_checked("flex-shrink", false, STYLE_IMPACT_LAYOUT, "1",
        bi_try_non_negative_number,
        [](aether_computed_style_t* st, const std::string& v) {
            return bi_apply_flex_number(st, v, &aether_computed_style_t::flex_shrink);
        });

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
            AETHER_JUSTIFY_LOGICAL_START,AETHER_JUSTIFY_FLEX_START,
            AETHER_JUSTIFY_LOGICAL_END,AETHER_JUSTIFY_FLEX_END,
            AETHER_JUSTIFY_CENTER,AETHER_JUSTIFY_SPACE_BETWEEN,AETHER_JUSTIFY_CENTER,AETHER_JUSTIFY_CENTER
        };
        reg->register_property("justify-content", false, STYLE_IMPACT_LAYOUT, "flex-start",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 8, v)) return false;
                for (int i = 0; i < 8; ++i)
                    if (str_iequals(v, kw[i])) { o = std::to_string(vals[i]); return true; }
                return false;
            },
            [](aether_computed_style_t* st, const std::string& v) { st->justify_content = std::atoi(v.c_str()); });
        reg->definitions.back().css_wide = bi_origin_css_wide;
        reg->definitions.back().inherit_explicit = [](aether_computed_style_t* st, ComputedStyleReadView parent) {
            st->justify_content = parent.scalars().justify_content;
        };
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
        bi_try_gap_length,
        [](aether_computed_style_t* st, const std::string& v) {
            aether_css_length l; aether_css_length_try_parse(v.c_str(), &l);
            st->gap = aether_css_length_resolve(l, 0.0f, 16.0f, 16.0f, 1920.0f, 1080.0f);
        });

    // column-gap
    reg->register_property("column-gap", false, STYLE_IMPACT_LAYOUT, "0",
        bi_try_gap_length,
        [](aether_computed_style_t* st, const std::string& v) {
            aether_css_length l; aether_css_length_try_parse(v.c_str(), &l);
            st->gap = aether_css_length_resolve(l, 0.0f, 16.0f, 16.0f, 1920.0f, 1080.0f);
        });

    // order
    reg->register_property("order", false, STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST, "0",
        [](const char* source, std::string& out) {
            if (!source) return false;
            const auto value = str_trim(source);
            if (value.empty()) return false;
            size_t i = value[0] == '+' || value[0] == '-' ? 1 : 0;
            if (i == value.size()) return false;
            const bool negative = value[0] == '-';
            const uint64_t limit = negative ? uint64_t(INT32_MAX) + 1 : uint64_t(INT32_MAX);
            uint64_t magnitude = 0;
            for (; i < value.size(); ++i) {
                if (value[i] < '0' || value[i] > '9') return false;
                const unsigned digit = unsigned(value[i] - '0');
                if (magnitude > (limit - digit) / 10) return false;
                magnitude = magnitude * 10 + digit;
            }
            const int64_t signed_value = negative ? -int64_t(magnitude) : int64_t(magnitude);
            out = std::to_string(signed_value);
            return true;
        }, [](aether_computed_style_t*, const std::string&) {});
    reg->definitions.back().css_wide = bi_origin_css_wide;
    reg->definitions.back().apply_with_context = [](property_apply_context_s& context, const std::string& value) {
        if (context.measures) context.measures->set_flex_order(static_cast<int32_t>(std::stoll(value)));
    };
    reg->definitions.back().inherit_context = [](property_apply_context_s& context, ComputedStyleReadView,
                                                const MeasuresReadView* parent) {
        if (context.measures) context.measures->set_flex_order(parent ? parent->flex_order() : 0);
    };

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
        "normal", bi_try_line_height,
        [](aether_computed_style_t* st, const std::string& v) {
            aether::css::calc_value_s result;
            if (bi_read_line_height(v, result))
                st->line_height = result.kind == aether::css::calc_value_kind_e::number ? static_cast<float>(result.number) :
                    aether_css_length_resolve(result.length, 0.0f, st->font_size, 16.0f, 1920.0f, 1080.0f);
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
    reg->register_property_with_context("background-color", false, STYLE_IMPACT_PAINT,
        "transparent", bi_try_color,
        [](property_apply_context_s& context, const std::string& value) {
            if (str_iequals(value, "currentColor")) {
                context.color_resolution->background_uses_current_color = true;
            } else {
                aether_color_rgba color;
                if (aether_color_rgba_try_parse(value.c_str(), &color)) {
                    context.style->background_color = color;
                    context.color_resolution->background_uses_current_color = false;
                }
            }
        });
    // Background is not inherited by default. Only explicit inherit carries its
    // symbol, which the final color pass resolves against the child's producer.
    // Unlike aggregate geometry inheritance, this adds no measure dependency.
    reg->definitions.back().css_wide = bi_origin_css_wide;
    reg->definitions.back().inherit_context = [](property_apply_context_s& context,
            ComputedStyleReadView parent, const MeasuresReadView*) {
        context.style->background_color = parent.scalars().background_color;
        context.color_resolution->background_uses_current_color =
            context.parent_color_resolution && context.parent_color_resolution->background();
    };

    // Backdrop-filter is a non-inherited ordered operation list. The compositor v1
    // currently understands blur(); unsupported functions are diagnosed explicitly.
    for (const char* name : {"backdrop-filter", "-webkit-backdrop-filter"}) {
        reg->register_property_checked_with_context(name, false, STYLE_IMPACT_PAINT | STYLE_IMPACT_COMPOSITE,
            "none", bi_try_backdrop_filter, bi_apply_backdrop_filter, bi_backdrop_filter_parse_failure);
        reg->definitions.back().inherit_explicit = bi_inherit_backdrop_filter;
        reg->definitions.back().css_wide = bi_origin_css_wide;
    }

    // background-image
    reg->register_property_checked("background-image", false, STYLE_IMPACT_PAINT,
        "none", bi_try_background_image,
        [](aether_computed_style_t* st, const std::string& v) -> property_apply_result_s {
            return bi_apply_background_image(st, v);
        });

    // These list-valued longhands deliberately apply after background-image.
    // CSS repeats the supplied list modulo its length when there are more layers.
    reg->register_property_checked("background-position", false, STYLE_IMPACT_PAINT,
        "0% 0%",
        [](const char* source, std::string& out) {
            // Substitution may pass out.c_str() as source: stage before publishing.
            std::string text = str_trim(source ? source : "");
            std::vector<aether_background_position_s> positions;
            if (!aether_background_position_list_try_parse(text, positions)) return false;
            out = std::move(text);
            return true;
        },
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
    reg->register_property_checked("opacity", false, STYLE_IMPACT_COMPOSITE | STYLE_IMPACT_HITTEST,
        "1", bi_try_opacity, bi_apply_opacity, {}, bi_inherit_opacity, bi_origin_css_wide);

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
        reg->register_property(name, false, STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST, "0", bi_try_corner_radius,
            [member](aether_computed_style_t* st, const std::string& value) {
                aether_corner_radius parsed{};
                if (bi_read_corner_radius(value, parsed)) st->border_radii.*member = parsed;
            });
    };
    register_corner_radius("border-top-left-radius", &aether_border_radii::top_left);
    register_corner_radius("border-top-right-radius", &aether_border_radii::top_right);
    register_corner_radius("border-bottom-right-radius", &aether_border_radii::bottom_right);
    register_corner_radius("border-bottom-left-radius", &aether_border_radii::bottom_left);

    // box-shadow
    reg->register_property_checked_with_context("box-shadow", false, STYLE_IMPACT_PAINT, "none",
        [](const char* source, std::string& output) { return bi_try_shadow(source, output, true); },
        bi_apply_box_shadow, [](const char* source) { return bi_shadow_parse_failure(source, true); });
    reg->register_property_checked_with_context("text-shadow", true, STYLE_IMPACT_PAINT, "none",
        [](const char* source, std::string& output) { return bi_try_shadow(source, output, false); },
        bi_apply_text_shadow, [](const char* source) { return bi_shadow_parse_failure(source, false); });

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
        bi_apply_transform, bi_transform_parse_failure);

    reg->register_property_checked("transform-origin", false,
        STYLE_IMPACT_COMPOSITE | STYLE_IMPACT_HITTEST, "50% 50%", bi_try_transform_origin,
        bi_apply_transform_origin, bi_origin_parse_failure, bi_inherit_transform_origin, bi_origin_css_wide);

    transition_properties_register_longhands(reg);
    paused_animation_register_longhands(reg);

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
        for (const auto& part : parts) if (bi_is_global_keyword(part)) return false;
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
            [box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            }, {t[0], t[1], t[2], t[3]});
    }

    // padding shorthand
    {
        const char* t[4] = { "padding-top", "padding-right", "padding-bottom", "padding-left" };
        reg->register_shorthand("padding",
            [box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            }, {t[0], t[1], t[2], t[3]});
    }

    // inset shorthand
    {
        const char* t[4] = { "top", "right", "bottom", "left" };
        reg->register_shorthand("inset",
            [box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            }, {t[0], t[1], t[2], t[3]});
    }

    // border-width shorthand
    {
        const char* t[4] = { "border-top-width", "border-right-width", "border-bottom-width", "border-left-width" };
        reg->register_shorthand("border-width",
            [box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            }, {t[0], t[1], t[2], t[3]});
    }

    // border-style shorthand
    {
        const char* t[4] = { "border-top-style", "border-right-style", "border-bottom-style", "border-left-style" };
        reg->register_shorthand("border-style",
            [box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            }, {t[0], t[1], t[2], t[3]});
    }

    // border-color shorthand
    {
        const char* t[4] = { "border-top-color", "border-right-color", "border-bottom-color", "border-left-color" };
        reg->register_shorthand("border-color",
            [box_shorthand, t](const char* src, style_shorthand_writer_s& w) -> bool {
                return box_shorthand(src, t, w);
            }, {t[0], t[1], t[2], t[3]});
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
        }, {"overflow-x", "overflow-y"});

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
            if (!bi_try_gap_length(parts[0].c_str(), v1)) return false;
            if (parts.size() == 2) {
                std::string v2;
                if (!bi_try_gap_length(parts[1].c_str(), v2)) return false;
                writer.set("row-gap", v1.c_str());
                writer.set("column-gap", v2.c_str());
            } else {
                writer.set("row-gap", v1.c_str());
                writer.set("column-gap", v1.c_str());
            }
            return true;
        }, {"row-gap", "column-gap"});

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
                    writer.set("background-position", "0% 0%");
                    writer.set("background-size", "auto auto");
                    writer.set("background-repeat", "repeat repeat");

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
        }, {"background-color", "background-image", "background-position", "background-size", "background-repeat"});

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
        }, {"border-top-width", "border-top-style", "border-top-color", "border-right-width", "border-right-style", "border-right-color", "border-bottom-width", "border-bottom-style", "border-bottom-color", "border-left-width", "border-left-style", "border-left-color"});

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
            }, {shorthand + "-width", shorthand + "-style", shorthand + "-color"});
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
            std::string sole_basis;
            if (bi_try_intrinsic_keyword(t.c_str(), sole_basis)) {
                writer.set("flex-grow", "1"); writer.set("flex-shrink", "1");
                writer.set("flex-basis", sole_basis.c_str()); return true;
            }
            auto parts = bi_split_ws(t);
            if (parts.size() < 1 || parts.size() > 3) return false;
            std::string grow_test;
            if (!bi_try_non_negative_number(parts[0].c_str(), grow_test)) return false;
            std::string grow = std::move(grow_test), shrink = "1", basis = "0%";
            if (parts.size() >= 2) {
                std::string shrink_test;
                if (bi_try_non_negative_number(parts[1].c_str(), shrink_test)) {
                    shrink = std::move(shrink_test);
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
        }, {"flex-grow", "flex-shrink", "flex-basis"});

    // Aggregate inheritance copies computed payloads, never reparses the parent
    // declaration against this child's font. Public list helpers stage ownership.
    for (auto& definition : reg->definitions) {
        const auto name = definition.name;
        if (name != "transform" && name != "transform-origin" && name != "border-radius" &&
            name != "border-top-left-radius" && name != "border-top-right-radius" &&
            name != "border-bottom-left-radius" && name != "border-bottom-right-radius" &&
            name != "background-image" && name != "background-position" && name != "background-size" &&
            name != "text-shadow" && name != "box-shadow") continue;
        definition.css_wide = bi_origin_css_wide;
        definition.inherit_context = [name](property_apply_context_s& c, ComputedStyleReadView pv, const MeasuresReadView* pm) {
            const auto& p=pv.scalars();
            auto& s = *c.style;
            if (name == "text-shadow" || name == "box-shadow") {
                const bool box = name == "box-shadow";
                const auto inherited = box ? pv.box_shadows() : pv.text_shadows();
                auto& destination = box ? s.box_shadows : s.text_shadows;
                auto& references = box ? c.color_resolution->box_shadow_uses_current_color : c.color_resolution->text_shadow_uses_current_color;
                std::vector<bool> copied_references;
                if (c.parent_color_resolution) { const auto& r=*c.parent_color_resolution; const size_t n=box?r.box_shadow_count():r.text_shadow_count(); copied_references.reserve(n); for(size_t i=0;i<n;++i)copied_references.push_back(box?r.box_shadow_at(i):r.text_shadow_at(i)); }
                aether_shadow_list copied{};
                if (copy_builder_shadows(inherited, &copied)!=AETHER_OK) throw std::bad_alloc();
                aether_shadow_list_free(&destination); destination = copied;
                references = std::move(copied_references);
            } else if (name == "transform") {
                aether_transform_list copied{};
                if (copy_builder_transforms(pv.transforms(), &copied)!=AETHER_OK) throw std::bad_alloc();
                aether_transform_list_free(&s.transforms); s.transforms = copied;
            } else if (name == "transform-origin") s.transform_origin = p.transform_origin;
            else if (name == "border-radius") s.border_radii = p.border_radii;
            else if (name == "border-top-left-radius") s.border_radii.top_left = p.border_radii.top_left;
            else if (name == "border-top-right-radius") s.border_radii.top_right = p.border_radii.top_right;
            else if (name == "border-bottom-left-radius") s.border_radii.bottom_left = p.border_radii.bottom_left;
            else if (name == "border-bottom-right-radius") s.border_radii.bottom_right = p.border_radii.bottom_right;
            else if (name == "background-image") {
                aether_background_layer_list copied{};
                if (copy_builder_backgrounds(pv, &copied)!=AETHER_OK) throw std::bad_alloc();
                // Only the image payload inherits. Position, size and repeat
                // are independent longhands with the child's own initial/cascade.
                for (int32_t i = 0; i < copied.count; ++i) {
                    copied.items[i].repeat_x = AETHER_BACKGROUND_REPEAT;
                    copied.items[i].repeat_y = AETHER_BACKGROUND_REPEAT;
                }
                aether_background_layer_list_free(&s.background_layers); s.background_layers = copied;
                measure_apply_background_longhands(s, c.measures->read());
            } else if (name == "background-position") {
                std::vector<measure_background_position_s> positions;
                if(pm){positions.reserve(pm->position_count());for(size_t i=0;i<pm->position_count();++i){auto v=pm->position_at(i);positions.push_back({v.x,v.y});}}else positions.emplace_back();
                c.measures->set_background_positions(std::move(positions));
                measure_apply_background_longhands(s, c.measures->read());
            } else if (name == "background-size") {
                std::vector<measure_background_size_s> sizes;
                if(pm){sizes.reserve(pm->size_count());for(size_t i=0;i<pm->size_count();++i){auto v=pm->size_at(i);sizes.push_back({v.kind,v.x,v.y});}}else sizes.emplace_back();
                c.measures->set_background_sizes(std::move(sizes));
                measure_apply_background_longhands(s, c.measures->read());
            }
            uint32_t dep = MEASURE_PARENT;
            if (pm) dep |= pm->dependency(name) & MEASURE_PERCENTAGE;
            c.measures->set_property_dependencies(dep);
        };
    }

    // A CSS longhand list is computed even when there is no image to paint.
    // Keeping it separately prevents background-image:inherit from inheriting
    // unrelated position/size values or discarding the parent's unpainted list.
    for (auto& definition : reg->definitions) {
        const auto name = definition.name;
        if (name != "background-position" && name != "background-size") continue;
        const auto legacy = definition.apply_checked;
        definition.apply_with_context_checked = [name, legacy](property_apply_context_s& c, const std::string& value) -> property_apply_result_s {
            if (!c.measures) return legacy(c.style, value);
            if (name == "background-position") {
                std::vector<aether_background_position_s> parsed;
                if (!aether_background_position_list_try_parse(value, parsed))
                    return property_apply_result_s::failure("AETHER-CSS-BACKGROUND-POSITION", "Invalid background-position list.");
                std::vector<measure_background_position_s> computed;
                computed.reserve(parsed.size());
                for (const auto& item : parsed) computed.push_back({measure_compute_length(item.x, c.measures->read().inputs()),measure_compute_length(item.y, c.measures->read().inputs())});
                c.measures->set_background_positions(std::move(computed));
            } else {
                std::vector<aether_background_size_s> parsed;
                if (!aether_background_size_list_try_parse(value, parsed))
                    return property_apply_result_s::failure("AETHER-CSS-BACKGROUND-SIZE", "Invalid background-size list.");
                std::vector<measure_background_size_s> computed;
                computed.reserve(parsed.size());
                for (const auto& item : parsed) computed.push_back({item.kind,measure_compute_length(item.x, c.measures->read().inputs()),measure_compute_length(item.y, c.measures->read().inputs())});
                c.measures->set_background_sizes(std::move(computed));
            }
            measure_apply_background_longhands(*c.style, c.measures->read());
            return {};
        };
    }

    // Private contextual route: legacy applicators remain available to flat callers.
    for (auto& definition : reg->definitions) {
        const auto name = definition.name;
        const auto field = [name](auto& st) -> aether_css_length* {
            if (name == "width") return &st.width;
            if (name == "height") return &st.height;
            if (name == "min-width") return &st.min_width;
            if (name == "min-height") return &st.min_height;
            if (name == "max-width") return &st.max_width;
            if (name == "max-height") return &st.max_height;
            if (name == "flex-basis") return &st.flex_basis.length;
            if (name == "top") return &st.inset.top;
            if (name == "right") return &st.inset.right;
            if (name == "bottom") return &st.inset.bottom;
            if (name == "left") return &st.inset.left;
            if (name == "margin-top") return &st.margin.top;
            if (name == "margin-right") return &st.margin.right;
            if (name == "margin-bottom") return &st.margin.bottom;
            if (name == "margin-left") return &st.margin.left;
            if (name == "padding-top") return &st.padding.top;
            if (name == "padding-right") return &st.padding.right;
            if (name == "padding-bottom") return &st.padding.bottom;
            if (name == "padding-left") return &st.padding.left;
            if (name == "border-top-width") return &st.border_width.top;
            if (name == "border-right-width") return &st.border_width.right;
            if (name == "border-bottom-width") return &st.border_width.bottom;
            if (name == "border-left-width") return &st.border_width.left;
            return nullptr;
        };
        aether_computed_style_t dummy{};
        if (!field(dummy) && name != "font-size" && name != "line-height" &&
            name != "row-gap" && name != "column-gap") continue;
        const auto legacy = definition.apply;
        definition.css_wide = bi_origin_css_wide;
        definition.apply_with_context = [name, field, legacy](property_apply_context_s& c, const std::string& value) {
            if (!c.measures) { if (legacy) legacy(c.style, value); return; }
            auto& m = *c.measures;
            m.set_property_dependencies(measure_source_dependencies(value));
            if (name == "font-size") {
                const auto units = m.read().inputs_with_self_font(m.read().parent_font_size());
                aether_css_length parsed;
                float font = units.initial_font_size;
                if (aether_css_length_try_parse(value.c_str(), &parsed)) {
                    const auto computed = measure_compute_length(parsed, units);
                    const auto used = measure_resolve_used_length(computed, m.read().parent_font_size(), true);
                    if (used.kind != measure_used_kind_e::resolved)
                        throw std::overflow_error("Invalid computed font-size percentage");
                    font = used.px;
                } else if (value == "smaller") font = m.read().parent_font_size() / 1.2f;
                else if (value == "larger") font = m.read().parent_font_size() * 1.2f;
                else {
                    static const char* names[] = {"xx-small","x-small","small","medium","large","x-large","xx-large"};
                    static const float factors[] = {.6f,.75f,.89f,1.f,1.2f,1.5f,2.f};
                    for (int i = 0; i < 7; ++i) if (value == names[i]) font *= factors[i];
                }
                if (!std::isfinite(font)) throw std::overflow_error("Non-finite computed font-size");
                c.style->font_size = std::max(0.f, font);
                m.set_self_font_size(c.style->font_size);
                auto dep = m.property_dependencies();
                if (dep & (MEASURE_SELF_FONT | MEASURE_PERCENTAGE)) dep = (dep & ~MEASURE_SELF_FONT) | MEASURE_PARENT;
                m.set_property_dependencies(dep);
                return;
            }
            if (name == "line-height") {
                if (value == "normal") m.set_line_height({});
                else {
                    aether::css::calc_value_s parsed;
                    if (bi_read_line_height(value, parsed)) {
                        if (parsed.kind == aether::css::calc_value_kind_e::number)
                            m.set_line_height({measure_line_height_kind_e::number, std::max(0.f, static_cast<float>(parsed.number))});
                        else {
                            const auto used = measure_resolve_used_length(measure_compute_length(parsed.length, m.read().inputs()), m.read().self_font_size(), true);
                            if (used.kind != measure_used_kind_e::resolved)
                                throw std::overflow_error("Invalid computed line-height percentage");
                            m.set_line_height({measure_line_height_kind_e::absolute_css_px, std::max(0.f, used.px)});
                        }
                    }
                }
                const auto line_height = m.read().line_height();
                const double used_height = line_height.kind == measure_line_height_kind_e::absolute_css_px ? line_height.value :
                    double(m.read().self_font_size()) * (line_height.kind == measure_line_height_kind_e::normal ? 1.2 : line_height.value);
                if (!std::isfinite(used_height) || used_height > std::numeric_limits<float>::max())
                    throw std::overflow_error("Non-finite computed line-height");
                if (legacy) legacy(c.style, value); // Explicit legacy projection, never consumed by contextual layout.
                return;
            }
            if (name == "row-gap" || name == "column-gap") {
                aether_css_length parsed;
                if (aether_css_length_try_parse(value.c_str(), &parsed)) {
                    const auto computed = measure_compute_length(parsed, m.read().inputs());
                    if (name == "row-gap") m.set_row_gap(computed); else m.set_column_gap(computed);
                }
                if (legacy) legacy(c.style, value);
                return;
            }
            if (legacy) legacy(c.style, value);
            if (auto* length = field(*c.style)) *length = measure_compute_length(*length, m.read().inputs());
        };
        definition.inherit_context = [name, field](property_apply_context_s& c, ComputedStyleReadView parent_view, const MeasuresReadView* pm) {
            const auto& parent=parent_view.scalars();
            if (auto* dst = field(*c.style)) {
                auto copy = parent;
                *dst = *field(copy);
                if (name == "flex-basis") c.style->flex_basis.kind = parent.flex_basis.kind;
            } else if (name == "font-size") { c.style->font_size = parent.font_size; c.measures->set_self_font_size(parent.font_size); }
            else if (name == "line-height") { c.style->line_height = parent.line_height; if (pm) c.measures->set_line_height(pm->line_height()); }
            else if (pm && name == "row-gap") c.measures->set_row_gap(pm->row_gap());
            else if (pm && name == "column-gap") c.measures->set_column_gap(pm->column_gap());
            uint32_t inherited_dependencies = MEASURE_PARENT;
            if (pm) {
                inherited_dependencies |= pm->dependency(name) & MEASURE_PERCENTAGE;
            }
            c.measures->set_property_dependencies(inherited_dependencies);
        };
    }

    // Comparison math cannot cross the legacy flat representation. Checked
    // applicators reject before touching it; contextual candidates own the tree.
    for (auto& definition : reg->definitions) {
        const auto name = definition.name;
        if (name != "flex-basis" && name != "padding-top" && name != "padding-right" &&
            name != "padding-bottom" && name != "padding-left") continue;
        const auto legacy = definition.apply;
        const auto contextual = definition.apply_with_context;
        const auto inherit = definition.inherit_context;
        auto needs_tree = [](const std::string& value, aether::css::math_tree& tree) {
            aether_css_length flat;
            if (bi_css_length(value, flat, true)) return false;
            return bi_contextual_length(value, tree);
        };
        const auto parse = definition.parse;
        definition.apply_checked = [legacy, needs_tree, parse](aether_computed_style_t* st, const std::string& value) {
            std::string canonical;
            if (!parse || !parse(value.c_str(), canonical))
                return property_apply_result_s::failure("AETHER-CSS-MATH-SYNTAX", "Invalid contextual length syntax.");
            aether::css::math_tree tree;
            if (needs_tree(value, tree))
                return property_apply_result_s::failure("AETHER-CSS-CONTEXT-REQUIRED", "CSS comparison math requires an owning contextual style.");
            if (legacy) legacy(st, value);
            return property_apply_result_s{};
        };
        // Legacy void callers cannot report a status; fail explicitly rather
        // than silently accepting a parsed expression and leaving stale data.
        definition.apply = [legacy, needs_tree, parse](aether_computed_style_t* st, const std::string& value) {
            std::string canonical;
            if (!parse || !parse(value.c_str(), canonical)) throw std::invalid_argument("Invalid contextual length syntax");
            aether::css::math_tree tree;
            if (needs_tree(value, tree)) throw std::invalid_argument("CSS comparison math requires contextual style");
            if (legacy) legacy(st, value);
        };
        definition.apply_with_context_checked = [name, contextual, needs_tree, parse, checked = definition.apply_checked]
            (property_apply_context_s& c, const std::string& value) -> property_apply_result_s {
            if (!c.measures) return checked(c.style, value);
            std::string canonical;
            if (!parse || !parse(value.c_str(), canonical))
                return property_apply_result_s::failure("AETHER-CSS-MATH-SYNTAX", "Invalid contextual length syntax.");
            aether::css::math_tree tree;
            if (!needs_tree(value, tree)) {
                if (contextual) contextual(c, value);
                c.measures->set_tree(name, {});
                return {};
            }
            const auto inputs = c.measures->read().inputs();
            auto computed = aether::css::compute_contextual_math(tree, inputs.self_font_size,
                inputs.root_font_size, inputs.viewport_width, inputs.viewport_height);
            const auto deps = aether::css::math_dependencies(tree);
            uint32_t dependencies = ((deps & 1) ? MEASURE_PERCENTAGE : 0) |
                ((deps & 2) ? MEASURE_SELF_FONT : 0) | ((deps & 4) ? MEASURE_ROOT_FONT : 0) |
                ((deps & 8) ? MEASURE_VIEWPORT : 0);
            // All parsing/computation precedes writes. Candidate/probe owns any
            // allocation failure; no frozen published record is mutated.
            c.measures->set_tree(name, std::move(computed));
            c.measures->set_property_dependencies(dependencies);
            auto placeholder = aether_css_length_zero();
            placeholder.value = std::numeric_limits<float>::quiet_NaN();
            if (name == "flex-basis") c.style->flex_basis = {AETHER_FLEX_BASIS_LENGTH, placeholder};
            else if (name == "padding-top") c.style->padding.top = placeholder;
            else if (name == "padding-right") c.style->padding.right = placeholder;
            else if (name == "padding-bottom") c.style->padding.bottom = placeholder;
            else c.style->padding.left = placeholder;
            return {};
        };
        definition.inherit_context = [name, inherit](property_apply_context_s& c, ComputedStyleReadView p, const MeasuresReadView* pm) {
            if (inherit) inherit(c, p, pm);
            c.measures->set_tree(name, pm ? pm->contextual_tree(name) : aether::css::math_tree{});
        };
    }

    // H8: intrinsic sizes remain owned private tags. A NaN sentinel prevents a
    // context-losing internal route from silently interpreting the tag as zero.
    for (auto& definition : reg->definitions) {
        const auto name = definition.name;
        if (name != "width" && name != "min-width" && name != "max-width" && name != "flex-basis") continue;
        const auto parse = definition.parse;
        definition.parse = [parse](const char* source, std::string& out) {
            return bi_try_intrinsic_keyword(source, out) || (parse && parse(source, out));
        };
        const auto intrinsic_parse = definition.parse;
        const auto flat = definition.apply;
        const auto checked = definition.apply_checked;
        const auto contextual = definition.apply_with_context;
        const auto contextual_checked = definition.apply_with_context_checked;
        const auto inherit = definition.inherit_context;
        definition.apply_checked = [flat, checked, intrinsic_parse](aether_computed_style_t* st, const std::string& value) {
            std::string canonical;
            if (!intrinsic_parse(value.c_str(), canonical)) return property_apply_result_s::failure("AETHER-CSS-INTRINSIC-SYNTAX", "Invalid size syntax.");
            if (bi_intrinsic_keyword(value) != aether::css::intrinsic_size_keyword::none)
                return property_apply_result_s::failure("AETHER-CSS-CONTEXT-REQUIRED", "Intrinsic size requires an owning contextual style.");
            if (checked) return checked(st, value);
            if (flat) flat(st, value);
            return property_apply_result_s{};
        };
        definition.apply = [flat, intrinsic_parse](aether_computed_style_t* st, const std::string& value) {
            std::string canonical;
            if (!intrinsic_parse(value.c_str(), canonical)) throw std::invalid_argument("Invalid size syntax");
            if (bi_intrinsic_keyword(value) != aether::css::intrinsic_size_keyword::none)
                throw std::invalid_argument("Intrinsic size requires contextual style");
            if (flat) flat(st, value);
        };
        definition.apply_with_context_checked = [name, contextual, contextual_checked, intrinsic_parse, checked = definition.apply_checked]
            (property_apply_context_s& c, const std::string& value) -> property_apply_result_s {
            if (!c.measures) return checked(c.style, value);
            std::string canonical;
            if (!intrinsic_parse(value.c_str(), canonical)) return property_apply_result_s::failure("AETHER-CSS-INTRINSIC-SYNTAX", "Invalid size syntax.");
            const auto keyword = bi_intrinsic_keyword(value);
            if (keyword == aether::css::intrinsic_size_keyword::none) {
                auto result = contextual_checked ? contextual_checked(c, value) : property_apply_result_s{};
                if (!contextual_checked && contextual) contextual(c, value);
                if (result.success) c.measures->set_intrinsic_keyword(name, keyword);
                return result;
            }
            c.measures->set_intrinsic_keyword(name, keyword);
            c.measures->set_tree(name, {});
            c.measures->set_property_dependencies(0);
            auto marker = aether_css_length_zero(); marker.value = std::numeric_limits<float>::quiet_NaN();
            if (name == "width") c.style->width = marker;
            else if (name == "min-width") c.style->min_width = marker;
            else if (name == "max-width") c.style->max_width = marker;
            else c.style->flex_basis = {AETHER_FLEX_BASIS_LENGTH, marker};
            return {};
        };
        // All internal callers must receive the same sidecar-safe behavior.
        definition.apply_with_context = [checked = definition.apply_with_context_checked](property_apply_context_s& c, const std::string& value) {
            if (!checked(c, value).success) throw std::invalid_argument("Invalid intrinsic size context");
        };
        definition.inherit_context = [name, inherit](property_apply_context_s& c, ComputedStyleReadView p, const MeasuresReadView* pm) {
            if (inherit) inherit(c, p, pm);
            if (c.measures) c.measures->set_intrinsic_keyword(name, pm ? pm->intrinsic_keyword(name) : aether::css::intrinsic_size_keyword::none);
        };
    }

    transition_properties_register_shorthands(reg);
    paused_animation_register_shorthands(reg);
}
