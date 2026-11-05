#include "../style_runtime/css_component_values.h"
#include "transforms.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n\f");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n\f");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool parse_number(const std::string& source, float* out) {
    if (!out) return false;
    const std::string value = trim(source);
    if (value.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(parsed)) return false;
    *out = parsed;
    return true;
}

bool parse_angle(const std::string& source, float* out_radians) {
    if (!out_radians) return false;
    const std::string value = lower(trim(source));
    if (value.empty()) return false;
    if (value == "0" || value == "+0" || value == "-0") {
        *out_radians = 0.0f;
        return true;
    }

    const char* units[] = {"deg", "grad", "rad", "turn"};
    float multiplier[] = {kPi / 180.0f, kPi / 200.0f, 1.0f, 2.0f * kPi};
    for (size_t index = 0; index < std::size(units); ++index) {
        const std::string unit = units[index];
        if (value.size() <= unit.size() || value.compare(value.size() - unit.size(), unit.size(), unit) != 0)
            continue;
        float amount = 0.0f;
        if (!parse_number(value.substr(0, value.size() - unit.size()), &amount)) return false;
        *out_radians = amount * multiplier[index];
        return std::isfinite(*out_radians);
    }
    return false;
}

bool split_components(const std::string& source, bool allow_commas, std::vector<std::string>* out) {
    if (!out) return false;
    out->clear();
    css_component_cursor cursor(source);
    css_component_value component;
    bool after_comma = false;
    while (cursor.next(component)) {
        if (css_component_is_delimiter(component, ',')) {
            if (!allow_commas || out->empty() || after_comma) return false;
            after_comma = true;
        } else {
            out->push_back(source.substr(component.begin, component.end - component.begin));
            after_comma = false;
        }
    }
    return cursor.valid() && !after_comma && !out->empty();
}

bool parse_length(const std::string& source, aether_css_length* out) {
    if (!out) return false;
    return aether_css_length_try_parse(trim(source).c_str(), out) == AETHER_TRUE &&
           out->unit != AETHER_CSS_UNIT_AUTO;
}

aether_css_length percent(float value) {
    aether_css_length result{};
    result.value = value;
    result.unit = AETHER_CSS_UNIT_PERCENT;
    return result;
}

bool parse_transform_function(const std::string& name, const std::vector<std::string>& args,
                              aether_transform_operation* out) {
    if (!out) return false;
    aether_transform_operation result{};
    const std::string function = lower(name);
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
        if (!parse_number(args[0], &first)) return false;
        if (x_only) result.scalar_x = first;
        else if (y_only) result.scalar_y = first;
        else {
            result.scalar_x = first;
            result.scalar_y = args.size() == 2 ? 0.0f : first;
            if (args.size() == 2 && !parse_number(args[1], &result.scalar_y)) return false;
        }
    } else if (function == "rotate" || function == "skewx" || function == "skewy") {
        if (args.size() != 1 || !parse_angle(args[0], &result.angle_radians)) return false;
        result.kind = function == "rotate" ? AETHER_TRANSFORM_ROTATE :
                      function == "skewx" ? AETHER_TRANSFORM_SKEW_X : AETHER_TRANSFORM_SKEW_Y;
    } else if (function == "matrix") {
        if (args.size() != 6) return false;
        result.kind = AETHER_TRANSFORM_MATRIX;
        float* values[] = {&result.matrix_a, &result.matrix_b, &result.matrix_c,
                           &result.matrix_d, &result.matrix_e, &result.matrix_f};
        for (size_t index = 0; index < args.size(); ++index)
            if (!parse_number(args[index], values[index])) return false;
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
    while (cursor.next(component)) {
        if (component.token.kind != css_variable_token_kind::function) return false;
        const auto name = source.substr(component.begin, component.token.end - component.begin - 1);
        std::vector<std::string> args;
        if (!split_components(source.substr(component.token.end, component.end - component.token.end - 1), true, &args)) return false;
        aether_transform_operation operation{};
        if (!parse_transform_function(name, args, &operation)) return false;
        out->push_back(operation);
    }
    return cursor.valid() && !out->empty();
}

bool origin_keyword(const std::string& value, bool* horizontal, aether_css_length* length) {
    if (!horizontal || !length) return false;
    const std::string keyword = lower(value);
    if (keyword == "left") { *horizontal = true; *length = percent(0.0f); return true; }
    if (keyword == "right") { *horizontal = true; *length = percent(100.0f); return true; }
    if (keyword == "top") { *horizontal = false; *length = percent(0.0f); return true; }
    if (keyword == "bottom") { *horizontal = false; *length = percent(100.0f); return true; }
    return false;
}

} // namespace

bool aether_transform_list_try_parse(const char* source, aether_transform_list* out) {
    if (!source || !out) return false;
    *out = {};
    const std::string value = trim(source);
    if (lower(value) == "none") return true;
    std::vector<aether_transform_operation> parsed;
    if (!parse_transform_list(value, &parsed) || parsed.size() > static_cast<size_t>((std::numeric_limits<int32_t>::max)()))
        return false;
    const size_t bytes = sizeof(aether_transform_operation) * parsed.size();
    auto* items = static_cast<aether_transform_operation*>(std::malloc(bytes));
    if (!items) return false;
    std::memcpy(items, parsed.data(), bytes);
    *out = {items, static_cast<int32_t>(parsed.size())};
    return true;
}

bool aether_transform_origin_try_parse(const char* source, aether_transform_origin* out) {
    if (!source || !out) return false;
    std::vector<std::string> parts;
    if (!split_components(trim(source), false, &parts) || parts.size() > 2) return false;
    aether_transform_origin result{percent(50.0f), percent(50.0f)};
    bool has_x = false;
    bool has_y = false;
    for (const auto& part : parts) {
        if (lower(part) == "center") {
            if (!has_x) { result.x = percent(50.0f); has_x = true; }
            else if (!has_y) { result.y = percent(50.0f); has_y = true; }
            else return false;
            continue;
        }
        bool horizontal = false;
        aether_css_length keyword{};
        if (origin_keyword(part, &horizontal, &keyword)) {
            if (horizontal) {
                if (has_x) return false;
                result.x = keyword; has_x = true;
            } else {
                if (has_y) return false;
                result.y = keyword; has_y = true;
            }
            continue;
        }
        aether_css_length length{};
        if (!parse_length(part, &length)) return false;
        if (!has_x) { result.x = length; has_x = true; }
        else if (!has_y) { result.y = length; has_y = true; }
        else return false;
    }
    *out = result;
    return true;
}
