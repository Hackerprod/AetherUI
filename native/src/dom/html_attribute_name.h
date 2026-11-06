#pragma once

#include <string>

namespace aether {
// The current DOM is HTML-only. Normalize names, never attribute values or
// generic CSS identifiers. Taking ownership also avoids copying parser tokens.
// Non-ASCII bytes are unchanged, independently of locale and char signedness.
inline std::string canonical_html_attribute_name(std::string name) {
    for (char& byte : name) {
        if (byte >= 'A' && byte <= 'Z')
            byte = static_cast<char>(byte + ('a' - 'A'));
    }
    return name;
}
} // namespace aether
