#include "stacking.h"

#include <algorithm>

namespace aether {
namespace stacking {

bool creates_stacking_context(const aether_computed_style_t& style) {
    const bool positioned = style.position == AETHER_POSITION_RELATIVE ||
                            style.position == AETHER_POSITION_ABSOLUTE ||
                            style.position == AETHER_POSITION_FIXED;
    if (positioned && style.z_index_kind == AETHER_Z_INDEX_INTEGER) return true;
    if (style.position == AETHER_POSITION_FIXED) return true;
    if (style.opacity < 1.0f) return true;
    if (style.transforms.count > 0) return true;
    if (style.backdrop_filters.count > 0) return true;
    return false;
}

paint_classification_t classify_element_paint(const aether_computed_style_t& style) {
    paint_classification_t result;
    if (style.position == AETHER_POSITION_STATIC) {
        result.cls = paint_class_t::in_flow;
        return result;
    }
    if (style.z_index_kind == AETHER_Z_INDEX_INTEGER) {
        result.has_explicit_z_index = true;
        result.z_value = style.z_index;
        if (result.z_value < 0) result.cls = paint_class_t::negative;
        else if (result.z_value > 0) result.cls = paint_class_t::positive;
        else result.cls = paint_class_t::auto_;
        return result;
    }
    /* Positioned with z-index auto paints in the AUTO phase at its DOM
       position, after all in-flow content. */
    result.cls = paint_class_t::auto_;
    return result;
}

bool needs_stacking_order(const std::vector<stack_item_t>& items) {
    for (const auto& item : items) {
        if (item.has_explicit_z_index) return true;
    }
    return false;
}

void stable_sort_stack_items(std::vector<stack_item_t>& items) {
    std::stable_sort(items.begin(), items.end(),
                     [](const stack_item_t& a, const stack_item_t& b) {
                         if (a.cls != b.cls) {
                             return static_cast<int32_t>(a.cls) <
                                    static_cast<int32_t>(b.cls);
                         }
                         if (a.z_value != b.z_value) return a.z_value < b.z_value;
                         return a.dom_index < b.dom_index;
                     });
}

}  // namespace stacking
}  // namespace aether
