#include <new>
#include "../style_runtime/css_component_values.h"
#include "backdrop_filters.h"
#include "css_length_internal.h"

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
    const auto amount = function.substr(open + 1, function.size() - open - 2);
    css_component_cursor cursor(amount);
    css_component_value component, extra;
    aether_css_length length = aether_css_length_zero();
    if (cursor.next(component)) {
        if (cursor.next(extra) || !cursor.valid()) return false;
        const auto argument = amount.substr(component.begin, component.end - component.begin);
        aether::css::calc_value_s parsed{};
        if (component.token.kind == css_variable_token_kind::function) {
            // Reject percentage tokens before arithmetic can cancel their units.
            css_variable_lexer lexer(argument.data(), argument.size());
            for (;;) {
                const auto token = lexer.next();
                if (token.kind == css_variable_token_kind::end) break;
                if (token.kind == css_variable_token_kind::error ||
                    token.serialization == css_variable_serialization::percentage) return false;
            }
            if (!aether::css::try_parse_calc(argument, parsed) ||
                parsed.kind != aether::css::calc_value_kind_e::length) return false;
            length = parsed.length;
        } else {
            double original_number = 0.0;
            if (!aether::css::try_parse_numeric_literal(argument, parsed, &original_number)) return false;
            if (parsed.kind == aether::css::calc_value_kind_e::number) {
                if (original_number != 0.0) return false;
            } else {
                // Validate before float rounding can hide a negative nonzero literal.
                if (original_number < 0.0 || parsed.length.unit == AETHER_CSS_UNIT_PERCENT) return false;
                length = parsed.length;
            }
        }
    } else if (!cursor.valid()) return false;
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
    bool valid = false;
    const auto functions = split_functions(value, &valid);
    if (!valid || functions.empty()) { if (out_error) *out_error = aether_backdrop_filter_parse_error_e::syntax; return false; }
    if (functions.size() == 1 && str_iequals(functions.front(), "none")) {
        aether_backdrop_filter_list_free(out_filters); return true;
    }
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
        if (!list.items) throw std::bad_alloc();
        std::memcpy(list.items, replacement.data(), sizeof(aether_backdrop_filter) * replacement.size());
        list.count = static_cast<int32_t>(replacement.size());
    }
    aether_backdrop_filter_list_free(out_filters);
    *out_filters = list;
    return true;
}
