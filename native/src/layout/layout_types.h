#pragma once
#include <aether/aether.h>
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>

/* ── Internal layout types (mirrors C# LayoutBox/LayoutResult) ──── */

enum layout_box_kind_t : int32_t {
    LAYOUT_BOX_ROOT = 0,
    LAYOUT_BOX_ELEMENT,
    LAYOUT_BOX_TEXT,
};

struct aether_layout_result_s;

struct aether_layout_box_s {
    aether_layout_result_s* owner = nullptr;
    layout_box_kind_t kind = LAYOUT_BOX_ELEMENT;
    aether_node_s*    element = nullptr;
    aether_computed_style_t style{};
    std::vector<aether_layout_box_s*> children;
    std::string text;
    bool  is_atomic_control = false;
    float scrollable_overflow_y = 0.f;
    float scroll_offset_y = 0.f;
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
    std::unordered_map<aether_node_s*, aether_layout_box_s*> primary_boxes;
    std::vector<aether_layout_box_s*> paint_order;
    aether_sizef viewport{};
    // Measurement passes can build detached trees. Only final-layout boxes are
    // published through the query and paint indexes.
    bool records_boxes = true;
    size_t live_box_count = 0;

    ~aether_layout_result_s() {
        delete root;
        assert(live_box_count == 0 && "all layout boxes must be owned by the final tree or released explicitly");
    }

    aether_layout_box_s* create_box();
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
        return hit_test_recursive(root, x, y);
    }

private:
    static aether_node_s* hit_test_recursive(aether_layout_box_s* box, float x, float y) {
        for (int i = static_cast<int>(box->children.size()) - 1; i >= 0; --i) {
            auto* found = hit_test_recursive(box->children[i], x, y);
            if (found) return found;
        }
        if (aether_rectf_contains(box->margin_box, {x, y})) {
            return box->element;
        }
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
