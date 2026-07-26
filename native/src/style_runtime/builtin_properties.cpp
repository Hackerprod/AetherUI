#include "style_property_registry_internal.h"
#include <cmath>
#include <cstdio>

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
    out = t; return true;
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

static bool bi_try_border_style(const char* src, std::string& out) {
    std::string t = str_trim(src);
    static const char* styles[] = {
        "none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset"
    };
    for (auto& s : styles) { if (str_iequals(t, s)) { out = t; return true; } }
    return false;
}

static bool bi_try_gradient(const char* src, std::string& out) {
    std::string t = str_trim(src);
    if (str_starts_with_i(t, "linear-gradient")) { out = t; return true; }
    return false;
}

static void bi_apply_gradient(aether_computed_style_t* st, const std::string& value) {
    aether_linear_gradient_free(&st->background_gradient);
    st->has_background_gradient = AETHER_FALSE;
    auto open = value.find('(');
    auto close = value.rfind(')');
    if (open == std::string::npos || close <= open) return;
    auto parts = split_top_level(value.substr(open + 1, close - open - 1), ',');
    if (parts.size() < 2) return;
    size_t first = 0;
    auto direction = str_to_lower(str_trim(parts[0]));
    float x = 0, y = 1;
    // CSS angles point clockwise from up: 0deg is up, 90deg is right.
    // Keep this in sync with StyleValueParsers.TryLinearGradient on the
    // managed path so a stylesheet renders identically on either runtime.
    if (direction.size() > 3 && direction.ends_with("deg")) {
        auto degrees_text = direction.substr(0, direction.size() - 3);
        char* end = nullptr;
        const auto degrees = std::strtof(degrees_text.c_str(), &end);
        if (end != degrees_text.c_str() && *end == '\0' && std::isfinite(degrees)) {
            constexpr float pi = 3.14159265358979323846f;
            const auto radians = degrees * pi / 180.0f;
            x = std::sin(radians);
            y = -std::cos(radians);
            first = 1;
        }
    } else if (str_starts_with_i(direction, "to ")) {
        first = 1;
        if (direction.find("right") != std::string::npos) { x = 1; y = 0; }
        else if (direction.find("left") != std::string::npos) { x = -1; y = 0; }
        else if (direction.find("top") != std::string::npos) { x = 0; y = -1; }
    }
    if (parts.size() - first < 2) return;
    auto count = static_cast<int32_t>(parts.size() - first);
    auto* stops = static_cast<aether_gradient_stop*>(std::calloc(count, sizeof(aether_gradient_stop)));
    if (!stops) return;
    for (int32_t i = 0; i < count; ++i) {
        auto token = str_trim(parts[first + i]);
        auto space = token.find(' ');
        auto color = space == std::string::npos ? token : token.substr(0, space);
        if (!aether_color_rgba_try_parse(color.c_str(), &stops[i].color)) {
            std::free(stops); return;
        }
        stops[i].offset = count == 1 ? 0 : static_cast<float>(i) / (count - 1);
    }
    st->background_gradient = {x, y, stops, count};
    st->has_background_gradient = AETHER_TRUE;
}

static void bi_apply_shadow(aether_computed_style_t* st, const std::string& value) {
    st->has_box_shadow = AETHER_FALSE;
    st->box_shadow = {};
    if (str_iequals(value, "none")) return;
    auto tokens = bi_split_ws(value);
    int length_index = 0;
    for (const auto& token : tokens) {
        if (str_iequals(token, "inset")) { st->box_shadow.inset = AETHER_TRUE; continue; }
        aether_color_rgba color;
        if (aether_color_rgba_try_parse(token.c_str(), &color)) { st->box_shadow.color = color; continue; }
        aether_css_length length;
        if (!aether_css_length_try_parse(token.c_str(), &length)) continue;
        auto pixels = aether_css_length_resolve(length, 0, 16, 16, 1920, 1080);
        if (length_index == 0) st->box_shadow.offset_x = pixels;
        else if (length_index == 1) st->box_shadow.offset_y = pixels;
        else if (length_index == 2) st->box_shadow.blur_radius = pixels;
        else if (length_index == 3) st->box_shadow.spread_radius = pixels;
        ++length_index;
    }
    st->has_box_shadow = length_index >= 2 ? AETHER_TRUE : AETHER_FALSE;
}

static bool bi_try_shadow(const char* src, std::string& out) {
    std::string t = str_trim(src);
    if (str_iequals(t, "none")) { out = "none"; return true; }
    out = t; return true;
}

static bool bi_try_transform(const char* src, std::string& out) {
    std::string t = str_trim(src);
    if (str_iequals(t, "none")) { out = "none"; return true; }
    if (str_starts_with_i(t, "matrix") || str_starts_with_i(t, "translate") ||
        str_starts_with_i(t, "rotate")  || str_starts_with_i(t, "scale") ||
        str_starts_with_i(t, "skew")) { out = t; return true; }
    return false;
}

static bool bi_try_transform_origin(const char* src, std::string& out) {
    std::string t = str_trim(src);
    static const char* kw[] = {
        "top left","top center","top right",
        "center left","center center","center right",
        "bottom left","bottom center","bottom right",
        "left","center","right","top","bottom"
    };
    for (auto& k : kw) { if (str_iequals(t, k)) { out = t; return true; } }
    auto parts = bi_split_ws(t);
    if (parts.size() >= 1 && parts.size() <= 2) { out = t; return true; }
    return false;
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
        static const char* kw[] = { "none", "block", "inline", "inline-block", "flex", "line-break" };
        static const int32_t vals[] = {
            AETHER_DISPLAY_NONE, AETHER_DISPLAY_BLOCK, AETHER_DISPLAY_INLINE,
            AETHER_DISPLAY_INLINE_BLOCK, AETHER_DISPLAY_FLEX, AETHER_DISPLAY_LINE_BREAK
        };
        reg->register_property("display", false,
            STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
            "inline",
            [](const char* s, std::string& o) -> bool {
                std::string v;
                if (!bi_try_keyword(s, kw, 6, v)) return false;
                for (int i = 0; i < 6; ++i)
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
                st->overflow = std::atoi(v.c_str());
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
                st->overflow = std::atoi(v.c_str());
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
        [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });

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
        [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });

    // flex-basis
    reg->register_property("flex-basis", false, STYLE_IMPACT_LAYOUT, "auto",
        bi_try_size,
        [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });

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
        reg->register_property("align-self", false, STYLE_IMPACT_LAYOUT, "auto",
            [](const char* s, std::string& o) -> bool { return bi_try_keyword(s, kw, 8, o); },
            [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });
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
            AETHER_TEXT_ALIGN_START,AETHER_TEXT_ALIGN_END,AETHER_TEXT_ALIGN_START,
            AETHER_TEXT_ALIGN_END,AETHER_TEXT_ALIGN_CENTER,AETHER_TEXT_ALIGN_START
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

    // white-space
    {
        static const char* kw[] = { "normal", "nowrap", "pre", "pre-wrap", "pre-line" };
        reg->register_property("white-space", true,
            STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_LAYOUT,
            "normal",
            [](const char* s, std::string& o) -> bool { return bi_try_keyword(s, kw, 5, o); },
            [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });
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

    // background-image
    reg->register_property("background-image", false, STYLE_IMPACT_PAINT,
        "none", bi_try_gradient,
        [](aether_computed_style_t* st, const std::string& v) {
            if (str_iequals(v, "none")) {
                aether_linear_gradient_free(&st->background_gradient);
                st->has_background_gradient = AETHER_FALSE;
            } else {
                bi_apply_gradient(st, v);
            }
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
            if (str_iequals(v, "text") && st->has_background_gradient) {
                aether_linear_gradient_free(&st->text_gradient);
                st->text_gradient = st->background_gradient;
                st->background_gradient = {};
                st->has_text_gradient = AETHER_TRUE;
                st->has_background_gradient = AETHER_FALSE;
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
        auto bwa = [](int side) -> property_apply_fn {
            return [side](aether_computed_style_t* st, const std::string& v) {
                aether_css_length l; aether_css_length_try_parse(v.c_str(), &l);
                switch (side) {
                    case 0: st->border_width.top = l; break;
                    case 1: st->border_width.right = l; break;
                    case 2: st->border_width.bottom = l; break;
                    case 3: st->border_width.left = l; break;
                }
            };
        };
        reg->register_property("border-top-width",    false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", bi_try_border_width, bwa(0));
        reg->register_property("border-right-width",  false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", bi_try_border_width, bwa(1));
        reg->register_property("border-bottom-width", false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", bi_try_border_width, bwa(2));
        reg->register_property("border-left-width",   false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT|STYLE_IMPACT_HITTEST, "0", bi_try_border_width, bwa(3));
    }

    // border-style (top/right/bottom/left)
    {
        auto bsa = [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; };
        reg->register_property("border-top-style",    false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT, "none", bi_try_border_style, bsa);
        reg->register_property("border-right-style",  false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT, "none", bi_try_border_style, bsa);
        reg->register_property("border-bottom-style", false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT, "none", bi_try_border_style, bsa);
        reg->register_property("border-left-style",   false, STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT, "none", bi_try_border_style, bsa);
    }

    // border-color (top/right/bottom/left)
    {
        auto bca = [](aether_computed_style_t* st, const std::string& v) {
            aether_color_rgba c; if (aether_color_rgba_try_parse(v.c_str(), &c)) st->border_color = c;
        };
        reg->register_property("border-top-color",    false, STYLE_IMPACT_PAINT, "currentColor", bi_try_color, bca);
        reg->register_property("border-right-color",  false, STYLE_IMPACT_PAINT, "currentColor", bi_try_color, bca);
        reg->register_property("border-bottom-color", false, STYLE_IMPACT_PAINT, "currentColor", bi_try_color, bca);
        reg->register_property("border-left-color",   false, STYLE_IMPACT_PAINT, "currentColor", bi_try_color, bca);
    }

    // border-radius
    reg->register_property("border-radius", false, STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
        "0", bi_try_size,
        [](aether_computed_style_t* st, const std::string& v) { apply_length_field(&st->border_radius, v); });

    // box-shadow
    reg->register_property("box-shadow", false, STYLE_IMPACT_PAINT, "none",
        bi_try_shadow,
        [](aether_computed_style_t* st, const std::string& v) {
            bi_apply_shadow(st, v);
        });

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
            [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });
    }

    // ── Transforms ─────────────────────────────────────────────

    reg->register_property("transform", false,
        STYLE_IMPACT_COMPOSITE | STYLE_IMPACT_HITTEST, "none", bi_try_transform,
        [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });

    reg->register_property("transform-origin", false,
        STYLE_IMPACT_COMPOSITE | STYLE_IMPACT_HITTEST, "50% 50%", bi_try_transform_origin,
        [](aether_computed_style_t* st, const std::string& v) { (void)st; (void)v; });

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
                    bi_apply_gradient(&temp, g);
                    if (temp.has_background_gradient && temp.background_gradient.stop_count > 0) {
                        const auto& c = temp.background_gradient.stops[0].color;
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
                    aether_linear_gradient_free(&temp.background_gradient);

                    return true;
                }
            }
            std::string c;
            if (bi_try_color(source, c)) {
                writer.set("background-color", c.c_str());
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
            std::string grow = parts[0], shrink = "1", basis = "0px";
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
            if (!bi_try_size(basis.c_str(), basis_test)) return false;
            writer.set("flex-grow", grow.c_str());
            writer.set("flex-shrink", shrink.c_str());
            writer.set("flex-basis", basis.c_str());
            return true;
        });
}
