#pragma once
#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "../css/style_map_types.h"
#include "../css/measure_state.h"
#include "../dom/dom_types.h"
#include "../rendering/stacking.h"

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
    std::string control_display_text;
    bool control_shows_placeholder = false;
    aether_scroll_metrics_t scroll{};
    bool visual_sample_valid = false;
    bool visual_has_transform = false;
    aether_transform_matrix visual_transform{1,0,0,1,0,0};
    float visual_opacity = 1.f;
    int64_t visual_timestamp = 0;
    std::weak_ptr<node_identity_s> visual_identity;

    // Used vertical margins visible to the enclosing block formatting context.
    // They begin as the specified margins and may be collapsed through children.
    float collapsed_margin_before = 0.f;
    float collapsed_margin_after = 0.f;

    // Only this box's relative inset; ancestor and placement translations are excluded.
    float own_relative_dx = 0.f;
    float own_relative_dy = 0.f;

    aether_thicknessf used_border{};
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

// Empty means exact DOM order. Classification is shared by paint and hit testing,
// including children later omitted for visibility, display or pointer events.
inline std::vector<aether::stacking::stack_item_t> layout_children_paint_order(
    const aether_layout_box_s* box, const aether_style_map_s* styles)
{
    using namespace aether::stacking;
    const auto classify = [styles](const aether_layout_box_s* child) {
        return child->kind == LAYOUT_BOX_TEXT || child->is_atomic_control
            ? paint_classification_t{} : classify_element_paint(layout_box_style(child, styles));
    };
    bool needs_order = false;
    for (auto* child : box->children) {
        if (classify(child).has_explicit_z_index) { needs_order = true; break; }
    }
    if (!needs_order) return {};
    std::vector<stack_item_t> items;
    items.reserve(box->children.size());
    for (size_t index = 0; index < box->children.size(); ++index) {
        auto* child = box->children[index];
        const auto classification = classify(child);
        stack_item_t item;
        item.dom_index = static_cast<int32_t>(index);
        item.is_inline_content = child->kind == LAYOUT_BOX_TEXT || child->is_atomic_control;
        item.cls = classification.cls;
        item.z_value = classification.z_value;
        item.has_explicit_z_index = classification.has_explicit_z_index;
        items.push_back(item);
    }
    stable_sort_stack_items(items);
    return items;
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

// Every input path reverses the exact immutable sample consumed by paint.
inline bool layout_box_unmap_visual(const aether_layout_box_s* box, float& x, float& y) {
    if (!box->visual_sample_valid || !box->visual_has_transform) return true;
    const auto& m = box->visual_transform;
    const double determinant = double(m.m11)*m.m22-double(m.m12)*m.m21;
    if (!std::isfinite(determinant) || determinant == 0) return false;
    const double px=double(x)-m.dx, py=double(y)-m.dy;
    const double nx=(px*m.m22-py*m.m21)/determinant;
    const double ny=(py*m.m11-px*m.m12)/determinant;
    if (!std::isfinite(nx)||!std::isfinite(ny)) return false;
    x=float(nx);y=float(ny);return std::isfinite(x)&&std::isfinite(y);
}
inline bool layout_box_input_alive(const aether_layout_box_s* box) {
    if (!box->visual_sample_valid || !box->element) return true;
    auto identity=box->visual_identity.lock();
    return identity && identity->alive && identity->attached;
}
// Own ordinary input area. Keep the existing inclusive/degenerate rect semantics.
// This predicate is deliberately independent of traversal, clipping and eligibility.
inline bool layout_box_contains_own_content(const aether_layout_box_s* box, float x, float y)
{
    return aether_rectf_contains(box->border_box, {x, y});
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
    size_t references = 1;
    aether_layout_result_s* retained_storage_owner = nullptr;
    aether_layout_box_s* root = nullptr;
    // Immutable effective context for this result, installed before all passes.
    bool effective_context_enabled = false;
    std::unordered_set<aether_node_s*> effective_fixed_container_nodes;
    // Independently owned immutable revision backing. Anonymous layout-only
    // styles are added before publication; caller map updates never affect it.
    // The document element remains available for canvas-background propagation.
    std::unique_ptr<aether_style_map_s> owned_style_map;
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
    float root_font_size = 16.f;
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
    bool abandoned = false;
    std::unordered_set<aether_layout_box_s*> allocated_boxes;

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

    bool accepts_style_map(const aether_style_map_s* override_styles) const noexcept {
        if (!override_styles || !style_map || !style_map->contextual) return true;
        return override_styles->contextual &&
            override_styles->revision == style_map->revision &&
            override_styles->document_identity == style_map->document_identity &&
            override_styles->generation == style_map->generation;
    }

    aether_node_s* hit_test(float x, float y) const {
        if (!root || !aether_style_map_structure_valid(style_map)) return nullptr;
        return hit_test_recursive(root, x, y, nullptr, nullptr, nullptr);
    }

    aether_node_s* hit_test(float x, float y, aether_scroll_offset_fn offsets, void* data) const {
        if (!root || !aether_style_map_structure_valid(style_map)) return nullptr;
        return hit_test_recursive(root, x, y, nullptr, offsets, data);
    }

    aether_node_s* hit_test(float x, float y, const aether_style_map_s* styles,
                            aether_scroll_offset_fn offsets, void* data) const {
        if (!root || !aether_style_map_structure_valid(style_map) || !accepts_style_map(styles)) return nullptr;
        return hit_test_recursive(root, x, y, styles, offsets, data);
    }

private:
    static aether_node_s* hit_test_recursive(aether_layout_box_s* box, float x, float y,
                                              const aether_style_map_s* styles,
                                              aether_scroll_offset_fn offsets, void* data) {
        if (!layout_box_input_alive(box) || !layout_box_unmap_visual(box, x, y)) return nullptr;
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
        if (!clips_children || ((style.overflow_x == AETHER_OVERFLOW_VISIBLE || (x >= box->padding_box.x && x <= box->padding_box.x + box->padding_box.width)) && (style.overflow_y == AETHER_OVERFLOW_VISIBLE || (y >= box->padding_box.y && y <= box->padding_box.y + box->padding_box.height)))) {
            const auto order = layout_children_paint_order(box, styles);
            if (order.empty()) {
                for (auto it = box->children.rbegin(); it != box->children.rend(); ++it) {
                    if (auto* found = hit_test_recursive(*it, child_x, child_y, styles, offsets, data)) return found;
                }
            } else {
                for (auto it = order.rbegin(); it != order.rend(); ++it) {
                    if (auto* found = hit_test_recursive(box->children[it->dom_index], child_x, child_y, styles, offsets, data)) return found;
                }
            }
        }
        if (presentation.visible && presentation.pointer_events != AETHER_POINTER_EVENTS_NONE &&
            layout_box_contains_own_content(box, x, y)) return layout_box_hit_owner(box);
        return nullptr;
    }
};

/* ── Layout engine (stateless; mutable per-layout scratch) ──────── */

struct aether_layout_engine_s {
    bool layout_in_progress = false;
    const aether_style_map_s* measure_styles = nullptr;
    // One-shot owned scratch. No runtime/map pointer survives prepare.
    bool transition_context_prepared = false;
    std::unordered_set<aether_node_s*> prepared_active_nodes;
    aether_text_measurer_contextual_fn contextual_text_measurer = nullptr;
    void* contextual_text_measurer_data = nullptr;
    aether_text_measurer_fn text_measurer = nullptr;
    void*                   text_measurer_data = nullptr;
    aether_scroll_offset_fn scroll_offset_fn = nullptr;
    void*                   scroll_offset_data = nullptr;
    float                   root_font_size = 16.f;
    float                   viewport_width = 0.f;
    float                   viewport_height = 0.f;
};

bool aether_overflow_is_scroll_container(int32_t overflow);

// Shared paint/input scope policy. Geometry and dynamic-scroll eligibility use
// the layout snapshot, including propagated viewport overflow.
inline bool layout_box_clips(const aether_computed_style_t& style) {
    return style.overflow_x != AETHER_OVERFLOW_VISIBLE || style.overflow_y != AETHER_OVERFLOW_VISIBLE;
}
inline int32_t layout_scroll_overflow_axis(const aether_layout_result_s* layout,
    const aether_layout_box_s* box, bool horizontal) {
    if (layout && layout->root == box)
        return horizontal ? layout->viewport_overflow_x : layout->viewport_overflow_y;
    return horizontal ? box->style.overflow_x : box->style.overflow_y;
}
inline bool layout_box_has_dynamic_scroll(const aether_layout_result_s* layout,
    const aether_layout_box_s* box) {
    return box->element &&
        ((aether_overflow_is_scroll_container(layout_scroll_overflow_axis(layout, box, true)) &&
          box->scroll.scrollable_overflow_x > 0.f) ||
         (aether_overflow_is_scroll_container(layout_scroll_overflow_axis(layout, box, false)) &&
          box->scroll.scrollable_overflow_y > 0.f));
}

/* Internal text measurement wrapper */
inline aether_text_metrics_t engine_measure_text(
    const aether_layout_engine_s* e,
    const char* text,
    const aether_computed_style_t* style,
    float max_width, aether_node_s* node = nullptr)
{
    const auto lifetime = e && e->measure_styles ? e->measure_styles->document_lifetime : nullptr;
    const auto epoch = lifetime ? lifetime->mutation_epoch : 0;
    const auto style_epoch = lifetime ? lifetime->style_tree_epoch : 0;
    const auto checked = [&](aether_text_metrics_t metrics) {
        if (!std::isfinite(metrics.width) || !std::isfinite(metrics.height) || !std::isfinite(metrics.baseline) || metrics.width < 0.f || metrics.height < 0.f)
            throw std::overflow_error("Invalid text metrics");
        if (lifetime && (!lifetime->alive || lifetime->mutation_epoch != epoch || lifetime->style_tree_epoch != style_epoch))
            throw std::runtime_error("DOM mutated during text measurement");
        return metrics;
    };
    if (e && e->contextual_text_measurer) {
        const measure_state_s* measures = nullptr;
        if (node && e->measure_styles) {
            const auto it = e->measure_styles->measures.find(node);
            if (it != e->measure_styles->measures.end()) measures = it->second.get();
        }
        const float used_line_height = measure_used_line_height(measures, *style);
        if (!std::isfinite(used_line_height)) throw std::overflow_error("Non-finite callback line height");
        return checked(e->contextual_text_measurer(text, style, used_line_height, max_width, e->contextual_text_measurer_data));
    }
    if (e && e->text_measurer)
        return checked(e->text_measurer(text, style, max_width, e->text_measurer_data));
    return {0.f, 0.f, 0.f};
}
