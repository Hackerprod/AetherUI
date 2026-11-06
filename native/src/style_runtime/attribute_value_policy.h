#pragma once

#include <array>
#include <cstdint>
#include <string_view>

enum class attribute_value_case_mode : uint8_t {
    default_html,
    ascii_insensitive,
    sensitive
};

namespace aether {
// HTML's legacy selector-value policy is a fixed name list, not a list of
// enumerated attributes or valid values. Classify a canonical name at compile
// time for the selector, never for each node in the matching hot path.
namespace attribute_value_policy_detail {
inline constexpr std::array<std::string_view, 46> names = {
        "accept-charset", "accept", "align", "alink", "axis", "bgcolor",
        "charset", "checked", "clear", "codetype", "color", "compact",
        "declare", "defer", "dir", "direction", "disabled", "enctype",
        "face", "frame", "hreflang", "http-equiv", "lang", "language",
        "link", "media", "method", "multiple", "nohref", "noresize",
        "noshade", "nowrap", "readonly", "rel", "rev", "rules", "scope",
        "scrolling", "selected", "shape", "target", "text", "type",
        "valign", "valuetype", "vlink"
};
} // namespace attribute_value_policy_detail

constexpr bool html_attribute_defaults_ascii_insensitive(std::string_view canonical_name) noexcept {
    for (const auto name : attribute_value_policy_detail::names)
        if (canonical_name == name) return true;
    return false;
}

// The native DOM currently represents HTML documents/elements only. If real
// namespaces or XML are added, default mode must also check that context.
// Explicit i/s modes remain independent of the HTML default classification.
constexpr bool attribute_value_is_ascii_insensitive(
    attribute_value_case_mode mode, bool html_default_ascii_insensitive) noexcept {
    return mode == attribute_value_case_mode::ascii_insensitive ||
        (mode == attribute_value_case_mode::default_html && html_default_ascii_insensitive);
}
} // namespace aether
