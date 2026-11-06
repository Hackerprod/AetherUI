#pragma once

#include "../primitives/ascii_text.h"
#include <utility>

namespace aether {
// The current DOM is HTML-only. Normalize names, never attribute values or
// generic CSS identifiers. Taking ownership also avoids copying parser tokens.
// Non-ASCII bytes are unchanged, independently of locale and char signedness.
inline std::string canonical_html_attribute_name(std::string name) {
    return ascii_lowercase(std::move(name));
}
} // namespace aether
