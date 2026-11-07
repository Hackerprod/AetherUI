#include <new>
#include "../style_runtime/css_component_values.h"
#include "transforms.h"
#include "css_length_internal.h"
#include "css_numeric_conversion_internal.h"
#include "../primitives/ascii_text.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool decode_identifier(const std::string& spelling, std::string& output) {
    css_variable_lexer lexer(spelling.data(), spelling.size());
    return lexer.decode_identifier([](void* data, const char* bytes, size_t size) {
        static_cast<std::string*>(data)->append(bytes, size); return true;
    }, &output);
}

bool single_token(const std::string& source, css_variable_serialization category) {
    css_variable_lexer lexer(source.data(), source.size());
    const auto token = lexer.next();
    return token.serialization == category && token.begin == 0 && token.end == source.size();
}

bool parse_number(const std::string& source, aether::css::c_numeric_locale& locale, float* out) {
    // C conversion alone accepts non-CSS spellings such as hexadecimal floats.
    // Retain the existing float/ERANGE policy, including subnormal underflow.
    if (!single_token(source, css_variable_serialization::number)) return false;
    const auto parsed = locale.parse_float(source);
    if (!parsed.valid || parsed.range_error) return false;
    *out = parsed.value;
    return true;
}

bool parse_angle(const std::string& source, aether::css::c_numeric_locale& locale, float* out_radians) {
    if (single_token(source, css_variable_serialization::number)) {
        float value = 0;
        if (!parse_number(source, locale, &value) || value != 0) return false;
        *out_radians = value;
        return true;
    }
    css_variable_lexer lexer(source.data(), source.size());
    const auto token = lexer.next();
    if (token.serialization != css_variable_serialization::dimension || token.end != source.size()) return false;
    std::string decoded_unit;
    if (!decode_identifier(source.substr(token.numeric_end), decoded_unit)) return false;
    const char* units[] = {"deg", "grad", "rad", "turn"};
    const float multiplier[] = {kPi / 180.0f, kPi / 200.0f, 1.0f, 2.0f * kPi};
    for (size_t index = 0; index < std::size(units); ++index) {
        if (!aether::ascii_iequals(decoded_unit, units[index])) continue;
        float amount = 0.0f;
        if (!parse_number(source.substr(0, token.numeric_end), locale, &amount)) return false;
        const float radians = amount * multiplier[index];
        if (!std::isfinite(radians)) return false;
        *out_radians = radians;
        return true;
    }
    return false;
}

bool split_arguments(const std::string& source, std::vector<std::string>& out) {
    css_component_cursor cursor(source);
    css_component_value component;
    bool expect_argument = true;
    while (cursor.next(component)) {
        if (css_component_is_delimiter(component, ',')) {
            if (expect_argument) return false;
        } else {
            if (!expect_argument) return false;
            out.push_back(source.substr(component.begin, component.end - component.begin));
        }
        expect_argument = !expect_argument;
    }
    return cursor.valid() && !expect_argument;
}

bool normalize_length_spelling(const std::string& source, std::string& output) {
    css_variable_lexer lexer(source.data(), source.size());
    size_t copied = 0;
    for (;;) {
        const auto token = lexer.next();
        if (token.kind == css_variable_token_kind::error) return false;
        if (token.kind == css_variable_token_kind::end) break;
        if (token.kind == css_variable_token_kind::function) {
            std::string function;
            if (!decode_identifier(source.substr(token.begin, token.end - token.begin - 1), function) ||
                !aether::ascii_iequals(function, "calc")) return false;
            // Only an existing Function token may become calc(. An escaped
            // parenthesis, intervening comment or space must not create one.
            output.append(source, copied, token.begin - copied);
            output += "calc(";
            copied = token.end;
            continue;
        }
        if (token.serialization != css_variable_serialization::dimension) continue;
        std::string unit;
        if (!decode_identifier(source.substr(token.numeric_end, token.end - token.numeric_end), unit)) return false;
        unit = aether::ascii_lowercase(unit);
        if (unit != "px" && unit != "em" && unit != "rem" && unit != "vw" && unit != "vh") return false;
        // Only known units are canonicalized. Decoding an arbitrary unit could
        // change token boundaries (e.g. an escaped e2px into a numeric exponent).
        // Preserve all other bytes, including calc whitespace and comments. An
        // escape's terminator belongs to its token and supplies no whitespace.
        output.append(source, copied, token.numeric_end - copied);
        output += unit;
        copied = token.end;
    }
    output.append(source, copied, source.size() - copied);
    return true;
}

bool parse_length(const std::string& source, aether_css_length* out) {
    std::string normalized;
    if (!normalize_length_spelling(source, normalized)) return false;
    aether::css::calc_value_s parsed;
    if (aether::css::try_parse_calc(normalized, parsed)) {
        if (parsed.kind != aether::css::calc_value_kind_e::length) return false;
    } else {
        if (!aether::css::try_parse_numeric_literal(normalized, parsed)) return false;
        // Unlike the public compatibility helper, transforms only coerce a
        // literal Number zero to Length. calc(0) remains Number and is invalid.
        if (parsed.kind == aether::css::calc_value_kind_e::number) {
            if (parsed.number != 0.0) return false;
            parsed.length = aether_css_length_zero();
        }
    }
    *out = parsed.length;
    return true;
}

aether_css_length percent(float value) {
    aether_css_length result{};
    result.value = value;
    result.unit = AETHER_CSS_UNIT_PERCENT;
    return result;
}

bool parse_transform_function(const std::string& name, const std::vector<std::string>& args,
                              aether::css::c_numeric_locale& locale, aether_transform_operation* out) {
    if (!out) return false;
    aether_transform_operation result{};
    const std::string function = aether::ascii_lowercase(name);
    if (function == "translate" || function == "translatex" || function == "translatey") {
        const bool x_only = function == "translatex";
        const bool y_only = function == "translatey";
        if ((x_only || y_only) ? args.size() != 1 : (args.empty() || args.size() > 2)) return false;
        result.kind = AETHER_TRANSFORM_TRANSLATE;
        result.x = aether_css_length_zero();
        result.y = aether_css_length_zero();
        if (x_only) return parse_length(args[0], &result.x) && ((*out = result), true);
        if (y_only) return parse_length(args[0], &result.y) && ((*out = result), true);
        if (!parse_length(args[0], &result.x)) return false;
        if (args.size() == 2 && !parse_length(args[1], &result.y)) return false;
    } else if (function == "scale" || function == "scalex" || function == "scaley") {
        const bool x_only = function == "scalex";
        const bool y_only = function == "scaley";
        if ((x_only || y_only) ? args.size() != 1 : (args.empty() || args.size() > 2)) return false;
        result.kind = AETHER_TRANSFORM_SCALE;
        result.scalar_x = 1.0f;
        result.scalar_y = 1.0f;
        float first = 0.0f;
        if (!parse_number(args[0], locale, &first)) return false;
        if (x_only) result.scalar_x = first;
        else if (y_only) result.scalar_y = first;
        else {
            result.scalar_x = first;
            result.scalar_y = args.size() == 2 ? 0.0f : first;
            if (args.size() == 2 && !parse_number(args[1], locale, &result.scalar_y)) return false;
        }
    } else if (function == "rotate" || function == "skewx" || function == "skewy") {
        if (args.size() != 1 || !parse_angle(args[0], locale, &result.angle_radians)) return false;
        result.kind = function == "rotate" ? AETHER_TRANSFORM_ROTATE :
                      function == "skewx" ? AETHER_TRANSFORM_SKEW_X : AETHER_TRANSFORM_SKEW_Y;
    } else if (function == "matrix") {
        if (args.size() != 6) return false;
        result.kind = AETHER_TRANSFORM_MATRIX;
        float* values[] = {&result.matrix_a, &result.matrix_b, &result.matrix_c,
                           &result.matrix_d, &result.matrix_e, &result.matrix_f};
        for (size_t index = 0; index < args.size(); ++index)
            if (!parse_number(args[index], locale, values[index])) return false;
    } else {
        return false;
    }
    *out = result;
    return true;
}

bool parse_transform_list(const std::string& source, std::vector<aether_transform_operation>* out) {
    if (!out) return false;
    out->clear();
    css_component_cursor cursor(source);
    css_component_value component;
    aether::css::c_numeric_locale locale;
    while (cursor.next(component)) {
        if (component.token.kind == css_variable_token_kind::ident && out->empty()) {
            std::string keyword;
            if (!decode_identifier(source.substr(component.begin, component.end - component.begin), keyword) ||
                !aether::ascii_iequals(keyword, "none")) return false;
            return !cursor.next(component) && cursor.valid();
        }
        if (component.token.kind != css_variable_token_kind::function) return false;
        std::string name;
        if (!decode_identifier(source.substr(component.begin, component.token.end - component.begin - 1), name)) return false;
        std::vector<std::string> args;
        if (!split_arguments(source.substr(component.token.end, component.end - component.token.end - 1), args)) return false;
        aether_transform_operation operation{};
        if (!parse_transform_function(name, args, locale, &operation)) return false;
        out->push_back(operation);
    }
    return cursor.valid() && !out->empty();
}

enum class origin_component_kind { horizontal, vertical, center, length };
struct origin_component {
    origin_component_kind kind = origin_component_kind::length;
    aether_css_length length{};
};

bool parse_origin_component(const std::string& source, const css_component_value& component,
                            origin_component& out) {
    const std::string spelling = source.substr(component.begin, component.end - component.begin);
    if (component.token.kind == css_variable_token_kind::ident) {
        std::string keyword;
        css_variable_lexer lexer(spelling.data(), spelling.size());
        if (!lexer.decode_identifier([](void* data, const char* bytes, size_t size) {
                static_cast<std::string*>(data)->append(bytes, size); return true;
            }, &keyword)) return false;
        if (aether::ascii_iequals(keyword, "left")) out = {origin_component_kind::horizontal, percent(0)};
        else if (aether::ascii_iequals(keyword, "right")) out = {origin_component_kind::horizontal, percent(100)};
        else if (aether::ascii_iequals(keyword, "top")) out = {origin_component_kind::vertical, percent(0)};
        else if (aether::ascii_iequals(keyword, "bottom")) out = {origin_component_kind::vertical, percent(100)};
        else if (aether::ascii_iequals(keyword, "center")) out = {origin_component_kind::center, percent(50)};
        else return false;
        return true;
    }
    aether::css::calc_value_s number;
    if (component.token.kind == css_variable_token_kind::function) {
        if (!aether::css::try_parse_calc(spelling, number) ||
            number.kind != aether::css::calc_value_kind_e::length) return false;
    } else {
        if (!aether::css::try_parse_numeric_literal(spelling, number)) return false;
        // Only a literal Number zero may become a length. calc(0) remains Number.
        if (number.kind == aether::css::calc_value_kind_e::number) {
            if (number.number != 0.0) return false;
            number.length = aether_css_length_zero();
        }
    }
    out = {origin_component_kind::length, number.length};
    return true;
}

} // namespace

bool aether_transform_list_try_parse(const char* source, aether_transform_list* out) {
    if (!source || !out) return false;
    *out = {};
    const std::string value(source);
    std::vector<aether_transform_operation> parsed;
    if (!parse_transform_list(value, &parsed) || parsed.size() > static_cast<size_t>((std::numeric_limits<int32_t>::max)()))
        return false;
    if (parsed.empty()) return true;
    const size_t bytes = sizeof(aether_transform_operation) * parsed.size();
    auto* items = static_cast<aether_transform_operation*>(std::malloc(bytes));
    if (!items) throw std::bad_alloc();
    std::memcpy(items, parsed.data(), bytes);
    *out = {items, static_cast<int32_t>(parsed.size())};
    return true;
}

bool aether_transform_origin_try_parse(const char* source, aether_transform_origin* out) {
    if (!source || !out) return false;
    const std::string value(source);
    css_component_cursor cursor(value);
    css_component_value component;
    origin_component parts[2];
    size_t count = 0;
    while (cursor.next(component)) {
        if (count == 2 || !parse_origin_component(value, component, parts[count])) return false;
        ++count;
    }
    if (!cursor.valid() || count == 0) return false;
    aether_transform_origin result{percent(50), percent(50)};
    using kind = origin_component_kind;
    if (count == 1) {
        if (parts[0].kind == kind::vertical) result.y = parts[0].length;
        else result.x = parts[0].length;
    } else if (parts[0].kind != kind::vertical && parts[1].kind != kind::horizontal) {
        // Ordered (H|C|L) (V|C|L).
        result = {parts[0].length, parts[1].length};
    } else if ((parts[0].kind == kind::vertical || parts[0].kind == kind::center) &&
               (parts[1].kind == kind::horizontal || parts[1].kind == kind::center)) {
        // Only keywords may reverse axes: (V|C) (H|C).
        result = {parts[1].length, parts[0].length};
    } else return false;
    *out = result;
    return true;
}
