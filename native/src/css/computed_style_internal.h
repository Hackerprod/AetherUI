#pragma once
#include <aether/aether.h>

/* Internal deep copy of a computed style's owned buffers. This is the single
   source of truth for style copying shared by the public
   aether_computed_style_copy ABI and the resolved style map, so every copy
   follows the same ownership rules. On failure `dst` is left as the canonical
   empty style (safe to free). */
aether_status_t computed_style_deep_copy(const aether_computed_style_t& src,
                                         aether_computed_style_t* dst);

/* Fallible inheritance for transactional style/layout callers. Unlike the legacy
 * struct-returning adapter it cannot turn allocation failure into lost values. */
aether_status_t computed_style_inherit_checked(const aether_computed_style_t* parent,
                                                aether_computed_style_t* out);
