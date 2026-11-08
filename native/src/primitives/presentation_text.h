#pragma once

#include "ascii_text.h"

namespace aether {
// Historical C-locale presentation policy, not HTML/CSS grammar whitespace.
// Vertical tab is intentionally included; non-ASCII bytes remain opaque.
constexpr bool presentation_space_byte(unsigned char byte) noexcept {
    return byte == ' ' || byte == '\t' || byte == '\n' ||
        byte == '\v' || byte == '\f' || byte == '\r';
}

constexpr unsigned char presentation_upper_byte(unsigned char byte) noexcept {
    return byte >= 'a' && byte <= 'z'
        ? static_cast<unsigned char>(byte - ('a' - 'A')) : byte;
}
} // namespace aether
