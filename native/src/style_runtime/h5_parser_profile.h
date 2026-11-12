#pragma once
// Internal bounded admission evidence, not a public CSS conformance API.
#include <vector>
#include <cstddef>
struct compiled_selector_s;
#include <string_view>
struct media_profile_evidence {
    bool full_consumption = false;
    bool profile_supported = false;
    bool saw_unknown_or_discarded = false;
    size_t consumed_bytes = 0;
};
// Transactional: out is replaced only on success. Allocation failure propagates.
bool selector_parse_list_strict_profile(std::string_view source,
    std::vector<compiled_selector_s>& out);
media_profile_evidence media_condition_profile(std::string_view source);

// Header-light admission for clients also including legacy css_types.
bool selector_profile_supported(std::string_view source);
