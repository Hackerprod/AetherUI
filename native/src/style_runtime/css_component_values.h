#pragma once

#include "css_variable_tokens.h"
#include <string>
#include <vector>

// Components preserve inner spelling (notably calc whitespace), while comments
// between components are trivia, never whitespace or slash delimiters.
struct css_component_value {
    css_variable_token token;
    size_t begin = 0, end = 0;
    bool whitespace_before = false;
};
class css_component_cursor {
public:
    explicit css_component_cursor(const std::string& source)
        : lexer_(source.data(), source.size()) {}
    bool valid() const { return valid_; }
    bool next(css_component_value& value) {
        if (!valid_) return false;
        value = {};
        css_variable_token token;
        do {
            token = lexer_.next();
            if (token.kind == css_variable_token_kind::whitespace) value.whitespace_before = true;
        } while (token.kind == css_variable_token_kind::whitespace || token.kind == css_variable_token_kind::comment);
        if (token.kind == css_variable_token_kind::end) return false;
        if (token.kind == css_variable_token_kind::error || token.kind == css_variable_token_kind::close) {
            valid_ = false; return false;
        }
        value.token = token; value.begin = token.begin; value.end = token.end;
        std::vector<char> closers;
        auto push = [&](const css_variable_token& t) {
            if (t.kind == css_variable_token_kind::function) closers.push_back(')');
            else if (t.kind == css_variable_token_kind::open)
                closers.push_back(t.delimiter == '(' ? ')' : t.delimiter == '[' ? ']' : '}');
        };
        push(token);
        while (!closers.empty()) {
            token = lexer_.next();
            if (token.kind == css_variable_token_kind::end || token.kind == css_variable_token_kind::error) {
                valid_ = false; return false;
            }
            if (token.kind == css_variable_token_kind::close) {
                if (token.delimiter != closers.back()) { valid_ = false; return false; }
                closers.pop_back();
            } else push(token);
            value.end = token.end;
        }
        return true;
    }
private:
    css_variable_lexer lexer_;
    bool valid_ = true;
};
inline bool css_component_is_delimiter(const css_component_value& value, char delimiter) {
    return (delimiter == ',' && value.token.kind == css_variable_token_kind::comma) ||
        (value.token.kind == css_variable_token_kind::other && value.token.delimiter == delimiter &&
         value.end == value.begin + 1);
}
inline std::vector<std::string> css_split_components(const std::string& source, bool* valid = nullptr) {
    css_component_cursor cursor(source);
    css_component_value value;
    std::vector<std::string> result;
    while (cursor.next(value)) result.push_back(source.substr(value.begin, value.end - value.begin));
    if (valid) *valid = cursor.valid();
    if (!cursor.valid()) result.clear();
    return result;
}
inline std::vector<std::string> css_split_component_delimited(const std::string& source, char delimiter, bool* valid = nullptr) {
    css_component_cursor cursor(source);
    css_component_value value;
    std::vector<std::string> result;
    size_t begin = 0;
    while (cursor.next(value)) {
        if (!css_component_is_delimiter(value, delimiter)) continue;
        result.push_back(source.substr(begin, value.begin - begin));
        begin = value.end;
    }
    result.push_back(source.substr(begin));
    if (valid) *valid = cursor.valid();
    if (!cursor.valid()) result.clear();
    return result;
}
inline int css_find_component_delimiter(const std::string& source, char delimiter) {
    css_component_cursor cursor(source);
    css_component_value value;
    while (cursor.next(value)) if (css_component_is_delimiter(value, delimiter)) return static_cast<int>(value.begin);
    return -1;
}
