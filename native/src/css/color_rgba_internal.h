#pragma once
#include <aether/aether.h>
#include <string>
namespace aether::css {
// Legacy comma rgba only, finite before clamp. Transactional; no public ABI.
bool parse_legacy_rgba_strict(const std::string& source, aether_color_rgba& output);
}
