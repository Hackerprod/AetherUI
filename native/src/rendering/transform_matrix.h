#pragma once

#include <aether/aether.h>
#include "style_kernel_readers.h"

/* Resolves percentage-dependent transform values against a post-layout border
   box.  The returned matrix is local to that box; ancestor matrices remain
   independent Display List brackets. */
aether_transform_matrix aether_transform_matrix_resolve(
    const aether_transform_list& transforms,
    aether_transform_origin origin,
    aether_rectf border_box,
    float font_size,
    float root_font_size,
    aether_sizef viewport);

aether_pointf aether_transform_matrix_map_point(aether_transform_matrix matrix, aether_pointf point);

// Same kernel, borrowed non-contiguous Frozen or genuinely contiguous C input.
aether_transform_matrix aether_transform_matrix_resolve(
    aether::rendering::IndexedReadRange<aether_transform_operation> transforms,
    aether_transform_origin origin, aether_rectf border_box, float font_size,
    float root_font_size, aether_sizef viewport);
