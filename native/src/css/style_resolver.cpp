#include "css_types.h"
#include "computed_style_internal.h"
#include "../abi/aether_abi.h"

AETHER_API aether_status_t aether_css_resolve(
    aether_doc_t doc, aether_stylesheet_t author_sheet,
    float viewport_width, float viewport_height,
    aether_style_map_t* out_map) {
    (void)doc; (void)author_sheet; (void)viewport_width; (void)viewport_height;
    if (!out_map) return AETHER_ERROR_NULL_ARG;
    *out_map = new aether_style_map_s();
    return AETHER_OK;
}

AETHER_API void aether_style_map_destroy(aether_style_map_t map) {
    delete map;
}

AETHER_API aether_computed_style_handle_t aether_style_map_get(aether_style_map_t map, aether_node_t element) {
    (void)map; (void)element;
    return nullptr;
}

AETHER_API aether_status_t aether_computed_style_copy(aether_computed_style_handle_t handle, aether_computed_style_t* out) {
    if (!handle || !out) return AETHER_ERROR_NULL_ARG;
    return computed_style_deep_copy(handle->style, out);
}

AETHER_API void aether_computed_style_handle_destroy(aether_computed_style_handle_t handle) {
    if (!handle) return;
    aether_computed_style_free(&handle->style);
    delete handle;
}
