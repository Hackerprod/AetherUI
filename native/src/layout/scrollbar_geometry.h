#pragma once

#include "layout_types.h"

/* Shared by display-list construction and the public layout ABI.  Scrollbar
   geometry is intentionally not reconstructed in a renderer or host. */
aether_scrollbar_geometry_t aether_resolve_scrollbar_geometry(const aether_layout_box_s& box);
aether_scrollbar_geometry_t aether_resolve_scrollbar_geometry(
    const aether_layout_box_s& box,
    aether_scroll_offset_t offset);
