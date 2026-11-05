#pragma once

#include "css_variable_tokens.h"
#include <string_view>

// Media/prelude component scanning must follow CSS string escapes. Keep this
// adapter separate from the custom-property lexer's existing error contract.
inline bool css_component_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}
inline bool css_component_newline(char c) { return c == '\n' || c == '\r' || c == '\f'; }
inline bool css_component_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Called on a backslash. A hex escape consumes up to six digits and one
// optional CSS whitespace character (CRLF counts as one). It does not decode
// names or add support for escaped feature names/units.
inline void css_component_skip_escape(std::string_view source, size_t& position) {
    ++position;
    if (position == source.size()) return;
    if (css_component_hex(source[position])) {
        size_t count = 0;
        while (position < source.size() && css_component_hex(source[position]) && count < 6) {
            ++position;
            ++count;
        }
        if (position < source.size() && css_component_space(source[position])) {
            const char c = source[position++];
            if (c == '\r' && position < source.size() && source[position] == '\n') ++position;
        }
    } else {
        const char c = source[position++];
        // In a string this is a line continuation. URL validation separately
        // rejects escaped newlines before calling this helper.
        if (c == '\r' && position < source.size() && source[position] == '\n') ++position;
    }
}

class css_component_lexer {
public:
    css_component_lexer(const char* data, size_t length, size_t start = 0)
        : source_(data, length), position_(start) {}

    css_variable_token next() {
        if (position_ < source_.size() && (source_[position_] == '\'' || source_[position_] == '"')) {
            css_variable_token token;
            token.begin = position_;
            const char quote = source_[position_++];
            while (position_ < source_.size()) {
                const char c = source_[position_];
                if (c == quote) {
                    ++position_;
                    token.kind = css_variable_token_kind::other;
                    token.end = position_;
                    return token;
                }
                if (css_component_newline(c)) {
                    ++position_;
                    token.kind = css_variable_token_kind::error;
                    token.end = position_;
                    return token;
                }
                if (c == '\\') css_component_skip_escape(source_, position_);
                else ++position_;
            }
            // Retain an EOF marker so media can add the implied string close.
            // Unlike bad-string, this is accepted by media's EOF handling.
            token.kind = css_variable_token_kind::error;
            token.end = position_;
            return token;
        }
        const auto token = css_variable_lexer(source_.data(), source_.size(), position_).next();
        position_ = token.end;
        return token;
    }

private:
    std::string_view source_;
    size_t position_;
};
