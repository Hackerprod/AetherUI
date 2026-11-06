#include "../style_runtime/css_component_values.h"
#include <aether/aether.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <locale.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif

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
// Own one C numeric locale for this parse. Never change the process/thread
// locale, fall back to it on allocation failure, or release a shared handle.
class rgb_numeric_locale {
public:
#if defined(_WIN32)
    rgb_numeric_locale() : locale_(_create_locale(LC_NUMERIC, "C")) {}
    ~rgb_numeric_locale() { if (locale_) _free_locale(locale_); }
#else
    rgb_numeric_locale() : locale_(newlocale(LC_NUMERIC_MASK, "C", nullptr)) {}
    ~rgb_numeric_locale() { if (locale_) freelocale(locale_); }
#endif
    rgb_numeric_locale(const rgb_numeric_locale&) = delete;
    rgb_numeric_locale& operator=(const rgb_numeric_locale&) = delete;

    bool parse(const std::string& text, float& value) const {
        if (!locale_) return false;
        char* end = nullptr;
#if defined(_WIN32)
        const float parsed = _strtof_l(text.c_str(), &end, locale_);
#else
        const float parsed = strtof_l(text.c_str(), &end, locale_);
#endif
        // ERANGE alone is not a failure: finite subnormals and underflow to
        // zero retain the existing float contract. Overflow is nonfinite.
        if (end == text.c_str() || end != text.c_str() + text.size() ||
            !std::isfinite(parsed)) return false;
        value = parsed;
        return true;
    }
private:
#if defined(_WIN32)
    _locale_t locale_;
#else
    locale_t locale_;
#endif
};

bool try_rgb_component(const std::string& source, const rgb_numeric_locale& locale,
                       bool alpha, bool& percentage, float& value) {
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
    float parsed = 0;
    if (!locale.parse(number, parsed)) return false;
    const float scale = percentage ? 100.0f : alpha ? 1.0f : 255.0f;
    value = fminf(fmaxf(parsed / scale, 0.0f), 1.0f);
    return true;
}
}

static aether_bool_t try_parse_rgb(const char* s, aether_bool_t has_alpha, aether_color_rgba* out) {
    const std::string body(s, std::strlen(s) - 1); // caller verified final ')'
    const auto parts = css_split_component_delimited(body, ',');
    if (parts.size() != static_cast<size_t>(has_alpha ? 4 : 3)) return AETHER_FALSE;
    const rgb_numeric_locale locale;
    float channels[4] = {0, 0, 0, 1};
    bool rgb_percentage = false;
    for (size_t index = 0; index < parts.size(); ++index) {
        bool percentage = false;
        if (!try_rgb_component(parts[index], locale, index == 3, percentage, channels[index]))
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

static const named_color_entry named_colors[] = {
    {"transparent",  {0, 0, 0, 0}},
    {"black",        from_bytes(0, 0, 0)},
    {"white",        from_bytes(255, 255, 255)},
    {"red",          from_bytes(255, 0, 0)},
    {"green",        from_bytes(0, 128, 0)},
    {"blue",         from_bytes(0, 0, 255)},
    {"gray",         from_bytes(128, 128, 128)},
    {"grey",         from_bytes(128, 128, 128)},
    {"silver",       from_bytes(192, 192, 192)},
    {"navy",         from_bytes(0, 0, 128)},
    {"teal",         from_bytes(0, 128, 128)},
    {"purple",       from_bytes(128, 0, 128)},
    {"orange",       from_bytes(255, 165, 0)},
    {"yellow",       from_bytes(255, 255, 0)},
    {"whitesmoke",   from_bytes(245, 245, 245)},
    {"aliceblue",    from_bytes(240, 248, 255)},
};
static const int named_color_count = sizeof(named_colors) / sizeof(named_colors[0]);

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
