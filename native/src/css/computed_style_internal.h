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

#include "style_read_view.h"
// Additive Frozen readers. Legacy flat overloads retain their existing semantics.
aether_status_t computed_style_deep_copy(aether::style::ComputedStyleReadView, aether_computed_style_t*) noexcept;
aether_status_t computed_style_inherit_checked(const aether::style::ComputedStyleReadView*, aether_computed_style_t*) noexcept;
namespace aether::style {
aether_status_t copy_builder_backgrounds(ComputedStyleReadView, aether_background_layer_list*) noexcept;
aether_status_t copy_builder_shadows(ReadOnlyArrayView<aether_shadow>, aether_shadow_list*) noexcept;
aether_status_t copy_builder_transforms(ReadOnlyArrayView<aether_transform_operation>, aether_transform_list*) noexcept;
}
