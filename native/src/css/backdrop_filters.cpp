#include "../style_runtime/css_component_values.h"
#include "backdrop_filters.h"

#include "../style_runtime/style_runtime_types.h"

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace {

std::vector<std::string> split_functions(const std::string& source, bool* valid) { return css_split_components(source, valid); }

bool parse_blur(const std::string& function, aether_backdrop_filter* out) {
    const auto open = function.find('(');
    if (open == std::string::npos || function.back() != ')') return false;
    const auto name = str_trim(function.substr(0, open));
    if (!str_iequals(name, "blur")) return false;
    const auto amount = str_trim(function.substr(open + 1, function.size() - open - 2));
    aether_css_length length{};
    if (amount.empty() || aether_css_length_try_parse(amount.c_str(), &length) != AETHER_TRUE ||
        length.unit == AETHER_CSS_UNIT_AUTO || length.unit == AETHER_CSS_UNIT_PERCENT || length.value < 0.f) return false;
    *out = {AETHER_BACKDROP_FILTER_BLUR, length, 0.f};
    return true;
}

} // namespace

bool aether_backdrop_filter_list_try_parse(const std::string& source,
                                           aether_backdrop_filter_list* out_filters,
                                           aether_backdrop_filter_parse_error_e* out_error) {
    if (!out_filters) return false;
    if (out_error) *out_error = aether_backdrop_filter_parse_error_e::none;
    const auto value = str_trim(source);
    if (str_iequals(value, "none")) { aether_backdrop_filter_list_free(out_filters); return true; }
    bool valid = false;
    const auto functions = split_functions(value, &valid);
    if (!valid || functions.empty()) { if (out_error) *out_error = aether_backdrop_filter_parse_error_e::syntax; return false; }
    std::vector<aether_backdrop_filter> replacement;
    replacement.reserve(functions.size());
    for (const auto& function : functions) {
        const auto open = function.find('(');
        if (open == std::string::npos || function.back() != ')') {
            if (out_error) *out_error = aether_backdrop_filter_parse_error_e::syntax;
            return false;
        }
        aether_backdrop_filter filter{};
        if (!parse_blur(function, &filter)) {
            const auto name = str_trim(function.substr(0, open));
            if (out_error) *out_error = str_iequals(name, "blur")
                ? aether_backdrop_filter_parse_error_e::unsupported_parameter
                : aether_backdrop_filter_parse_error_e::unsupported_operation;
            return false;
        }
        replacement.push_back(filter);
    }
    aether_backdrop_filter_list list{};
    if (!replacement.empty()) {
        list.items = static_cast<aether_backdrop_filter*>(std::malloc(sizeof(aether_backdrop_filter) * replacement.size()));
        if (!list.items) return false;
        std::memcpy(list.items, replacement.data(), sizeof(aether_backdrop_filter) * replacement.size());
        list.count = static_cast<int32_t>(replacement.size());
    }
    aether_backdrop_filter_list_free(out_filters);
    *out_filters = list;
    return true;
}
