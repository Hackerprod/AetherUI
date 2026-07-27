#include <aether/aether.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cctype>

AETHER_API aether_css_length aether_css_length_auto(void) {
    aether_css_length l{};
    l.unit = AETHER_CSS_UNIT_AUTO;
    return l;
}

AETHER_API aether_css_length aether_css_length_zero(void) {
    aether_css_length l{};
    l.unit = AETHER_CSS_UNIT_PX;
    l.value = 0;
    return l;
}

AETHER_API aether_bool_t aether_css_length_is_auto(aether_css_length l) {
    return l.unit == AETHER_CSS_UNIT_AUTO ? AETHER_TRUE : AETHER_FALSE;
}

AETHER_API float aether_css_length_resolve(aether_css_length l, float reference,
    float font_size, float root_font_size, float viewport_width, float viewport_height) {
    switch (l.unit) {
        case AETHER_CSS_UNIT_AUTO:    return NAN;
        case AETHER_CSS_UNIT_PX:      return l.value;
        case AETHER_CSS_UNIT_PERCENT: return reference * l.value / 100.0f;
        case AETHER_CSS_UNIT_EM:      return font_size * l.value;
        case AETHER_CSS_UNIT_REM:     return root_font_size * l.value;
        case AETHER_CSS_UNIT_VW:      return viewport_width * l.value / 100.0f;
        case AETHER_CSS_UNIT_VH:      return viewport_height * l.value / 100.0f;
        case AETHER_CSS_UNIT_LINEAR:
            return l.value
                + (reference * l.linear_percent / 100.0f)
                + (font_size * l.linear_em)
                + (root_font_size * l.linear_rem)
                + (viewport_width * l.linear_vw / 100.0f)
                + (viewport_height * l.linear_vh / 100.0f);
    }
    return l.value;
}

AETHER_API aether_bool_t aether_css_length_try_parse(const char* input, aether_css_length* out) {
    if (!input || !out) return AETHER_FALSE;
    *out = aether_css_length_auto();

    const char* s = input;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '\0') return AETHER_FALSE;

    if (s[0] == 'a' && s[1] == 'u' && s[2] == 't' && s[3] == 'o' && s[4] == '\0') {
        *out = aether_css_length_auto();
        return AETHER_TRUE;
    }

    if (s[0] == '0' && s[1] == '\0') {
        *out = aether_css_length_zero();
        return AETHER_TRUE;
    }

    aether_css_unit_t unit = AETHER_CSS_UNIT_PX;
    size_t len = strlen(s);

    if (len >= 3 && s[len - 3] == 'r' && s[len - 2] == 'e' && s[len - 1] == 'm') {
        unit = AETHER_CSS_UNIT_REM;
        len -= 3;
    } else if (len >= 2 && s[len - 2] == 'e' && s[len - 1] == 'm') {
        unit = AETHER_CSS_UNIT_EM;
        len -= 2;
    } else if (len >= 2 && s[len - 2] == 'v' && s[len - 1] == 'w') {
        unit = AETHER_CSS_UNIT_VW;
        len -= 2;
    } else if (len >= 2 && s[len - 2] == 'v' && s[len - 1] == 'h') {
        unit = AETHER_CSS_UNIT_VH;
        len -= 2;
    } else if (len >= 2 && s[len - 2] == 'p' && s[len - 1] == 'x') {
        unit = AETHER_CSS_UNIT_PX;
        len -= 2;
    } else if (len >= 1 && s[len - 1] == '%') {
        unit = AETHER_CSS_UNIT_PERCENT;
        len -= 1;
    }

    char buf[64];
    if (len >= sizeof(buf)) return AETHER_FALSE;
    memcpy(buf, s, len);
    buf[len] = '\0';

    char* end = nullptr;
    float number = strtof(buf, &end);
    if (end == buf || !std::isfinite(number)) return AETHER_FALSE;

    out->value = number;
    out->unit = unit;
    return AETHER_TRUE;
}

AETHER_API aether_css_edges aether_css_edges_zero(void) {
    const auto zero = aether_css_length_zero();
    return {zero, zero, zero, zero};
}
