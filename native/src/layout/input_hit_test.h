#pragma once
#include "scrollbar_geometry.h"

struct layout_input_hit_t {
    aether_input_hit_part_t part = AETHER_INPUT_HIT_NONE;
    aether_node_s* owner = nullptr;
};

// Reverse the painter's local scopes: own chrome, children, own content.
inline layout_input_hit_t layout_hit_test_input(const aether_layout_result_s* layout,
    const aether_layout_box_s* box, const StyleLookup& styles, BoxStylePolicy policy, float x, float y,
    aether_scroll_offset_fn offsets, void* data) {
    if (!box || (layout && layout->assets && !layout->assets->structure_valid()) || !layout_box_input_alive(box) || !layout_box_unmap_visual(box, x, y)) return {};
    const auto style_view = layout_box_style(box, styles, policy);
    const auto& style = style_view.scalars();
    if (style.display == AETHER_DISPLAY_NONE) return {};
    const auto& presentation = layout_box_presentation_style(box, styles, policy);
    auto* owner = layout_box_hit_owner(box);
    const bool eligible = owner && presentation.visible &&
        presentation.pointer_events != AETHER_POINTER_EVENTS_NONE;
    const bool inside_clip = (style.overflow_x == AETHER_OVERFLOW_VISIBLE || (x >= box->padding_box.x && x <= box->padding_box.x + box->padding_box.width)) && (style.overflow_y == AETHER_OVERFLOW_VISIBLE || (y >= box->padding_box.y && y <= box->padding_box.y + box->padding_box.height));
    aether_scroll_offset_t offset{box->scroll.scroll_offset_x, box->scroll.scroll_offset_y};
    if (offsets && box->element) offset = offsets(box->element, data);
    offset.x = std::clamp(offset.x, 0.f, box->scroll.scrollable_overflow_x);
    offset.y = std::clamp(offset.y, 0.f, box->scroll.scrollable_overflow_y);
    if (inside_clip) {
        if (eligible && box->kind != LAYOUT_BOX_TEXT &&
            box->border_box.width > 0.f && box->border_box.height > 0.f) {
            const auto g = aether_resolve_scrollbar_geometry(*box, offset);
            const auto contains = [x,y](aether_rectf rect) {
                return rect.width > 0.f && rect.height > 0.f && aether_rectf_contains(rect, {x,y});
            };
            if (g.horizontal_visible && contains(g.horizontal_thumb)) return {AETHER_INPUT_HIT_HORIZONTAL_THUMB, owner};
            if (g.vertical_visible && contains(g.vertical_thumb)) return {AETHER_INPUT_HIT_VERTICAL_THUMB, owner};
            if (g.horizontal_visible && contains(g.horizontal_track)) return {AETHER_INPUT_HIT_HORIZONTAL_TRACK, owner};
            if (g.vertical_visible && contains(g.vertical_track)) return {AETHER_INPUT_HIT_VERTICAL_TRACK, owner};
            if (g.horizontal_visible && g.vertical_visible && contains(g.corner)) return {AETHER_INPUT_HIT_SCROLLBAR_CORNER, owner};
        }
        if (!box->is_atomic_control && box->kind != LAYOUT_BOX_TEXT) {
            const bool dynamic = layout_box_has_dynamic_scroll(layout, box);
            const float child_x = x + (dynamic ? offset.x : 0.f);
            const float child_y = y + (dynamic ? offset.y : 0.f);
            const auto visit = [&](const aether_layout_box_s* child) {
                return layout_hit_test_input(layout, child, styles, policy, child_x, child_y, offsets, data);
            };
            const auto order = layout_hit_order(box, styles, policy);
            if (order.empty()) {
                for (auto it = box->children.rbegin(); it != box->children.rend(); ++it) {
                    const auto hit = visit(*it); if (hit.part != AETHER_INPUT_HIT_NONE) return hit;
                }
            } else {
                for (auto it = order.rbegin(); it != order.rend(); ++it) {
                    const auto hit = visit(box->children[it->dom_index]); if (hit.part != AETHER_INPUT_HIT_NONE) return hit;
                }
            }
        }
    }
    if (eligible && layout_box_contains_own_content(box, x, y)) return {AETHER_INPUT_HIT_CONTENT, owner};
    return {};
}
