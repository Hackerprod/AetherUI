#pragma once

#include <aether/aether.h>
#include <string>

/* Canonical text-transform representation used by intrinsic measurement, layout,
   controls, and display-list emission. */
std::string aether_apply_text_transform(const std::string& value, int32_t text_transform);
