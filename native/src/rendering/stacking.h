#pragma once
#include <aether/aether.h>
#include "../css/style_read_view.h"

#include <cstdint>
#include <vector>

/* Local child z-index stacking: the generic engine capability that reorders paint
   emission within a box's children according to the CSS stacking rules.
   The module is self-contained (only the public aether.h types + the STL) so
   it can be unit-tested standalone. When no child carries an explicit
   z-index, the caller keeps exact DOM order and this module is inert. */

namespace aether {
namespace stacking {

/* The four CSS paint classes of a stacking context's descendants, painted in
   ascending enum order:
     negative -> in_flow -> auto -> positive. */
enum class paint_class_t : int32_t {
    negative = 0, /* positioned, z-index integer < 0, ascending by value */
    in_flow = 1,  /* non-positioned boxes, text and atomic inline items, DOM order */
    auto_ = 2,    /* positioned, z-index auto or 0, DOM order */
    positive = 3, /* positioned, z-index > 0, ascending by value */
};

/* Result of classifying one element box for stacking-order emission. */
struct paint_classification_t {
    paint_class_t cls = paint_class_t::in_flow;
    int32_t z_value = 0;                /* meaningful for NEGATIVE/POSITIVE */
    bool has_explicit_z_index = false;  /* declared integer z-index (non-auto) */
};

/* One child of a container, annotated for stable stacking-order emission.
   dom_index is the child's index in the container's DOM-ordered children. */
struct stack_item_t {
    int32_t dom_index = 0;
    bool is_inline_content = false;  /* text runs / atomic inline items */
    paint_class_t cls = paint_class_t::in_flow;
    int32_t z_value = 0;
    bool has_explicit_z_index = false;
};

/* True when the element creates a stacking context (CSS 2.1 §9.9 common
   triggers): positioned with an explicit integer z-index, position:fixed,
   opacity < 1, a non-empty transform list, or a non-empty backdrop-filter
   list. v1 covers the common set. */
bool creates_stacking_context(const aether_computed_style_t& style);
bool creates_stacking_context(const style::StyleScalars& style, bool has_transforms, bool has_backdrop_filters);

/* Classifies an element box's paint phase within its parent. Text boxes and
   atomic inline items are inline content and always classify as IN_FLOW. */
paint_classification_t classify_element_paint(const aether_computed_style_t& style);
paint_classification_t classify_element_paint(const style::StyleScalars& style);

/* True when any element child carries an explicit (non-auto) z-index, so the
   container must be emitted in stacking order instead of exact DOM order. */
bool needs_stacking_order(const std::vector<stack_item_t>& items);

/* Stable sort by (paint class, z-index value, DOM index). DOM order breaks
   all ties, so equal items keep their exact document order. */
void stable_sort_stack_items(std::vector<stack_item_t>& items);

}  // namespace stacking
}  // namespace aether
