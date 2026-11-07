#pragma once

#include <aether/aether.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

// Private shared grammar for registry validation and checked application.
// Parsing never modifies an existing style, reports diagnostics, or resolves
// currentColor. Output is published only after the complete list is valid.
struct measure_inputs_s;
enum class shadow_value_error { none, invalid, inset_not_allowed, spread_not_allowed, too_many, allocation };
struct parsed_shadow_list {
    std::vector<aether_shadow> items;
    std::vector<bool> uses_current_color;
};
struct shadow_storage_policy {
    size_t max_count = static_cast<size_t>(std::numeric_limits<int32_t>::max());
    size_t max_bytes = std::numeric_limits<size_t>::max();
    void* (*allocate)(size_t) = std::malloc;
    void (*release)(void*) = std::free;
};
shadow_value_error shadow_list_parse(const std::string& source, bool box,
    parsed_shadow_list& output, const shadow_storage_policy& storage = {}, const measure_inputs_s* measures = nullptr);
shadow_value_error shadow_list_apply(const std::string& source, bool box,
    aether_shadow_list& destination, std::vector<bool>& references,
    const shadow_storage_policy& storage = {}, const measure_inputs_s* measures = nullptr);
const char* shadow_value_diagnostic_code(bool box);
const char* shadow_value_error_message(shadow_value_error error, bool box);
