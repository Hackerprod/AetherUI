#pragma once
#include "style_runtime_types.h"

// Single native owner of the engine's baked-in user-agent stylesheet. The
// managed host no longer carries the defaults as a managed constant; it reads
// them from this ABI export and applies the sheet at the engine-defaults layer
// (layer 0) exactly as before. The returned pointer is a static literal that
// lives for the process lifetime and must not be freed.
const char* aether_style_runtime_defaults_css();
