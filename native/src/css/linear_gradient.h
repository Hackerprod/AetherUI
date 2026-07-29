#pragma once

#include <aether/aether.h>

struct aether_linear_gradient_parse_info_s {
    // The ABI stores only a fixed direction vector. Keyword corner directions
    // need the final paint rectangle for fully CSS-accurate geometry.
    bool uses_fixed_corner_direction = false;
};

/* CSS Images resolves color-stop <length-percentage>s only after the painted
   gradient line is known. The input remains immutable; the returned gradient
   owns a resolved stop array and must be released by the caller. */
struct aether_linear_gradient_resolution_context_s {
    aether_rectf gradient_rect{};
    float font_size = 16.f;
    float root_font_size = 16.f;
    float viewport_width = 0.f;
    float viewport_height = 0.f;
};

/* Parses and normalizes the CSS linear-gradient subset represented by the ABI.
   The returned stops own heap storage; release them with aether_linear_gradient_free. */
bool aether_linear_gradient_try_parse(const char* source, aether_linear_gradient* out_gradient,
                                      aether_linear_gradient_parse_info_s* out_info = nullptr);

bool aether_linear_gradient_resolve(const aether_linear_gradient& source,
                                    const aether_linear_gradient_resolution_context_s& context,
                                    aether_linear_gradient* out_gradient);
