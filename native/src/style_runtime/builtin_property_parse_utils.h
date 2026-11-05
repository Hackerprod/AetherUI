#pragma once

#include "style_string_utils_internal.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

inline bool bi_is_global_keyword(const std::string& s) {
    return str_iequals(s, "initial") || str_iequals(s, "inherit") ||
           str_iequals(s, "unset")   || str_iequals(s, "revert");
}

inline std::vector<std::string> bi_split_ws(const std::string& s) {
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

