#pragma once

#include <aether/aether.h>
#include <string_view>

namespace aether::css {

enum class calc_value_kind_e { number, length };

struct calc_value_s {
    calc_value_kind_e kind = calc_value_kind_e::number;
    float number = 0.f;
    aether_css_length length = aether_css_length_zero();
};

// Evaluates a complete calc expression without coercing its resulting kind.
bool try_parse_calc(std::string_view source, calc_value_s& output);

} // namespace aether::css
