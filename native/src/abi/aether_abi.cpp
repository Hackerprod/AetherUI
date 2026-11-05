#include "aether_abi.h"

#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

bool gradients_equal(const aether_linear_gradient& left, const aether_linear_gradient& right) {
    if (left.direction_x != right.direction_x || left.direction_y != right.direction_y ||
        left.stop_count != right.stop_count) return false;
    if (left.stop_count < 0 || (left.stop_count > 0 && (!left.stops || !right.stops))) return false;
    return left.stop_count == 0 || std::memcmp(left.stops, right.stops,
        sizeof(aether_gradient_stop) * static_cast<size_t>(left.stop_count)) == 0;
}

bool copy_gradient(const aether_linear_gradient& source, aether_linear_gradient* destination) {
    if (!destination || source.stop_count < 0 || (source.stop_count > 0 && !source.stops)) return false;
    *destination = source;
    destination->stops = nullptr;
    if (source.stop_count == 0) return true;
    const size_t count = static_cast<size_t>(source.stop_count);
    if (count > (std::numeric_limits<size_t>::max)() / sizeof(aether_gradient_stop)) return false;
    destination->stops = static_cast<aether_gradient_stop*>(std::malloc(sizeof(aether_gradient_stop) * count));
    if (!destination->stops) return false;
    std::memcpy(destination->stops, source.stops, sizeof(aether_gradient_stop) * count);
    return true;
}

char* copy_string(const char* source) {
    if (!source) return nullptr;
    const size_t length = std::strlen(source);
    if (length == (std::numeric_limits<size_t>::max)()) return nullptr;
    auto* destination = static_cast<char*>(std::malloc(length + 1));
    if (!destination) return nullptr;
    std::memcpy(destination, source, length + 1);
    return destination;
}

void free_background_layer(aether_background_layer* layer) {
    if (!layer) return;
    aether_linear_gradient_free(&layer->gradient);
    std::free(layer->image_source);
    *layer = {};
}

bool copy_background_layer(const aether_background_layer& source, aether_background_layer* destination) {
    if (!destination) return false;
    *destination = source;
    destination->gradient.stops = nullptr;
    destination->image_source = nullptr;
    if (source.kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT) {
        return copy_gradient(source.gradient, &destination->gradient);
    }
    if (source.kind == AETHER_BACKGROUND_LAYER_IMAGE_URL && source.image_source) {
        destination->image_source = copy_string(source.image_source);
        return destination->image_source != nullptr;
    }
    return false;
}

bool background_layers_equal(const aether_background_layer& left, const aether_background_layer& right) {
    if (left.kind != right.kind ||
        std::memcmp(&left.position_x, &right.position_x, sizeof(aether_css_length)) != 0 ||
        std::memcmp(&left.position_y, &right.position_y, sizeof(aether_css_length)) != 0 ||
        left.size_kind != right.size_kind ||
        std::memcmp(&left.size_x, &right.size_x, sizeof(aether_css_length)) != 0 ||
        std::memcmp(&left.size_y, &right.size_y, sizeof(aether_css_length)) != 0 ||
        left.repeat_x != right.repeat_x || left.repeat_y != right.repeat_y) return false;
    if (left.kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT)
        return gradients_equal(left.gradient, right.gradient);
    if (left.kind == AETHER_BACKGROUND_LAYER_IMAGE_URL)
        return left.image_source && right.image_source && std::strcmp(left.image_source, right.image_source) == 0;
    return false;
}

} // namespace

extern "C" {

AETHER_API uint32_t aether_abi_version(void) {
    return AETHER_ABI_VERSION_CURRENT;
}

AETHER_API aether_capabilities_t aether_abi_capabilities(void) {
    return AETHER_CAPABILITY_DOM |
           AETHER_CAPABILITY_STYLE_RUNTIME |
           AETHER_CAPABILITY_LAYOUT |
           AETHER_CAPABILITY_DISPLAY_LIST |
           AETHER_CAPABILITY_RENDER_PLAN |
           AETHER_CAPABILITY_INPUT_HIT_TEST |
           AETHER_CAPABILITY_LAYOUT_TRANSITION_CONTEXT;
}

AETHER_API size_t aether_computed_style_size(void) {
    return sizeof(aether_computed_style_t);
}

AETHER_API size_t aether_paint_command_size(void) {
    return sizeof(aether_paint_command_t);
}

AETHER_API const char* aether_status_message(aether_status_t status) {
    switch (status) {
        case AETHER_OK:                   return "OK";
        case AETHER_ERROR_NULL_ARG:       return "Null argument";
        case AETHER_ERROR_INVALID_STATE:  return "Invalid state";
        case AETHER_ERROR_OUT_OF_MEMORY:  return "Out of memory";
        case AETHER_ERROR_DISPOSED:       return "Object disposed";
        case AETHER_ERROR_PARSE_FAILED:   return "Parse failed";
        default:                          return "Unknown error";
    }
}

AETHER_API void aether_string_free(char* str) {
    free(str);
}

AETHER_API void aether_linear_gradient_free(aether_linear_gradient* g) {
    if (g && g->stops) {
        free(g->stops);
        g->stops = nullptr;
        g->stop_count = 0;
    }
}

AETHER_API void aether_shadow_list_free(aether_shadow_list* list) {
    if (!list) return;
    free(list->items);
    list->items = nullptr;
    list->count = 0;
}

AETHER_API void aether_background_layer_list_free(aether_background_layer_list* list) {
    if (!list) return;
    if (list->items && list->count > 0) {
        for (int32_t index = 0; index < list->count; ++index) free_background_layer(&list->items[index]);
    }
    std::free(list->items);
    list->items = nullptr;
    list->count = 0;
}

AETHER_API void aether_backdrop_filter_list_free(aether_backdrop_filter_list* list) {
    if (!list) return;
    std::free(list->items);
    list->items = nullptr;
    list->count = 0;
}

AETHER_API void aether_transform_list_free(aether_transform_list* list) {
    if (!list) return;
    std::free(list->items);
    list->items = nullptr;
    list->count = 0;
}

AETHER_API aether_bool_t aether_transform_list_copy(const aether_transform_list* source,
                                                      aether_transform_list* destination) {
    if (!destination) return AETHER_FALSE;
    if (source == destination) return AETHER_TRUE;
    if (!source || source->count < 0 || (source->count > 0 && !source->items)) {
        aether_transform_list_free(destination);
        return AETHER_FALSE;
    }
    aether_transform_list replacement{};
    if (source->count > 0) {
        const size_t count = static_cast<size_t>(source->count);
        if (count > (std::numeric_limits<size_t>::max)() / sizeof(aether_transform_operation)) {
            aether_transform_list_free(destination);
            return AETHER_FALSE;
        }
        replacement.items = static_cast<aether_transform_operation*>(
            std::malloc(sizeof(aether_transform_operation) * count));
        if (!replacement.items) {
            aether_transform_list_free(destination);
            return AETHER_FALSE;
        }
        std::memcpy(replacement.items, source->items, sizeof(aether_transform_operation) * count);
        replacement.count = source->count;
    }
    aether_transform_list_free(destination);
    *destination = replacement;
    return AETHER_TRUE;
}

AETHER_API aether_bool_t aether_transform_list_equal(const aether_transform_list* left,
                                                       const aether_transform_list* right) {
    if (!left || !right || left->count < 0 || right->count < 0 || left->count != right->count)
        return AETHER_FALSE;
    if (left->count == 0) return AETHER_TRUE;
    if (!left->items || !right->items) return AETHER_FALSE;
    return std::memcmp(left->items, right->items,
        sizeof(aether_transform_operation) * static_cast<size_t>(left->count)) == 0 ? AETHER_TRUE : AETHER_FALSE;
}

AETHER_API aether_bool_t aether_backdrop_filter_list_copy(const aether_backdrop_filter_list* source,
                                                            aether_backdrop_filter_list* destination) {
    if (!destination) return AETHER_FALSE;
    if (source == destination) return AETHER_TRUE;
    if (!source || source->count < 0 || (source->count > 0 && !source->items)) {
        aether_backdrop_filter_list_free(destination);
        return AETHER_FALSE;
    }
    aether_backdrop_filter_list replacement{};
    if (source->count > 0) {
        const auto count = static_cast<size_t>(source->count);
        if (count > (std::numeric_limits<size_t>::max)() / sizeof(aether_backdrop_filter)) {
            aether_backdrop_filter_list_free(destination);
            return AETHER_FALSE;
        }
        replacement.items = static_cast<aether_backdrop_filter*>(std::malloc(sizeof(aether_backdrop_filter) * count));
        if (!replacement.items) { aether_backdrop_filter_list_free(destination); return AETHER_FALSE; }
        std::memcpy(replacement.items, source->items, sizeof(aether_backdrop_filter) * count);
        replacement.count = source->count;
    }
    aether_backdrop_filter_list_free(destination);
    *destination = replacement;
    return AETHER_TRUE;
}

AETHER_API aether_bool_t aether_backdrop_filter_list_equal(const aether_backdrop_filter_list* left,
                                                             const aether_backdrop_filter_list* right) {
    if (!left || !right || left->count < 0 || right->count < 0 || left->count != right->count) return AETHER_FALSE;
    if (left->count == 0) return AETHER_TRUE;
    if (!left->items || !right->items) return AETHER_FALSE;
    return std::memcmp(left->items, right->items,
        sizeof(aether_backdrop_filter) * static_cast<size_t>(left->count)) == 0 ? AETHER_TRUE : AETHER_FALSE;
}

AETHER_API aether_bool_t aether_background_layer_list_copy(const aether_background_layer_list* source,
                                                            aether_background_layer_list* destination) {
    if (!destination) return AETHER_FALSE;
    if (source == destination) return AETHER_TRUE;
    if (!source || source->count < 0 || (source->count > 0 && !source->items)) {
        aether_background_layer_list_free(destination);
        return AETHER_FALSE;
    }

    aether_background_layer_list replacement{};
    if (source->count > 0) {
        const size_t count = static_cast<size_t>(source->count);
        if (count > (std::numeric_limits<size_t>::max)() / sizeof(aether_background_layer)) {
            aether_background_layer_list_free(destination);
            return AETHER_FALSE;
        }
        replacement.items = static_cast<aether_background_layer*>(std::calloc(count, sizeof(aether_background_layer)));
        if (!replacement.items) {
            aether_background_layer_list_free(destination);
            return AETHER_FALSE;
        }
        replacement.count = source->count;
        for (int32_t index = 0; index < source->count; ++index) {
            if (!copy_background_layer(source->items[index], &replacement.items[index])) {
                aether_background_layer_list_free(&replacement);
                aether_background_layer_list_free(destination);
                return AETHER_FALSE;
            }
        }
    }

    aether_background_layer_list_free(destination);
    *destination = replacement;
    return AETHER_TRUE;
}

AETHER_API aether_bool_t aether_background_layer_list_equal(const aether_background_layer_list* left,
                                                             const aether_background_layer_list* right) {
    if (!left || !right || left->count < 0 || right->count < 0 || left->count != right->count)
        return AETHER_FALSE;
    if (left->count == 0) return AETHER_TRUE;
    if (!left->items || !right->items) return AETHER_FALSE;
    for (int32_t index = 0; index < left->count; ++index) {
        if (!background_layers_equal(left->items[index], right->items[index])) return AETHER_FALSE;
    }
    return AETHER_TRUE;
}

AETHER_API aether_bool_t aether_shadow_list_copy(const aether_shadow_list* source,
                                                  aether_shadow_list* destination) {
    if (!destination) return AETHER_FALSE;
    if (source == destination) return AETHER_TRUE;
    if (!source || source->count < 0 || (source->count > 0 && !source->items)) {
        aether_shadow_list_free(destination);
        return AETHER_FALSE;
    }

    aether_shadow_list replacement{};
    if (source->count > 0) {
        const size_t count = static_cast<size_t>(source->count);
        if (count > (std::numeric_limits<size_t>::max)() / sizeof(aether_shadow)) {
            aether_shadow_list_free(destination);
            return AETHER_FALSE;
        }
        const size_t bytes = sizeof(aether_shadow) * count;
        replacement.items = static_cast<aether_shadow*>(malloc(bytes));
        if (!replacement.items) {
            aether_shadow_list_free(destination);
            return AETHER_FALSE;
        }
        memcpy(replacement.items, source->items, bytes);
        replacement.count = source->count;
    }

    aether_shadow_list_free(destination);
    *destination = replacement;
    return AETHER_TRUE;
}

AETHER_API aether_bool_t aether_shadow_list_equal(const aether_shadow_list* left,
                                                   const aether_shadow_list* right) {
    if (!left || !right || left->count < 0 || right->count < 0) return AETHER_FALSE;
    if (left->count != right->count) return AETHER_FALSE;
    if (left->count == 0) return AETHER_TRUE;
    if (!left->items || !right->items) return AETHER_FALSE;
    return memcmp(left->items, right->items,
                  sizeof(aether_shadow) * static_cast<size_t>(left->count)) == 0
        ? AETHER_TRUE : AETHER_FALSE;
}

} // extern "C"
