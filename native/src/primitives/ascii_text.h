#pragma once

#include <string>
#include <string_view>

namespace aether {
// Byte identity only: no locale, Unicode case folding, or normalization.
constexpr unsigned char ascii_lower_byte(unsigned char byte) noexcept {
    return byte >= 'A' && byte <= 'Z'
        ? static_cast<unsigned char>(byte + ('a' - 'A')) : byte;
}

inline std::string ascii_lowercase(std::string text) {
    for (char& byte : text)
        byte = static_cast<char>(ascii_lower_byte(static_cast<unsigned char>(byte)));
    return text;
}

constexpr bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (ascii_lower_byte(static_cast<unsigned char>(a[i])) !=
            ascii_lower_byte(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

constexpr bool ascii_istarts_with(std::string_view text, std::string_view prefix) noexcept {
    return prefix.size() <= text.size() && ascii_iequals(text.substr(0, prefix.size()), prefix);
}

constexpr bool ascii_iends_with(std::string_view text, std::string_view suffix) noexcept {
    return suffix.size() <= text.size() &&
        ascii_iequals(text.substr(text.size() - suffix.size()), suffix);
}

constexpr bool ascii_icontains(std::string_view text, std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > text.size()) return false;
    for (size_t i = 0; i <= text.size() - needle.size(); ++i)
        if (ascii_iequals(text.substr(i, needle.size()), needle)) return true;
    return false;
}

// HTML and CSS whitespace exclude vertical tab and every non-ASCII byte.
constexpr bool is_html_space(unsigned char byte) noexcept {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\f' || byte == '\r';
}
} // namespace aether
