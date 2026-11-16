#pragma once
#include "style_runtime_types.h"

// Single native owner of the engine's baked-in user-agent stylesheet. The
// managed host no longer carries the defaults as a managed constant; it reads
// them from this ABI export and applies the sheet at the engine-defaults layer
// (layer 0) exactly as before. The returned pointer is a static literal that
// lives for the process lifetime and must not be freed.
const char* aether_style_runtime_defaults_css();

namespace aether::style {
// Internal C++ accessor, intentionally not an ABI export or a runtime toggle.
// Use exactly one UA sheet, with full-document contextual prepare_revision.
// Not a supported replacement for legacy BODY-only subtree resolution.
const char* web_capture_defaults_css();
}
