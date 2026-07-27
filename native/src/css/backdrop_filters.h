#pragma once

#include <aether/aether.h>

#include <string>

enum class aether_backdrop_filter_parse_error_e {
    none,
    syntax,
    unsupported_operation,
    unsupported_parameter
};

// Parses an ordered CSS filter-function list. v1 admits blur(<length>), while
// the ABI deliberately represents scalar operations for later compositor cuts.
bool aether_backdrop_filter_list_try_parse(const std::string& source,
                                           aether_backdrop_filter_list* out_filters,
                                           aether_backdrop_filter_parse_error_e* out_error = nullptr);
