#pragma once

#include "ascii_text.h"
#include <string_view>

namespace aether {
// Borrowed spans only: callers must keep the source alive while scanning and
// copy any token they retain. Delimiting does not normalize or compare bytes.
class html_space_token_scanner {
    std::string_view remaining_;
public:
    constexpr explicit html_space_token_scanner(std::string_view source) noexcept
        : remaining_(source) {}

    constexpr bool next(std::string_view& token) noexcept {
        size_t start = 0;
        while (start < remaining_.size() &&
               is_html_space(static_cast<unsigned char>(remaining_[start]))) ++start;
        remaining_.remove_prefix(start);
        size_t end = 0;
        while (end < remaining_.size() &&
               !is_html_space(static_cast<unsigned char>(remaining_[end]))) ++end;
        token = remaining_.substr(0, end);
        remaining_.remove_prefix(end);
        return !token.empty();
    }
};

// Class membership is exact. Attribute selectors choose their own comparison
// mode while sharing only the scanner above.
constexpr bool html_space_contains(std::string_view source, std::string_view needle) noexcept {
    if (needle.empty()) return false;
    html_space_token_scanner scanner(source);
    std::string_view token;
    while (scanner.next(token))
        if (token == needle) return true;
    return false;
}
} // namespace aether
