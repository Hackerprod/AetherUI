#include "css_length_internal.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <string>
#include <string_view>

namespace {

using aether::css::calc_value_kind_e;
using aether::css::calc_value_s;

static bool is_ascii_alpha(char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

static bool ascii_iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        const auto lower = [](char value) {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
        };
        if (lower(left[i]) != lower(right[i])) return false;
    }
    return true;
}

static aether_css_length calc_linear_length(aether_css_length value) {
    if (value.unit == AETHER_CSS_UNIT_LINEAR) return value;

    aether_css_length linear = aether_css_length_zero();
    linear.unit = AETHER_CSS_UNIT_LINEAR;
    switch (value.unit) {
        case AETHER_CSS_UNIT_PX:      linear.value = value.value; break;
        case AETHER_CSS_UNIT_PERCENT: linear.linear_percent = value.value; break;
        case AETHER_CSS_UNIT_EM:      linear.linear_em = value.value; break;
        case AETHER_CSS_UNIT_REM:     linear.linear_rem = value.value; break;
        case AETHER_CSS_UNIT_VW:      linear.linear_vw = value.value; break;
        case AETHER_CSS_UNIT_VH:      linear.linear_vh = value.value; break;
        default: break;
    }
    return linear;
}

static calc_value_s calc_length(aether_css_length value) {
    return {calc_value_kind_e::length, 0.f, calc_linear_length(value)};
}

static bool calc_add(calc_value_s& left, const calc_value_s& right, float sign) {
    if (left.kind == calc_value_kind_e::number && right.kind == calc_value_kind_e::number) {
        left.number += sign * right.number;
        return std::isfinite(left.number);
    }
    if (left.kind != right.kind) return false;
    left.length.value += sign * right.length.value;
    left.length.linear_percent += sign * right.length.linear_percent;
    left.length.linear_em += sign * right.length.linear_em;
    left.length.linear_rem += sign * right.length.linear_rem;
    left.length.linear_vw += sign * right.length.linear_vw;
    left.length.linear_vh += sign * right.length.linear_vh;
    return std::isfinite(left.length.value) && std::isfinite(left.length.linear_percent) &&
           std::isfinite(left.length.linear_em) && std::isfinite(left.length.linear_rem) &&
           std::isfinite(left.length.linear_vw) && std::isfinite(left.length.linear_vh);
}

static bool calc_multiply(calc_value_s& left, const calc_value_s& right) {
    if (left.kind == calc_value_kind_e::length && right.kind == calc_value_kind_e::length) return false;
    if (left.kind == calc_value_kind_e::number && right.kind == calc_value_kind_e::number) {
        left.number *= right.number;
        return std::isfinite(left.number);
    }
    const float scale = left.kind == calc_value_kind_e::number ? left.number : right.number;
    const aether_css_length length = left.kind == calc_value_kind_e::length ? left.length : right.length;
    left = calc_length(length);
    left.length.value *= scale;
    left.length.linear_percent *= scale;
    left.length.linear_em *= scale;
    left.length.linear_rem *= scale;
    left.length.linear_vw *= scale;
    left.length.linear_vh *= scale;
    return std::isfinite(left.length.value) && std::isfinite(left.length.linear_percent) &&
           std::isfinite(left.length.linear_em) && std::isfinite(left.length.linear_rem) &&
           std::isfinite(left.length.linear_vw) && std::isfinite(left.length.linear_vh);
}

static bool calc_divide(calc_value_s& left, const calc_value_s& right) {
    if (right.kind != calc_value_kind_e::number || right.number == 0.f || !std::isfinite(right.number)) return false;
    if (left.kind == calc_value_kind_e::number) {
        left.number /= right.number;
        return std::isfinite(left.number);
    }
    const float reciprocal = 1.f / right.number;
    left.length.value *= reciprocal;
    left.length.linear_percent *= reciprocal;
    left.length.linear_em *= reciprocal;
    left.length.linear_rem *= reciprocal;
    left.length.linear_vw *= reciprocal;
    left.length.linear_vh *= reciprocal;
    return std::isfinite(left.length.value) && std::isfinite(left.length.linear_percent) &&
           std::isfinite(left.length.linear_em) && std::isfinite(left.length.linear_rem) &&
           std::isfinite(left.length.linear_vw) && std::isfinite(left.length.linear_vh);
}

class calc_parser_s {
public:
    explicit calc_parser_s(std::string_view source) : source_(source) {}

    bool parse(calc_value_s& output) {
        skip_whitespace();
        if (!consume_identifier("calc") || !consume('(')) return false;
        calc_value_s value{};
        if (!parse_sum(value)) return false;
        skip_whitespace();
        if (!consume(')')) return false;
        skip_whitespace();
        if (position_ != source_.size()) return false;
        output = value;
        return true;
    }

private:
    bool parse_sum(calc_value_s& output) {
        if (!parse_product(output)) return false;
        for (;;) {
            const size_t before_whitespace = position_;
            skip_whitespace();
            const bool had_leading_whitespace = position_ != before_whitespace;
            if (position_ == source_.size() || (source_[position_] != '+' && source_[position_] != '-')) return true;
            const char operation = source_[position_++];
            const size_t after_operator = position_;
            skip_whitespace();
            // Binary + and - require whitespace on both sides in CSS calc().
            if (!had_leading_whitespace || position_ == after_operator) return false;
            calc_value_s right{};
            if (!parse_product(right) || !calc_add(output, right, operation == '+' ? 1.f : -1.f)) return false;
        }
    }

    bool parse_product(calc_value_s& output) {
        if (!parse_unary(output)) return false;
        for (;;) {
            const size_t before_whitespace = position_;
            skip_whitespace();
            if (position_ == source_.size() || (source_[position_] != '*' && source_[position_] != '/')) {
                // Addition/subtraction owns its preceding whitespace. Retain it
                // so parse_sum can enforce the CSS tokenization rule.
                position_ = before_whitespace;
                return true;
            }
            const char operation = source_[position_++];
            calc_value_s right{};
            if (!parse_unary(right)) return false;
            if (operation == '*' ? !calc_multiply(output, right) : !calc_divide(output, right)) return false;
        }
    }

    bool parse_unary(calc_value_s& output) {
        skip_whitespace();
        float sign = 1.f;
        if (consume('+')) sign = 1.f;
        else if (consume('-')) sign = -1.f;
        if (!parse_primary(output)) return false;
        if (output.kind == calc_value_kind_e::number) output.number *= sign;
        else {
            output.length.value *= sign;
            output.length.linear_percent *= sign;
            output.length.linear_em *= sign;
            output.length.linear_rem *= sign;
            output.length.linear_vw *= sign;
            output.length.linear_vh *= sign;
        }
        return true;
    }

    bool parse_primary(calc_value_s& output) {
        skip_whitespace();
        if (consume('(')) {
            if (!parse_sum(output)) return false;
            skip_whitespace();
            return consume(')');
        }

        const char* begin = source_.data() + position_;
        char* end = nullptr;
        const float number = std::strtof(begin, &end);
        if (end == begin || !std::isfinite(number)) return false;
        position_ += static_cast<size_t>(end - begin);

        const size_t unit_begin = position_;
        if (position_ < source_.size() && source_[position_] == '%') ++position_;
        else while (position_ < source_.size() && is_ascii_alpha(source_[position_])) ++position_;
        const auto unit = source_.substr(unit_begin, position_ - unit_begin);
        if (unit.empty()) {
            output = {calc_value_kind_e::number, number, aether_css_length_zero()};
            return true;
        }

        aether_css_length length = aether_css_length_zero();
        length.value = number;
        if (unit == "px") length.unit = AETHER_CSS_UNIT_PX;
        else if (unit == "%") length.unit = AETHER_CSS_UNIT_PERCENT;
        else if (ascii_iequals(unit, "em")) length.unit = AETHER_CSS_UNIT_EM;
        else if (ascii_iequals(unit, "rem")) length.unit = AETHER_CSS_UNIT_REM;
        else if (ascii_iequals(unit, "vw")) length.unit = AETHER_CSS_UNIT_VW;
        else if (ascii_iequals(unit, "vh")) length.unit = AETHER_CSS_UNIT_VH;
        else return false;
        output = calc_length(length);
        return true;
    }

    bool consume(char expected) {
        if (position_ >= source_.size() || source_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool consume_identifier(std::string_view expected) {
        if (source_.substr(position_, expected.size()).size() != expected.size() ||
            !ascii_iequals(source_.substr(position_, expected.size()), expected)) return false;
        position_ += expected.size();
        return true;
    }

    void skip_whitespace() {
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_]))) ++position_;
    }

    std::string_view source_;
    size_t position_ = 0;
};

} // namespace

bool aether::css::try_parse_calc(std::string_view source, calc_value_s& output) {
    return calc_parser_s(source).parse(output);
}

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

    std::string source(input);
    const auto first = source.find_first_not_of(" \t\r\n\f");
    if (first == std::string::npos) return AETHER_FALSE;
    const auto last = source.find_last_not_of(" \t\r\n\f");
    source = source.substr(first, last - first + 1);
    const char* s = source.c_str();

    calc_value_s calculated{};
    if (aether::css::try_parse_calc(source, calculated)) {
        if (calculated.kind != calc_value_kind_e::length) return AETHER_FALSE;
        *out = calculated.length;
        return AETHER_TRUE;
    }
    if (source.size() >= 5 && ascii_iequals(std::string_view(source).substr(0, 5), "calc(")) return AETHER_FALSE;

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
    if (end == buf || *end != '\0' || !std::isfinite(number)) return AETHER_FALSE;

    out->value = number;
    out->unit = unit;
    return AETHER_TRUE;
}

AETHER_API aether_css_edges aether_css_edges_zero(void) {
    const auto zero = aether_css_length_zero();
    return {zero, zero, zero, zero};
}
