#pragma once

#include "css_component_tokens.h"
#include <string>
#include <vector>

// Private declaration-value extraction, shared by stylesheet and inline
// compilation. Inspect source tokens, not decoded punctuation or text suffixes.
// Only whole outer trivia tokens may be trimmed: an escaped space (including a
// hex escape terminator) is part of its identifier and must survive extraction.
inline bool css_strip_important(std::string& value) {
    struct component {
        css_variable_token token;
        size_t prefix_end = 0;
    } previous, last;
    size_t first = std::string::npos;
    std::vector<char> blocks;
    css_component_lexer lexer(value.data(), value.size());
    for (;;) {
        const auto token = lexer.next();
        if (token.kind == css_variable_token_kind::end) break;
        if (token.kind == css_variable_token_kind::error) return false;
        if (token.kind == css_variable_token_kind::whitespace ||
            token.kind == css_variable_token_kind::comment) continue;

        if (token.kind == css_variable_token_kind::close) {
            if (blocks.empty() || blocks.back() != token.delimiter) return false;
            blocks.pop_back();
            // A balanced block is one component. Its closing delimiter belongs
            // to the value when an annotation follows it.
            if (blocks.empty()) last.token.end = token.end;
            continue;
        }
        if (blocks.empty()) {
            if (first == std::string::npos) first = token.begin;
            previous = last;
            last = {token, previous.token.end};
        }
        if (token.kind == css_variable_token_kind::open ||
            token.kind == css_variable_token_kind::function)
            blocks.push_back(token.delimiter == '(' ? ')' : token.delimiter == '[' ? ']' : '}');
    }
    // Do not infer priority from a marker inside an unterminated component, or
    // alter bytes inside that component as if they were outer trivia.
    if (!blocks.empty()) return false;

    bool important = false;
    if (previous.token.kind == css_variable_token_kind::other &&
        previous.token.delimiter == '!' && last.token.kind == css_variable_token_kind::ident) {
        size_t matched = 0;
        css_variable_lexer decoder(value.data() + last.token.begin,
            last.token.end - last.token.begin);
        important = decoder.decode_identifier([](void* context, const char* bytes, size_t size) {
            auto& matched = *static_cast<size_t*>(context);
            if (size != 1 || matched == 9) return false;
            char c = bytes[0];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c != "important"[matched]) return false;
            ++matched;
            return true;
        }, &matched) && matched == 9;
    }
    const size_t end = important ? previous.prefix_end : last.token.end;
    if (first == std::string::npos || end <= first) value.clear();
    else value = value.substr(first, end - first);
    return important;
}
