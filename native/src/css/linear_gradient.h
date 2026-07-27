#pragma once

#include <aether/aether.h>

struct aether_linear_gradient_parse_info_s {
    // The ABI stores only a fixed direction vector. Keyword corner directions
    // need the final paint rectangle for fully CSS-accurate geometry.
    bool uses_fixed_corner_direction = false;
};

/* Parses and normalizes the CSS linear-gradient subset represented by the ABI.
   The returned stops own heap storage; release them with aether_linear_gradient_free. */
bool aether_linear_gradient_try_parse(const char* source, aether_linear_gradient* out_gradient,
                                      aether_linear_gradient_parse_info_s* out_info = nullptr);
