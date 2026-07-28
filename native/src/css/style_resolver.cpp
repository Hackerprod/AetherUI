#include "css_types.h"
#include "../abi/aether_abi.h"

#include <cstdlib>
#include <cstring>

namespace {

bool copy_gradient(const aether_linear_gradient& source, aether_linear_gradient& destination) {
    destination = source;
    destination.stops = nullptr;
    if (source.stop_count < 0 || (source.stop_count > 0 && !source.stops)) return false;
    if (source.stop_count == 0) return true;
    const size_t bytes = sizeof(aether_gradient_stop) * static_cast<size_t>(source.stop_count);
    destination.stops = static_cast<aether_gradient_stop*>(std::malloc(bytes));
    if (!destination.stops) return false;
    std::memcpy(destination.stops, source.stops, bytes);
    return true;
}

} // namespace

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
    aether_computed_style_t copy = handle->style;
    copy.background_layers.items = nullptr;
    copy.background_layers.count = 0;
    copy.backdrop_filters.items = nullptr;
    copy.backdrop_filters.count = 0;
    copy.transforms.items = nullptr;
    copy.transforms.count = 0;
    copy.text_gradient.stops = nullptr;
    copy.text_shadows.items = nullptr;
    copy.text_shadows.count = 0;
    copy.box_shadows.items = nullptr;
    copy.box_shadows.count = 0;
    if (aether_background_layer_list_copy(&handle->style.background_layers, &copy.background_layers) != AETHER_TRUE ||
        aether_backdrop_filter_list_copy(&handle->style.backdrop_filters, &copy.backdrop_filters) != AETHER_TRUE ||
        aether_transform_list_copy(&handle->style.transforms, &copy.transforms) != AETHER_TRUE ||
        !copy_gradient(handle->style.text_gradient, copy.text_gradient) ||
        aether_shadow_list_copy(&handle->style.text_shadows, &copy.text_shadows) != AETHER_TRUE ||
        aether_shadow_list_copy(&handle->style.box_shadows, &copy.box_shadows) != AETHER_TRUE) {
        aether_computed_style_free(&copy);
        return AETHER_ERROR_OUT_OF_MEMORY;
    }
    *out = copy;
    return AETHER_OK;
}

AETHER_API void aether_computed_style_handle_destroy(aether_computed_style_handle_t handle) {
    delete handle;
}
