#pragma once

#include "style_string_utils_internal.h"
#include "css_component_values.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

inline bool bi_is_global_keyword(const std::string& s) {
    return str_iequals(s, "initial") || str_iequals(s, "inherit") ||
           str_iequals(s, "unset")   || str_iequals(s, "revert");
}

inline std::vector<std::string> bi_split_ws(const std::string& s) {
    return css_split_components(s);
}
