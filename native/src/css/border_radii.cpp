#include "../style_runtime/css_component_values.h"
#include "border_radii.h"
#include "css_types.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

static std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

static std::vector<std::string> words(const std::string& value) { return css_split_components(value); }

static bool parse_component(const std::vector<std::string>& values, aether_css_length output[4]) {
    if (values.empty() || values.size() > 4) return false;
    aether_css_length parsed[4];
    for (size_t i = 0; i < values.size(); ++i)
        if (!aether_css_length_try_parse(values[i].c_str(), &parsed[i])) return false;
    output[0] = parsed[0];
    output[1] = values.size() == 1 ? parsed[0] : parsed[1];
    output[2] = values.size() <= 2 ? parsed[0] : parsed[2];
    output[3] = values.size() == 1 ? parsed[0] : values.size() == 2 ? parsed[1] : values.size() == 3 ? parsed[1] : parsed[3];
    return true;
}

bool aether_border_radii_try_parse(const std::string& source, aether_border_radii* out) {
    if (!out) return false;
    const auto sections = css_split_component_delimited(source, '/');
    if (sections.empty() || sections.size() > 2) return false;
    const auto slash = css_find_component_delimiter(source, '/');
    aether_css_length horizontal[4], vertical[4];
    if (!parse_component(words(trim(source.substr(0, slash))), horizontal)) return false;
    if (slash < 0) std::copy(std::begin(horizontal), std::end(horizontal), std::begin(vertical));
    else if (!parse_component(words(trim(source.substr(slash + 1))), vertical)) return false;
    *out = {{horizontal[0], vertical[0]}, {horizontal[1], vertical[1]},
            {horizontal[2], vertical[2]}, {horizontal[3], vertical[3]}};
    return true;
}

static float resolve(aether_css_length value, float reference, float font_size, float root_font_size, float vw, float vh) {
    const float result = aether_css_length_resolve(value, reference, font_size, root_font_size, vw, vh);
    return std::isfinite(result) ? std::max(0.f, result) : 0.f;
}

aether_corner_radii aether_border_radii_resolve(const aether_border_radii& value,
                                                aether_rectf box, float font_size, float root_font_size, float vw, float vh) {
    if (!std::isfinite(box.width) || !std::isfinite(box.height) || box.width < 0.f || box.height < 0.f)
        return {};
    aether_corner_radii result{
        resolve(value.top_left.horizontal, box.width, font_size, root_font_size, vw, vh), resolve(value.top_left.vertical, box.height, font_size, root_font_size, vw, vh),
        resolve(value.top_right.horizontal, box.width, font_size, root_font_size, vw, vh), resolve(value.top_right.vertical, box.height, font_size, root_font_size, vw, vh),
        resolve(value.bottom_right.horizontal, box.width, font_size, root_font_size, vw, vh), resolve(value.bottom_right.vertical, box.height, font_size, root_font_size, vw, vh),
        resolve(value.bottom_left.horizontal, box.width, font_size, root_font_size, vw, vh), resolve(value.bottom_left.vertical, box.height, font_size, root_font_size, vw, vh)};
    // Keep the common scale in double through the final products: a finite
    // pair can overflow float, and a useful scale can be smaller than float.
    double factor = 1.0;
    const auto constrain = [&factor](double first, double second, double extent) {
        const double sum = first + second;
        if (sum > extent) factor = std::min(factor, extent / sum);
    };
    constrain(result.top_left_x, result.top_right_x, box.width);
    constrain(result.bottom_left_x, result.bottom_right_x, box.width);
    constrain(result.top_left_y, result.bottom_left_y, box.height);
    constrain(result.top_right_y, result.bottom_right_y, box.height);
    if (factor == 1.0) return result;
    result.top_left_x = static_cast<float>(static_cast<double>(result.top_left_x) * factor);
    result.top_left_y = static_cast<float>(static_cast<double>(result.top_left_y) * factor);
    result.top_right_x = static_cast<float>(static_cast<double>(result.top_right_x) * factor);
    result.top_right_y = static_cast<float>(static_cast<double>(result.top_right_y) * factor);
    result.bottom_right_x = static_cast<float>(static_cast<double>(result.bottom_right_x) * factor);
    result.bottom_right_y = static_cast<float>(static_cast<double>(result.bottom_right_y) * factor);
    result.bottom_left_x = static_cast<float>(static_cast<double>(result.bottom_left_x) * factor);
    result.bottom_left_y = static_cast<float>(static_cast<double>(result.bottom_left_y) * factor);
    return result;
}

aether_corner_radii aether_corner_radii_inset(aether_corner_radii r, float left, float top, float right, float bottom) {
    r.top_left_x = std::max(0.f, r.top_left_x - left); r.top_left_y = std::max(0.f, r.top_left_y - top);
    r.top_right_x = std::max(0.f, r.top_right_x - right); r.top_right_y = std::max(0.f, r.top_right_y - top);
    r.bottom_right_x = std::max(0.f, r.bottom_right_x - right); r.bottom_right_y = std::max(0.f, r.bottom_right_y - bottom);
    r.bottom_left_x = std::max(0.f, r.bottom_left_x - left); r.bottom_left_y = std::max(0.f, r.bottom_left_y - bottom);
    return r;
}

bool aether_corner_radii_is_zero(aether_corner_radii r) {
    return r.top_left_x <= .5f && r.top_left_y <= .5f && r.top_right_x <= .5f && r.top_right_y <= .5f &&
           r.bottom_right_x <= .5f && r.bottom_right_y <= .5f && r.bottom_left_x <= .5f && r.bottom_left_y <= .5f;
}

aether_corner_radii aether_border_radii_resolve(const aether_border_radii& value, aether_rectf box, float font_size) {
    return aether_border_radii_resolve(value, box, font_size, font_size, 0.f, 0.f);
}
