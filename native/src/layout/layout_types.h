#pragma once
#include <aether/aether.h>
#include <cassert>
#include <cstddef>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "../css/style_map_types.h"
#include "../dom/dom_types.h"

/* ── Internal layout types (mirrors C# LayoutBox/LayoutResult) ──── */

enum layout_box_kind_t : int32_t {
    LAYOUT_BOX_ROOT = 0,
    LAYOUT_BOX_ELEMENT,
    LAYOUT_BOX_TEXT,
};

struct aether_layout_result_s;
struct aether_style_map_s;

struct aether_layout_box_s {
    aether_layout_result_s* owner = nullptr;
    layout_box_kind_t kind = LAYOUT_BOX_ELEMENT;
    aether_node_s*    element = nullptr;
    aether_computed_style_t style{};
    std::vector<aether_layout_box_s*> children;
    std::string text;
    bool  is_atomic_control = false;
    aether_scroll_metrics_t scroll{};
    // Used vertical margins visible to the enclosing block formatting context.
    // They begin as the specified margins and may be collapsed through children.
    float collapsed_margin_before = 0.f;
    float collapsed_margin_after = 0.f;

    aether_rectf margin_box{};
    aether_rectf border_box{};
    aether_rectf padding_box{};
    aether_rectf content_box{};

    ~aether_layout_box_s();

    void add_child(aether_layout_box_s* child) { children.push_back(child); }

    /* recursive descendant+self count (matches C# DescendantsAndSelf) */
    int descendants_and_self() const {
        int n = 1;
        for (auto* c : children) n += c->descendants_and_self();
        return n;
    }
};

// Normal boxes, including pseudo-elements, use their own current computed
// style and fall back to the layout snapshot when the map has no entry.
inline const aether_computed_style_t& layout_box_style(
    const aether_layout_box_s* box, const aether_style_map_s* styles)
{
    if (styles && box->element) {
        const auto found = styles->styles.find(box->element);
        if (found != styles->styles.end()) return found->second;
    }
    return box->style;
}

// Only visibility and pointer-events use this selection. Anonymous wrappers
// must not borrow their owner's decorations, transforms, opacity or flex style.
inline const aether_computed_style_t& layout_box_presentation_style(
    const aether_layout_box_s* box, const aether_style_map_s* styles)
{
    if (box->element && box->element->anonymous_flex_owner) {
        if (styles) {
            const auto found = styles->styles.find(box->element->anonymous_flex_owner);
            if (found != styles->styles.end()) return found->second;
        }
        return box->style;
    }
    return layout_box_style(box, styles);
}

inline aether_node_s* layout_box_hit_owner(const aether_layout_box_s* box)
{
    return box->element && box->element->anonymous_flex_owner
        ? box->element->anonymous_flex_owner : box->element;
}

struct inline_item_s {
    aether_node_s*        element = nullptr;
    aether_computed_style_t style{};
    std::string           text;
    bool is_atomic = false;
    bool is_line_break = false;

    static inline_item_s text_item(aether_node_s* el, const aether_computed_style_t& s,
                                   const std::string& t) {
        inline_item_s i;
        i.element = el;
        i.style = s;
        i.text = t;
        return i;
    }
    static inline_item_s atomic(aether_node_s* el, const aether_computed_style_t& s) {
        inline_item_s i;
        i.element = el;
        i.style = s;
        i.is_atomic = true;
        return i;
    }
    static inline_item_s line_break(aether_node_s* el, const aether_computed_style_t& s) {
        inline_item_s i;
        i.element = el;
        i.style = s;
        i.is_line_break = true;
        return i;
    }
};

struct aether_layout_result_s {
    aether_layout_box_s* root = nullptr;
    // Borrowed for the lifetime of a layout result.  Rendering needs the
    // document element's style to apply CSS canvas-background propagation even
    // though layout intentionally starts at <body>.
    aether_style_map_s* style_map = nullptr;
    std::unordered_map<aether_node_s*, aether_layout_box_s*> primary_boxes;
    std::vector<aether_layout_box_s*> paint_order;
    // Anonymous flex items are layout-only nodes.  They must outlive their
    // boxes and display-list construction, but never become DOM-observable.
    aether_style_map_s* layout_only_style_map = nullptr;
    std::vector<aether_node_s*> layout_only_nodes;
    std::vector<aether_node_s*> layout_only_style_nodes;
    // One anonymous-item sequence per flex container.  Intrinsic measurement
    // and final layout reuse these exact nodes, avoiding duplicate synthetic
    // DOM ownership during provisional passes.
    std::unordered_map<aether_node_s*, std::vector<aether_node_s*>> anonymous_flex_items;
    aether_sizef viewport{};
    // CSS Overflow 3.3: the viewport's used overflow, propagated from the root
    // (or its first body child). The root box's scroll metrics use these while
    // the root's own style keeps its used value for paint and chrome.
    int32_t viewport_overflow_x = AETHER_OVERFLOW_VISIBLE;
    int32_t viewport_overflow_y = AETHER_OVERFLOW_VISIBLE;
    // Measurement passes can build detached trees. Only final-layout boxes are
    // published through the query and paint indexes.
    bool records_boxes = true;
    size_t live_box_count = 0;

    ~aether_layout_result_s();

    aether_layout_box_s* create_box();
    // Published boxes may be replaced by a later layout pass. Destruction must
    // remove the exact instance, including text fragments that have no primary
    // box entry, before its storage can be reused.
    void unregister_box(aether_layout_box_s* box) {
        paint_order.erase(std::remove(paint_order.begin(), paint_order.end(), box), paint_order.end());
        if (box->element) {
            const auto it = primary_boxes.find(box->element);
            if (it != primary_boxes.end() && it->second == box) primary_boxes.erase(it);
        }
    }
    void release_box() {
        assert(live_box_count > 0);
        --live_box_count;
    }

    aether_layout_box_s* find_primary_box(aether_node_s* element) const {
        auto it = primary_boxes.find(element);
        return it != primary_boxes.end() ? it->second : nullptr;
    }

    aether_node_s* hit_test(float x, float y) const {
        if (!root) return nullptr;
        return hit_test_recursive(root, x, y, nullptr, nullptr, nullptr);
    }

    aether_node_s* hit_test(float x, float y, aether_scroll_offset_fn offsets, void* data) const {
        if (!root) return nullptr;
        return hit_test_recursive(root, x, y, nullptr, offsets, data);
    }

    aether_node_s* hit_test(float x, float y, const aether_style_map_s* styles,
                            aether_scroll_offset_fn offsets, void* data) const {
        if (!root) return nullptr;
        return hit_test_recursive(root, x, y, styles, offsets, data);
    }

private:
    static aether_node_s* hit_test_recursive(aether_layout_box_s* box, float x, float y,
                                              const aether_style_map_s* styles,
                                              aether_scroll_offset_fn offsets, void* data) {
        const auto& style = layout_box_style(box, styles);
        if (style.display == AETHER_DISPLAY_NONE) return nullptr;
        const auto& presentation = layout_box_presentation_style(box, styles);
        // A scroll offset moves only descendants; the scroll container's own
        // border/padding box remains in viewport coordinates.
        float child_x = x;
        float child_y = y;
        const bool scroll_container = box->element &&
            (box->scroll.scrollable_overflow_x > 0.f || box->scroll.scrollable_overflow_y > 0.f);
        if (scroll_container && offsets) {
            const auto requested = offsets(box->element, data);
            child_x += std::clamp(requested.x, 0.f, box->scroll.scrollable_overflow_x);
            child_y += std::clamp(requested.y, 0.f, box->scroll.scrollable_overflow_y);
        }
        const bool clips_children =
            style.overflow_x != AETHER_OVERFLOW_VISIBLE ||
            style.overflow_y != AETHER_OVERFLOW_VISIBLE;
        if (!clips_children || aether_rectf_contains(box->padding_box, {x, y})) {
            for (int i = static_cast<int>(box->children.size()) - 1; i >= 0; --i) {
                auto* found = hit_test_recursive(box->children[i], child_x, child_y, styles, offsets, data);
                if (found) return found;
            }
        }
        if (presentation.visible && presentation.pointer_events != AETHER_POINTER_EVENTS_NONE &&
            aether_rectf_contains(box->margin_box, {x, y})) return layout_box_hit_owner(box);
        return nullptr;
    }
};

/* ── Layout engine (stateless; mutable per-layout scratch) ──────── */

struct aether_layout_engine_s {
    aether_text_measurer_fn text_measurer = nullptr;
    void*                   text_measurer_data = nullptr;
    aether_scroll_offset_fn scroll_offset_fn = nullptr;
    void*                   scroll_offset_data = nullptr;
    float                   root_font_size = 16.f;
    float                   viewport_width = 0.f;
    float                   viewport_height = 0.f;
};

/* Internal text measurement wrapper */
inline aether_text_metrics_t engine_measure_text(
    const aether_layout_engine_s* e,
    const char* text,
    const aether_computed_style_t* style,
    float max_width)
{
    if (e && e->text_measurer)
        return e->text_measurer(text, style, max_width, e->text_measurer_data);
    return {0.f, 0.f, 0.f};
}
