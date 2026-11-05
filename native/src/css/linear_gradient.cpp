#include "../style_runtime/css_component_values.h"
#include "linear_gradient.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lowercase(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
    return value;
}

std::vector<std::string> split_top_level(const std::string& source) {
    auto parts = css_split_component_delimited(source, ',');
    for (auto& part : parts) part = trim(part);
    return parts;
}

enum class direction_result { not_a_direction, valid, invalid };

direction_result try_parse_direction(const std::string& source, float& x, float& y,
                                     bool& uses_fixed_corner_direction) {
    const auto components = css_split_components(source);
    std::string direction;
    for (const auto& component : components) {
        if (!direction.empty()) direction += ' ';
        direction += lowercase(component);
    }
    if (direction.size() > 3 && direction.ends_with("deg")) {
        const auto degrees_text = direction.substr(0, direction.size() - 3);
        char* end = nullptr;
        const float degrees = std::strtof(degrees_text.c_str(), &end);
        if (end == degrees_text.c_str() || *end != '\0' || !std::isfinite(degrees)) return direction_result::invalid;
        constexpr float pi = 3.14159265358979323846f;
        const float radians = degrees * pi / 180.f;
        x = std::sin(radians);
        y = -std::cos(radians);
        return direction_result::valid;
    }

    if (!direction.starts_with("to ")) return direction_result::not_a_direction;
    const auto words = split_top_level(direction.substr(3));
    // split_top_level only splits commas. Direction keywords use whitespace.
    const auto has = [&](const char* word) {
        const std::string needle = word;
        const auto padded = " " + trim(direction.substr(3)) + " ";
        return padded.find(" " + needle + " ") != std::string::npos;
    };
    (void)words;
    const bool left = has("left");
    const bool right = has("right");
    const bool top = has("top");
    const bool bottom = has("bottom");
    if ((left == right && (left || right)) || (top == bottom && (top || bottom))) return direction_result::invalid;
    if (!left && !right && !top && !bottom) return direction_result::invalid;

    const auto compact = trim(direction.substr(3));
    std::string rebuilt;
    bool previous_space = true;
    for (char c : compact) {
        const bool space = c == ' ' || c == '\t' || c == '\r' || c == '\n';
        if (space) {
            if (!previous_space) rebuilt += ' ';
        } else {
            rebuilt += c;
        }
        previous_space = space;
    }
    if (!rebuilt.empty() && rebuilt.back() == ' ') rebuilt.pop_back();
    const bool valid_words = rebuilt == "left" || rebuilt == "right" || rebuilt == "top" || rebuilt == "bottom" ||
        rebuilt == "left top" || rebuilt == "left bottom" || rebuilt == "right top" || rebuilt == "right bottom" ||
        rebuilt == "top left" || rebuilt == "top right" || rebuilt == "bottom left" || rebuilt == "bottom right";
    if (!valid_words) return direction_result::invalid;

    x = right ? 1.f : left ? -1.f : 0.f;
    y = bottom ? 1.f : top ? -1.f : 0.f;
    if (x != 0.f && y != 0.f) {
        constexpr float inverse_sqrt_two = 0.7071067811865475244f;
        x *= inverse_sqrt_two;
        y *= inverse_sqrt_two;
        uses_fixed_corner_direction = true;
    }
    return direction_result::valid;
}

bool parse_stop(const std::string& source, aether_gradient_stop& stop) {
    const auto components = css_split_components(source);
    if (components.empty() || components.size() > 2) return false;
    if (!aether_color_rgba_try_parse(components[0].c_str(), &stop.color)) return false;
    const auto offset_text = components.size() == 2 ? components[1] : std::string{};
    if (offset_text.empty()) {
        stop.position = aether_css_length_zero();
        stop.offset = 0.f;
        stop.has_position = AETHER_FALSE;
        return true;
    }
    aether_css_length length{};
    if (!aether_css_length_try_parse(offset_text.c_str(), &length) ||
        aether_css_length_is_auto(length)) return false;
    stop.position = length;
    stop.offset = 0.f;
    stop.has_position = AETHER_TRUE;
    return true;
}

void normalize_offsets(std::vector<aether_gradient_stop>& stops) {
    if (!stops.front().has_position) stops.front().offset = 0.f;
    if (!stops.back().has_position) stops.back().offset = 1.f;

    // CSS Images: color-stop positions never move backward. A descending explicit
    // position becomes a hard transition at the preceding used position.
    float previous = stops.front().offset;
    for (size_t index = 1; index < stops.size(); ++index) {
        const bool endpoint_default = index + 1 == stops.size() && !stops[index].has_position;
        if (!stops[index].has_position && !endpoint_default) continue;
        stops[index].offset = std::max(stops[index].offset, previous);
        previous = stops[index].offset;
    }

    for (size_t index = 1; index + 1 < stops.size();) {
        if (stops[index].has_position) { ++index; continue; }
        const size_t start = index - 1;
        while (index + 1 < stops.size() && !stops[index].has_position) ++index;
        const size_t end = index;
        const float from = stops[start].offset;
        const float to = stops[end].offset;
        for (size_t missing = start + 1; missing < end; ++missing) {
            stops[missing].offset = from + (to - from) *
                static_cast<float>(missing - start) / static_cast<float>(end - start);
        }
    }
}

} // namespace

bool aether_linear_gradient_try_parse(const char* source, aether_linear_gradient* out_gradient,
                                      aether_linear_gradient_parse_info_s* out_info) {
    if (!source || !out_gradient) return false;
    *out_gradient = {};
    if (out_info) *out_info = {};
    const std::string value = trim(source);
    const auto open = value.find('(');
    const auto close = value.rfind(')');
    if (open == std::string::npos || close <= open || lowercase(trim(value.substr(0, open))) != "linear-gradient") return false;

    const auto parts = split_top_level(value.substr(open + 1, close - open - 1));
    if (parts.size() < 2) return false;

    float x = 0.f;
    float y = 1.f;
    size_t first_stop = 0;
    bool uses_fixed_corner_direction = false;
    const auto direction = try_parse_direction(parts.front(), x, y, uses_fixed_corner_direction);
    if (direction == direction_result::invalid) return false;
    if (direction == direction_result::valid) first_stop = 1;
    if (parts.size() - first_stop < 2) return false;

    std::vector<aether_gradient_stop> stops(parts.size() - first_stop);
    for (size_t index = 0; index < stops.size(); ++index) {
        if (!parse_stop(parts[first_stop + index], stops[index])) return false;
    }

    auto* owned_stops = static_cast<aether_gradient_stop*>(std::malloc(sizeof(aether_gradient_stop) * stops.size()));
    if (!owned_stops) return false;
    std::memcpy(owned_stops, stops.data(), sizeof(aether_gradient_stop) * stops.size());
    *out_gradient = {x, y, owned_stops, static_cast<int32_t>(stops.size())};
    if (out_info) out_info->uses_fixed_corner_direction = uses_fixed_corner_direction;
    return true;
}

bool aether_linear_gradient_resolve(const aether_linear_gradient& source,
                                    const aether_linear_gradient_resolution_context_s& context,
                                    aether_linear_gradient* out_gradient) {
    if (!out_gradient || source.stop_count <= 0 || !source.stops ||
        context.gradient_rect.width < 0.f || context.gradient_rect.height < 0.f) return false;
    *out_gradient = {};

    const float line_length = std::abs(context.gradient_rect.width * source.direction_x) +
                              std::abs(context.gradient_rect.height * source.direction_y);
    if (!std::isfinite(line_length) || line_length <= 0.f) return false;

    std::vector<aether_gradient_stop> stops(source.stops, source.stops + source.stop_count);
    for (auto& stop : stops) {
        if (!stop.has_position) continue;
        const float length = aether_css_length_resolve(stop.position, line_length,
            context.font_size, context.root_font_size,
            context.viewport_width, context.viewport_height);
        if (!std::isfinite(length)) return false;
        stop.offset = std::clamp(length / line_length, 0.f, 1.f);
    }
    normalize_offsets(stops);

    const size_t bytes = sizeof(aether_gradient_stop) * stops.size();
    auto* owned_stops = static_cast<aether_gradient_stop*>(std::malloc(bytes));
    if (!owned_stops) return false;
    std::memcpy(owned_stops, stops.data(), bytes);
    *out_gradient = {source.direction_x, source.direction_y, owned_stops, source.stop_count};
    return true;
}
