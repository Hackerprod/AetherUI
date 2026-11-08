#include "css_length_internal.h"
#include "css_numeric_conversion_internal.h"
#include <cmath>
#include <limits>
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

static bool css_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool ascii_digit(char c) { return c >= '0' && c <= '9'; }

static bool store_length_component(double number, float& output) {
    if (!std::isfinite(number) || std::fabs(number) > std::numeric_limits<float>::max()) return false;
    output = static_cast<float>(number);
    return true;
}

// Fix the CSS token span before invoking the C conversion routine. In
// particular, C hexadecimal numbers and an incomplete fraction are not CSS.
static bool parse_numeric_token(std::string_view source, size_t& position, calc_value_s& output,
                                aether::css::c_numeric_locale& locale, double* original_number = nullptr) {
    const size_t start = position;
    size_t end = start;
    if (end < source.size() && (source[end] == '+' || source[end] == '-')) ++end;
    const size_t digits = end;
    bool nonzero_mantissa = false;
    while (end < source.size() && ascii_digit(source[end])) {
        nonzero_mantissa |= source[end] != '0';
        ++end;
    }
    bool has_digits = end != digits;
    if (end + 1 < source.size() && source[end] == '.' && ascii_digit(source[end + 1])) {
        ++end;
        while (end < source.size() && ascii_digit(source[end])) {
            nonzero_mantissa |= source[end] != '0';
            ++end;
        }
        has_digits = true;
    }
    if (!has_digits) return false;
    if (end < source.size() && (source[end] == 'e' || source[end] == 'E')) {
        size_t exponent = end + 1;
        if (exponent < source.size() && (source[exponent] == '+' || source[exponent] == '-')) ++exponent;
        const size_t exponent_digits = exponent;
        while (exponent < source.size() && ascii_digit(source[exponent])) ++exponent;
        if (exponent != exponent_digits) end = exponent;
    }
    const std::string token(source.substr(start, end - start));
    const auto converted = locale.parse_double(token);
    if (!converted.valid || converted.range_error ||
        (converted.value == 0.0 && nonzero_mantissa)) return false;
    const double number = converted.value;

    const size_t unit_start = end;
    if (end < source.size() && source[end] == '%') ++end;
    else while (end < source.size() && is_ascii_alpha(source[end])) ++end;
    const auto unit = source.substr(unit_start, end - unit_start);
    calc_value_s value{};
    if (unit.empty()) value.number = number;
    else {
        value.kind = calc_value_kind_e::length;
        if (ascii_iequals(unit, "px")) value.length.unit = AETHER_CSS_UNIT_PX;
        else if (unit == "%") value.length.unit = AETHER_CSS_UNIT_PERCENT;
        else if (ascii_iequals(unit, "em")) value.length.unit = AETHER_CSS_UNIT_EM;
        else if (ascii_iequals(unit, "rem")) value.length.unit = AETHER_CSS_UNIT_REM;
        else if (ascii_iequals(unit, "vw")) value.length.unit = AETHER_CSS_UNIT_VW;
        else if (ascii_iequals(unit, "vh")) value.length.unit = AETHER_CSS_UNIT_VH;
        else return false;
        if (!store_length_component(number, value.length.value)) return false;
    }
    position = end;
    output = value;
    if (original_number) *original_number = number;
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

static bool scale_length(aether_css_length& length, double scalar, bool divide) {
    float* components[] = {&length.value, &length.linear_percent, &length.linear_em,
        &length.linear_rem, &length.linear_vw, &length.linear_vh};
    for (auto component : components) {
        const double value = divide ? *component / scalar : *component * scalar;
        if (!store_length_component(value, *component)) return false;
    }
    return true;
}

static bool calc_multiply(calc_value_s& left, const calc_value_s& right) {
    if (left.kind == calc_value_kind_e::length && right.kind == calc_value_kind_e::length) return false;
    if (left.kind == calc_value_kind_e::number && right.kind == calc_value_kind_e::number) {
        const double result = left.number * right.number;
        if (!std::isfinite(result) || (result == 0.0 && left.number != 0.0 && right.number != 0.0)) return false;
        left.number = result;
        return true;
    }
    const double scale = left.kind == calc_value_kind_e::number ? left.number : right.number;
    const aether_css_length length = left.kind == calc_value_kind_e::length ? left.length : right.length;
    left = calc_length(length);
    return scale_length(left.length, scale, false);
}

static bool calc_divide(calc_value_s& left, const calc_value_s& right) {
    if (right.kind != calc_value_kind_e::number || right.number == 0.0 || !std::isfinite(right.number)) return false;
    if (left.kind == calc_value_kind_e::number) {
        const double result = left.number / right.number;
        if (!std::isfinite(result) || (result == 0.0 && left.number != 0.0)) return false;
        left.number = result;
        return true;
    }
    return scale_length(left.length, right.number, true);
}

class calc_parser_s {
public:
    explicit calc_parser_s(std::string_view source) : source_(source) {}

    bool parse(calc_value_s& output) {
        if (!skip_trivia()) return false;
        // The opening parenthesis must be adjacent to the identifier: trivia
        // here would turn the function into an ident followed by a block.
        if (!consume_identifier("calc") || !consume('(')) return false;
        calc_value_s value{};
        if (!parse_sum(value)) return false;
        if (!skip_trivia()) return false;
        if (!consume(')')) return false;
        if (!skip_trivia()) return false;
        if (position_ != source_.size()) return false;
        output = value;
        return true;
    }

private:
    bool parse_sum(calc_value_s& output) {
        if (!parse_product(output)) return false;
        for (;;) {
            bool had_leading_whitespace = false;
            if (!skip_trivia(&had_leading_whitespace)) return false;
            if (position_ == source_.size() || (source_[position_] != '+' && source_[position_] != '-')) return true;
            const char operation = source_[position_++];
            bool had_trailing_whitespace = false;
            if (!skip_trivia(&had_trailing_whitespace)) return false;
            // Comments separate tokens, but do not supply CSS whitespace.
            if (!had_leading_whitespace || !had_trailing_whitespace) return false;
            calc_value_s right{};
            if (!parse_product(right) || !calc_add(output, right, operation == '+' ? 1.f : -1.f)) return false;
        }
    }

    bool parse_product(calc_value_s& output) {
        if (!parse_primary(output)) return false;
        for (;;) {
            const size_t before_trivia = position_;
            if (!skip_trivia()) return false;
            if (position_ == source_.size() || (source_[position_] != '*' && source_[position_] != '/')) {
                // Addition/subtraction owns all preceding trivia. Restore it
                // so parse_sum can distinguish comments from real whitespace.
                position_ = before_trivia;
                return true;
            }
            const char operation = source_[position_++];
            calc_value_s right{};
            if (!parse_primary(right)) return false;
            if (operation == '*' ? !calc_multiply(output, right) : !calc_divide(output, right)) return false;
        }
    }

    bool parse_primary(calc_value_s& output) {
        if (!skip_trivia()) return false;
        if (consume('(')) {
            if (!parse_sum(output)) return false;
            if (!skip_trivia()) return false;
            return consume(')');
        }

        if (!parse_numeric_token(source_, position_, output, locale_)) return false;
        if (output.kind == calc_value_kind_e::length) output = calc_length(output.length);
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

    bool skip_trivia(bool* saw_whitespace = nullptr) {
        if (saw_whitespace) *saw_whitespace = false;
        while (position_ < source_.size()) {
            if (css_space(source_[position_])) {
                if (saw_whitespace) *saw_whitespace = true;
                ++position_;
            } else if (source_[position_] == '/' && position_ + 1 < source_.size() &&
                       source_[position_ + 1] == '*') {
                const size_t end = source_.find("*/", position_ + 2);
                if (end == std::string_view::npos) return false;
                position_ = end + 2;
            } else {
                break;
            }
        }
        return true;
    }

    aether::css::c_numeric_locale locale_;
    std::string_view source_;
    size_t position_ = 0;
};

} // namespace

bool aether::css::try_parse_numeric_literal(std::string_view source, calc_value_s& output, double* original_number) {
    while (!source.empty() && css_space(source.front())) source.remove_prefix(1);
    while (!source.empty() && css_space(source.back())) source.remove_suffix(1);
    size_t position = 0;
    calc_value_s value{};
    c_numeric_locale locale;
    double number = 0.0;
    if (!parse_numeric_token(source, position, value, locale, &number) || position != source.size()) return false;
    output = value;
    if (original_number) *original_number = number;
    return true;
}

bool aether::css::try_parse_calc(std::string_view source, calc_value_s& output) {
    return calc_parser_s(source).parse(output);
}

// The private parser regression compiles only the internal helpers and uses
// the public functions from Core. Avoid defining dllimport functions on Windows.
#ifndef AETHER_CSS_NUMERIC_ONLY
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
    // Compute in double so multiplication before percentage division, or
    // cancellation in an affine value, cannot overflow a finite float result.
    // The legacy ABI keeps AUTO as NaN and out-of-range results as infinity;
    // contextual used-value consumers reject non-finite results transactionally.
    double value = l.value;
    switch (l.unit) {
        case AETHER_CSS_UNIT_AUTO: return NAN;
        case AETHER_CSS_UNIT_PX: break;
        case AETHER_CSS_UNIT_PERCENT: value = double(reference) * l.value / 100.; break;
        case AETHER_CSS_UNIT_EM: value = double(font_size) * l.value; break;
        case AETHER_CSS_UNIT_REM: value = double(root_font_size) * l.value; break;
        case AETHER_CSS_UNIT_VW: value = double(viewport_width) * l.value / 100.; break;
        case AETHER_CSS_UNIT_VH: value = double(viewport_height) * l.value / 100.; break;
        case AETHER_CSS_UNIT_LINEAR:
            value += double(reference) * l.linear_percent / 100.
                + double(font_size) * l.linear_em
                + double(root_font_size) * l.linear_rem
                + double(viewport_width) * l.linear_vw / 100.
                + double(viewport_height) * l.linear_vh / 100.;
            break;
    }
    if (std::abs(value) > std::numeric_limits<float>::max())
        return std::copysign(std::numeric_limits<float>::infinity(), value);
    return static_cast<float>(value);
}

AETHER_API aether_bool_t aether_css_length_try_parse(const char* input, aether_css_length* out) {
    if (!input || !out) return AETHER_FALSE;
    *out = aether_css_length_auto();

    std::string source(input);
    const auto first = source.find_first_not_of(" \t\r\n\f");
    if (first == std::string::npos) return AETHER_FALSE;
    const auto last = source.find_last_not_of(" \t\r\n\f");
    source = source.substr(first, last - first + 1);

    calc_value_s calculated{};
    if (aether::css::try_parse_calc(source, calculated)) {
        if (calculated.kind != calc_value_kind_e::length) return AETHER_FALSE;
        *out = calculated.length;
        return AETHER_TRUE;
    }
    if (source.size() >= 5 && ascii_iequals(std::string_view(source).substr(0, 5), "calc(")) return AETHER_FALSE;

    if (source == "auto") {
        *out = aether_css_length_auto();
        return AETHER_TRUE;
    }

    calc_value_s literal{};
    if (!aether::css::try_parse_numeric_literal(source, literal)) return AETHER_FALSE;
    if (literal.kind == calc_value_kind_e::length) *out = literal.length;
    else {
        // Preserve the ordinary-property API's historical unitless-to-px policy.
        aether_css_length length = aether_css_length_zero();
        if (!store_length_component(literal.number, length.value)) return AETHER_FALSE;
        *out = length;
    }
    return AETHER_TRUE;
}

AETHER_API aether_css_edges aether_css_edges_zero(void) {
    const auto zero = aether_css_length_zero();
    return {zero, zero, zero, zero};
}

#endif // AETHER_CSS_NUMERIC_ONLY
