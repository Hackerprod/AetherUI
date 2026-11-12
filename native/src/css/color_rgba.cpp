#include "../style_runtime/css_component_values.h"
#include "css_numeric_conversion_internal.h"
#include "color_rgba_internal.h"
#include <aether/aether.h>
#include <cstring>
#include <cstdlib>
#include <cmath>

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static aether_bool_t try_parse_hex_component(const char* s, int len, uint8_t* out) {
    if (len == 1) {
        int d = hex_digit(s[0]);
        if (d < 0) return AETHER_FALSE;
        *out = (uint8_t)(d * 17);
        return AETHER_TRUE;
    }
    if (len == 2) {
        int h = hex_digit(s[0]);
        int l = hex_digit(s[1]);
        if (h < 0 || l < 0) return AETHER_FALSE;
        *out = (uint8_t)((h << 4) | l);
        return AETHER_TRUE;
    }
    return AETHER_FALSE;
}

static aether_bool_t try_parse_hex(const char* s, aether_color_rgba* out) {
    uint8_t parts[4] = {0, 0, 0, 255};
    size_t len = strlen(s);

    if (len == 3 || len == 4) {
        for (size_t i = 0; i < len; i++) {
            if (!try_parse_hex_component(s + i, 1, &parts[i]))
                return AETHER_FALSE;
        }
    } else if (len == 6 || len == 8) {
        for (size_t i = 0; i < len / 2; i++) {
            if (!try_parse_hex_component(s + i * 2, 2, &parts[i]))
                return AETHER_FALSE;
        }
    } else {
        return AETHER_FALSE;
    }

    out->r = parts[0] / 255.0f;
    out->g = parts[1] / 255.0f;
    out->b = parts[2] / 255.0f;
    out->a = parts[3] / 255.0f;
    return AETHER_TRUE;
}

namespace {
bool try_rgb_component(const std::string& source, aether::css::c_numeric_locale& locale,
                       bool alpha, bool& percentage, float& value, bool finite_only = false) {
    css_component_cursor cursor(source);
    css_component_value component, extra;
    if (!cursor.next(component) || cursor.next(extra) || !cursor.valid()) return false;
    const auto category = component.token.serialization;
    percentage = category == css_variable_serialization::percentage;
    if (!percentage && category != css_variable_serialization::number) return false;
    std::string number = source.substr(component.begin, component.end - component.begin);
    if (percentage) {
        number.pop_back();
        // Preserve the existing percentage spelling limit (31 numeric bytes).
        if (number.size() >= 32) return false;
    }
    const auto converted = locale.parse_float(number);
    // Preserve finite subnormals and underflow-to-zero, including ERANGE.
    if (!converted.valid || (finite_only && !std::isfinite(converted.value))) return false;
    const float parsed = converted.value;
    const float scale = percentage ? 100.0f : alpha ? 1.0f : 255.0f;
    value = fminf(fmaxf(parsed / scale, 0.0f), 1.0f);
    return true;
}
}

static aether_bool_t try_parse_rgb(const char* s, aether_bool_t has_alpha, aether_color_rgba* out, bool finite_only = false) {
    const std::string body(s, std::strlen(s) - 1); // caller verified final ')'
    const auto parts = css_split_component_delimited(body, ',');
    if (parts.size() != static_cast<size_t>(has_alpha ? 4 : 3)) return AETHER_FALSE;
    aether::css::c_numeric_locale locale;
    float channels[4] = {0, 0, 0, 1};
    bool rgb_percentage = false;
    for (size_t index = 0; index < parts.size(); ++index) {
        bool percentage = false;
        if (!try_rgb_component(parts[index], locale, index == 3, percentage, channels[index], finite_only))
            return AETHER_FALSE;
        if (index == 0) rgb_percentage = percentage;
        else if (index < 3 && percentage != rgb_percentage) return AETHER_FALSE;
    }

    out->r = channels[0];
    out->g = channels[1];
    out->b = channels[2];
    out->a = channels[3];
    return AETHER_TRUE;
}

static aether_color_rgba from_bytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    aether_color_rgba c;
    c.r = r / 255.0f;
    c.g = g / 255.0f;
    c.b = b / 255.0f;
    c.a = a / 255.0f;
    return c;
}

struct named_color_entry { const char* name; aether_color_rgba color; };

// CSS Color 4, 3 November 2026, section 6.1: 148 opaque names plus transparent.
// https://www.w3.org/TR/2026/CRD-css-color-4-20261103/#named-colors
static const named_color_entry named_colors[] = {
    {"aliceblue", from_bytes(240, 248, 255)},
    {"antiquewhite", from_bytes(250, 235, 215)},
    {"aqua", from_bytes(0, 255, 255)},
    {"aquamarine", from_bytes(127, 255, 212)},
    {"azure", from_bytes(240, 255, 255)},
    {"beige", from_bytes(245, 245, 220)},
    {"bisque", from_bytes(255, 228, 196)},
    {"black", from_bytes(0, 0, 0)},
    {"blanchedalmond", from_bytes(255, 235, 205)},
    {"blue", from_bytes(0, 0, 255)},
    {"blueviolet", from_bytes(138, 43, 226)},
    {"brown", from_bytes(165, 42, 42)},
    {"burlywood", from_bytes(222, 184, 135)},
    {"cadetblue", from_bytes(95, 158, 160)},
    {"chartreuse", from_bytes(127, 255, 0)},
    {"chocolate", from_bytes(210, 105, 30)},
    {"coral", from_bytes(255, 127, 80)},
    {"cornflowerblue", from_bytes(100, 149, 237)},
    {"cornsilk", from_bytes(255, 248, 220)},
    {"crimson", from_bytes(220, 20, 60)},
    {"cyan", from_bytes(0, 255, 255)},
    {"darkblue", from_bytes(0, 0, 139)},
    {"darkcyan", from_bytes(0, 139, 139)},
    {"darkgoldenrod", from_bytes(184, 134, 11)},
    {"darkgray", from_bytes(169, 169, 169)},
    {"darkgreen", from_bytes(0, 100, 0)},
    {"darkgrey", from_bytes(169, 169, 169)},
    {"darkkhaki", from_bytes(189, 183, 107)},
    {"darkmagenta", from_bytes(139, 0, 139)},
    {"darkolivegreen", from_bytes(85, 107, 47)},
    {"darkorange", from_bytes(255, 140, 0)},
    {"darkorchid", from_bytes(153, 50, 204)},
    {"darkred", from_bytes(139, 0, 0)},
    {"darksalmon", from_bytes(233, 150, 122)},
    {"darkseagreen", from_bytes(143, 188, 143)},
    {"darkslateblue", from_bytes(72, 61, 139)},
    {"darkslategray", from_bytes(47, 79, 79)},
    {"darkslategrey", from_bytes(47, 79, 79)},
    {"darkturquoise", from_bytes(0, 206, 209)},
    {"darkviolet", from_bytes(148, 0, 211)},
    {"deeppink", from_bytes(255, 20, 147)},
    {"deepskyblue", from_bytes(0, 191, 255)},
    {"dimgray", from_bytes(105, 105, 105)},
    {"dimgrey", from_bytes(105, 105, 105)},
    {"dodgerblue", from_bytes(30, 144, 255)},
    {"firebrick", from_bytes(178, 34, 34)},
    {"floralwhite", from_bytes(255, 250, 240)},
    {"forestgreen", from_bytes(34, 139, 34)},
    {"fuchsia", from_bytes(255, 0, 255)},
    {"gainsboro", from_bytes(220, 220, 220)},
    {"ghostwhite", from_bytes(248, 248, 255)},
    {"gold", from_bytes(255, 215, 0)},
    {"goldenrod", from_bytes(218, 165, 32)},
    {"gray", from_bytes(128, 128, 128)},
    {"green", from_bytes(0, 128, 0)},
    {"greenyellow", from_bytes(173, 255, 47)},
    {"grey", from_bytes(128, 128, 128)},
    {"honeydew", from_bytes(240, 255, 240)},
    {"hotpink", from_bytes(255, 105, 180)},
    {"indianred", from_bytes(205, 92, 92)},
    {"indigo", from_bytes(75, 0, 130)},
    {"ivory", from_bytes(255, 255, 240)},
    {"khaki", from_bytes(240, 230, 140)},
    {"lavender", from_bytes(230, 230, 250)},
    {"lavenderblush", from_bytes(255, 240, 245)},
    {"lawngreen", from_bytes(124, 252, 0)},
    {"lemonchiffon", from_bytes(255, 250, 205)},
    {"lightblue", from_bytes(173, 216, 230)},
    {"lightcoral", from_bytes(240, 128, 128)},
    {"lightcyan", from_bytes(224, 255, 255)},
    {"lightgoldenrodyellow", from_bytes(250, 250, 210)},
    {"lightgray", from_bytes(211, 211, 211)},
    {"lightgreen", from_bytes(144, 238, 144)},
    {"lightgrey", from_bytes(211, 211, 211)},
    {"lightpink", from_bytes(255, 182, 193)},
    {"lightsalmon", from_bytes(255, 160, 122)},
    {"lightseagreen", from_bytes(32, 178, 170)},
    {"lightskyblue", from_bytes(135, 206, 250)},
    {"lightslategray", from_bytes(119, 136, 153)},
    {"lightslategrey", from_bytes(119, 136, 153)},
    {"lightsteelblue", from_bytes(176, 196, 222)},
    {"lightyellow", from_bytes(255, 255, 224)},
    {"lime", from_bytes(0, 255, 0)},
    {"limegreen", from_bytes(50, 205, 50)},
    {"linen", from_bytes(250, 240, 230)},
    {"magenta", from_bytes(255, 0, 255)},
    {"maroon", from_bytes(128, 0, 0)},
    {"mediumaquamarine", from_bytes(102, 205, 170)},
    {"mediumblue", from_bytes(0, 0, 205)},
    {"mediumorchid", from_bytes(186, 85, 211)},
    {"mediumpurple", from_bytes(147, 112, 219)},
    {"mediumseagreen", from_bytes(60, 179, 113)},
    {"mediumslateblue", from_bytes(123, 104, 238)},
    {"mediumspringgreen", from_bytes(0, 250, 154)},
    {"mediumturquoise", from_bytes(72, 209, 204)},
    {"mediumvioletred", from_bytes(199, 21, 133)},
    {"midnightblue", from_bytes(25, 25, 112)},
    {"mintcream", from_bytes(245, 255, 250)},
    {"mistyrose", from_bytes(255, 228, 225)},
    {"moccasin", from_bytes(255, 228, 181)},
    {"navajowhite", from_bytes(255, 222, 173)},
    {"navy", from_bytes(0, 0, 128)},
    {"oldlace", from_bytes(253, 245, 230)},
    {"olive", from_bytes(128, 128, 0)},
    {"olivedrab", from_bytes(107, 142, 35)},
    {"orange", from_bytes(255, 165, 0)},
    {"orangered", from_bytes(255, 69, 0)},
    {"orchid", from_bytes(218, 112, 214)},
    {"palegoldenrod", from_bytes(238, 232, 170)},
    {"palegreen", from_bytes(152, 251, 152)},
    {"paleturquoise", from_bytes(175, 238, 238)},
    {"palevioletred", from_bytes(219, 112, 147)},
    {"papayawhip", from_bytes(255, 239, 213)},
    {"peachpuff", from_bytes(255, 218, 185)},
    {"peru", from_bytes(205, 133, 63)},
    {"pink", from_bytes(255, 192, 203)},
    {"plum", from_bytes(221, 160, 221)},
    {"powderblue", from_bytes(176, 224, 230)},
    {"purple", from_bytes(128, 0, 128)},
    {"rebeccapurple", from_bytes(102, 51, 153)},
    {"red", from_bytes(255, 0, 0)},
    {"rosybrown", from_bytes(188, 143, 143)},
    {"royalblue", from_bytes(65, 105, 225)},
    {"saddlebrown", from_bytes(139, 69, 19)},
    {"salmon", from_bytes(250, 128, 114)},
    {"sandybrown", from_bytes(244, 164, 96)},
    {"seagreen", from_bytes(46, 139, 87)},
    {"seashell", from_bytes(255, 245, 238)},
    {"sienna", from_bytes(160, 82, 45)},
    {"silver", from_bytes(192, 192, 192)},
    {"skyblue", from_bytes(135, 206, 235)},
    {"slateblue", from_bytes(106, 90, 205)},
    {"slategray", from_bytes(112, 128, 144)},
    {"slategrey", from_bytes(112, 128, 144)},
    {"snow", from_bytes(255, 250, 250)},
    {"springgreen", from_bytes(0, 255, 127)},
    {"steelblue", from_bytes(70, 130, 180)},
    {"tan", from_bytes(210, 180, 140)},
    {"teal", from_bytes(0, 128, 128)},
    {"thistle", from_bytes(216, 191, 216)},
    {"tomato", from_bytes(255, 99, 71)},
    {"transparent", {0, 0, 0, 0}},
    {"turquoise", from_bytes(64, 224, 208)},
    {"violet", from_bytes(238, 130, 238)},
    {"wheat", from_bytes(245, 222, 179)},
    {"white", from_bytes(255, 255, 255)},
    {"whitesmoke", from_bytes(245, 245, 245)},
    {"yellow", from_bytes(255, 255, 0)},
    {"yellowgreen", from_bytes(154, 205, 50)},
};
static const int named_color_count = sizeof(named_colors) / sizeof(named_colors[0]);
static_assert(named_color_count == 149, "148 CSS named colors plus transparent");

static void to_lower_inplace(char* s) {
    for (; *s; ++s) {
        if (*s >= 'A' && *s <= 'Z') *s = *s + ('a' - 'A');
    }
}

AETHER_API aether_bool_t aether_color_rgba_try_parse(const char* input, aether_color_rgba* out) {
    if (!input || !out) return AETHER_FALSE;
    *out = from_bytes(0, 0, 0, 0);

    while (*input == ' ' || *input == '\t') ++input;
    if (*input == '\0') return AETHER_FALSE;

    char lower[128];
    size_t len = strlen(input);
    if (len >= sizeof(lower)) return AETHER_FALSE;
    memcpy(lower, input, len + 1);
    to_lower_inplace(lower);

    for (int i = 0; i < named_color_count; i++) {
        if (strcmp(lower, named_colors[i].name) == 0) {
            *out = named_colors[i].color;
            return AETHER_TRUE;
        }
    }

    if (lower[0] == '#') {
        return try_parse_hex(lower + 1, out);
    }

    if (strncmp(lower, "rgba(", 5) == 0 && lower[len - 1] == ')') {
        return try_parse_rgb(lower + 5, AETHER_TRUE, out);
    }
    if (strncmp(lower, "rgb(", 4) == 0 && lower[len - 1] == ')') {
        return try_parse_rgb(lower + 4, AETHER_FALSE, out);
    }

    return AETHER_FALSE;
}

// Narrow internal admission wrapper. It shares the actual legacy component
// parser; errors (including allocation failure) never publish a partial color.
bool aether::css::parse_legacy_rgba_strict(const std::string& source, aether_color_rgba& output) {
    if (source.find('\0') != std::string::npos) return false;
    css_component_cursor cursor(source);
    css_component_value component, extra;
    if (!cursor.next(component) || cursor.next(extra) || !cursor.valid() ||
        component.token.kind != css_variable_token_kind::function) return false;
    std::string spelling = source.substr(component.begin, component.end - component.begin);
    // The engine's public parser accepts literal, case-insensitive names only.
    // Do not admit escaped names it would subsequently ignore.
    if (spelling.size() < 6 || spelling.size() >= 128) return false;
    to_lower_inplace(spelling.data());
    if (spelling.compare(0, 5, "rgba(") != 0 || spelling.back() != ')') return false;
    aether_color_rgba temporary{};
    if (!try_parse_rgb(spelling.c_str() + 5, AETHER_TRUE, &temporary, true)) return false;
    output = temporary;
    return true;
}
