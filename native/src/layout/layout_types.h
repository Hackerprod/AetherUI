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
#include "operation_pin.h"
#include "../css/style_c_export.h"
using aether::layout::BoxStyleSnapshot;
using aether::layout::StyleLookup;
using aether::layout::BoxStylePolicy;
using aether::layout::StyleOperationPin;
using aether::layout::LayoutOperationPin;
using aether::layout::CallbackEpochGuard;
#include "../css/measure_state.h"
#include "../dom/dom_types.h"
#include "../rendering/stacking.h"

/* ── Internal layout types (mirrors C# LayoutBox/LayoutResult) ──── */

enum layout_box_kind_t : int32_t {
    LAYOUT_BOX_ROOT = 0,
    LAYOUT_BOX_ELEMENT,
    LAYOUT_BOX_TEXT,
};

// Construction-only facts for table-related effective-fixed overflow repair.
// Text boxes keep these invalid. No style pointers or callback samples are retained.
enum layout_scroll_completion_flag : uint8_t {
    LAYOUT_SCROLL_VALID = 1,
    LAYOUT_SCROLL_X = 2,
    LAYOUT_SCROLL_Y = 4,
    LAYOUT_SCROLL_TABLE_CELL = 8,
};

struct aether_layout_result_s;
struct aether_style_map_s;
struct aether_layout_box_s;

// Allocation ownership is independent of the tree and published paint indexes.
// This view has no allocating operations; only the result can change membership.
class LayoutBoxRegistry {
public:
    class iterator {
    public:
        aether_layout_box_s* operator*() const noexcept { return box_; }
        iterator& operator++() noexcept;
        bool operator==(iterator other) const noexcept { return box_ == other.box_; }
        bool operator!=(iterator other) const noexcept { return !(*this == other); }
    private:
        friend class LayoutBoxRegistry;
        explicit iterator(aether_layout_box_s* box) noexcept : box_(box) {}
        aether_layout_box_s* box_;
    };
    LayoutBoxRegistry() noexcept = default;
    LayoutBoxRegistry(const LayoutBoxRegistry&) = delete;
    LayoutBoxRegistry& operator=(const LayoutBoxRegistry&) = delete;
    LayoutBoxRegistry(LayoutBoxRegistry&&) = delete;
    LayoutBoxRegistry& operator=(LayoutBoxRegistry&&) = delete;
    iterator begin() const noexcept { return iterator(head_); }
    iterator end() const noexcept { return iterator(nullptr); }
    bool empty() const noexcept { return head_ == nullptr; }
private:
    friend struct aether_layout_result_s;
    void link(aether_layout_box_s* box) noexcept;
    void unlink(aether_layout_box_s* box) noexcept;
    aether_layout_box_s* head_ = nullptr;
};

// Flow-construction state only: offsets refer to the explicit flow content origin.
// Node identities are pinned by the result assets; never retain a box/style pointer.
struct block_static_anchor_s {
    aether_node_s* node = nullptr;
    aether_node_s* cursor_owner = nullptr;
    float inline_offset = 0.f;
    float block_offset = 0.f;
};

// Own horizontal scroll contribution only. Keep the original x origin rather
// than a delta from a distant fragment, preserving a small hanging edge.
// Vertical bounds always come from current physical content geometry: storing
// a distant y origin would lose height when a large placement cancels to zero.
struct layout_scroll_proxy_s {
    double left = 0., right = 0.;
    float reference_content_x = 0.f;
    bool valid = false;
};

struct aether_layout_box_s {
    aether_layout_result_s* owner = nullptr;
    layout_box_kind_t kind = LAYOUT_BOX_ELEMENT;
    aether_node_s*    element = nullptr;
    BoxStyleSnapshot style{};
    aether_node_s* presentation_owner = nullptr;
    std::vector<aether_layout_box_s*> children;
    // Cleared by positioned completion, including deferred table-cell completion.
    // Published snapshots and clones never contain logical construction records.
    std::vector<block_static_anchor_s> block_static_anchors;
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
    layout_scroll_proxy_s scroll_proxy{};
    float scroll_flow_height = 0.f;
    float scroll_base_height = NAN;
    uint8_t scroll_completion_flags = 0;


    aether_layout_box_s() = default;
    aether_layout_box_s(const aether_layout_box_s&) = delete;
    aether_layout_box_s& operator=(const aether_layout_box_s&) = delete;
    aether_layout_box_s(aether_layout_box_s&&) = delete;
    aether_layout_box_s& operator=(aether_layout_box_s&&) = delete;
    ~aether_layout_box_s() noexcept;

    void add_child(aether_layout_box_s* child) { children.push_back(child); }

    /* recursive descendant+self count (matches C# DescendantsAndSelf) */
    int descendants_and_self() const {
        int n = 1;
        for (auto* c : children) n += c->descendants_and_self();
        return n;
    }
private:
    friend class LayoutBoxRegistry;
    aether_layout_box_s* registry_prev_ = nullptr;
    aether_layout_box_s* registry_next_ = nullptr;
};

// Keep traversal usable by internal header consumers linked to the hidden core.
inline LayoutBoxRegistry::iterator& LayoutBoxRegistry::iterator::operator++() noexcept {
    box_ = box_->registry_next_;
    return *this;
}

inline aether::style::ComputedStyleReadView layout_box_style(
    const aether_layout_box_s* box, const StyleLookup& styles, BoxStylePolicy policy)
{
    return styles.lookup_box(*box, policy);
}

// Empty means exact DOM order. Classification is shared by paint and hit testing,
// including children later omitted for visibility, display or pointer events.
inline std::vector<aether::stacking::stack_item_t> layout_children_paint_order(
    const aether_layout_box_s* box, const StyleLookup& styles, BoxStylePolicy policy)
{
    using namespace aether::stacking;
    const auto classify = [&styles, policy](const aether_layout_box_s* child) {
        return child->kind == LAYOUT_BOX_TEXT || child->is_atomic_control
            ? paint_classification_t{} : classify_element_paint(layout_box_style(child, styles, policy).scalars());
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

inline aether::layout::Presentation layout_box_presentation_style(
    const aether_layout_box_s* box, const StyleLookup& styles, BoxStylePolicy policy)
{
    return styles.presentation(*box, policy);
}
inline aether_node_s* layout_box_hit_owner(const aether_layout_box_s* box) {
    return box->presentation_owner ? box->presentation_owner : box->element;
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
    BoxStyleSnapshot style{};
    aether_node_s* presentation_owner = nullptr;
    std::string           text;
    bool is_atomic = false;
    bool is_line_break = false;
    // Collection-time marker: flattened inline decoration disables the entire
    // hanging sequence, including empty decorated elements.
    bool hanging_barrier = false;
    // Admission evidence and formerly discarded whitespace are retained for
    // the homogeneous source-unit path, but invisible to every legacy path.
    bool legacy_ignored = false;
    bool unit_barrier = false;
    // Only the final flow collector creates these zero-consumption markers.
    size_t block_anchor_index = static_cast<size_t>(-1);

    static inline_item_s block_anchor(size_t index) {
        inline_item_s i;
        i.block_anchor_index = index;
        return i;
    }

    static inline_item_s text_item(aether_node_s* el, const BoxStyleSnapshot& s,
                                   const std::string& t) {
        inline_item_s i;
        i.element = el;
        i.style = s;
        i.text = t;
        return i;
    }
    static inline_item_s atomic(aether_node_s* el, const BoxStyleSnapshot& s) {
        inline_item_s i;
        i.element = el;
        i.style = s;
        i.is_atomic = true;
        return i;
    }
    static inline_item_s line_break(aether_node_s* el, const BoxStyleSnapshot& s) {
        inline_item_s i;
        i.element = el;
        i.style = s;
        i.is_line_break = true;
        return i;
    }
};

struct aether_layout_result_s {
    size_t references = 1;
    aether_layout_box_s* root = nullptr;
    // Immutable effective context for this result, installed before all passes.
    bool effective_context_enabled = false;
    std::unordered_set<aether_node_s*> effective_fixed_container_nodes;
    aether::layout::LayoutAssetsRef assets;
    std::unique_ptr<aether::layout::MutableLayoutOverlay> build_overlay;
    std::unordered_map<aether_node_s*, aether_layout_box_s*> primary_boxes;
    std::vector<aether_layout_box_s*> paint_order;
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
    // Only published cells finished by the collected table algorithm enable repair.
    bool has_finalized_table_cells = false;
    size_t live_box_count = 0;
    bool abandoned = false;
    LayoutBoxRegistry allocated_boxes;

    aether_layout_result_s() = default;
    aether_layout_result_s(const aether_layout_result_s&) = delete;
    aether_layout_result_s& operator=(const aether_layout_result_s&) = delete;
    aether_layout_result_s(aether_layout_result_s&&) = delete;
    aether_layout_result_s& operator=(aether_layout_result_s&&) = delete;
    ~aether_layout_result_s() noexcept;

    aether_layout_box_s* create_box();
    // Published boxes may be replaced by a later layout pass. Destruction must
    // remove the exact instance, including text fragments that have no primary
    // box entry, before its storage can be reused.
    void unregister_box(aether_layout_box_s* box) noexcept {
        paint_order.erase(std::remove(paint_order.begin(), paint_order.end(), box), paint_order.end());
        if (box->element) {
            const auto it = primary_boxes.find(box->element);
            if (it != primary_boxes.end() && it->second == box) primary_boxes.erase(it);
        }
    }

    aether_layout_box_s* find_primary_box(aether_node_s* element) const {
        auto it = primary_boxes.find(element);
        return it != primary_boxes.end() ? it->second : nullptr;
    }

    bool accepts_style_map(const aether_style_map_s* override_styles) const noexcept {
        StyleOperationPin pin(assets, aether::style::pin_style_map(override_styles), override_styles != nullptr);
        return pin.accepts_override();
    }
    aether_node_s* hit_test(float x, float y, const StyleLookup& styles, BoxStylePolicy policy,
        aether_scroll_offset_fn offsets = nullptr, void* data = nullptr) const {
        if (!root || (assets && !assets->structure_valid())) return nullptr;
        return hit_test_recursive(root, x, y, styles, policy, offsets, data);
    }

private:
    friend struct aether_layout_box_s;
    void release_box(aether_layout_box_s* box) noexcept;
    static aether_node_s* hit_test_recursive(aether_layout_box_s* box, float x, float y,
                                              const StyleLookup& styles, BoxStylePolicy policy,
                                              aether_scroll_offset_fn offsets, void* data) {
        if (!layout_box_input_alive(box) || !layout_box_unmap_visual(box, x, y)) return nullptr;
        const auto style_view = layout_box_style(box, styles, policy);
        const auto& style = style_view.scalars();
        if (style.display == AETHER_DISPLAY_NONE) return nullptr;
        const auto& presentation = layout_box_presentation_style(box, styles, policy);
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
            const auto order = layout_children_paint_order(box, styles, policy);
            if (order.empty()) {
                for (auto it = box->children.rbegin(); it != box->children.rend(); ++it) {
                    if (auto* found = hit_test_recursive(*it, child_x, child_y, styles, policy, offsets, data)) return found;
                }
            } else {
                for (auto it = order.rbegin(); it != order.rend(); ++it) {
                    if (auto* found = hit_test_recursive(box->children[it->dom_index], child_x, child_y, styles, policy, offsets, data)) return found;
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
    const StyleLookup* measure_styles = nullptr;
    std::shared_ptr<node_identity_s> measure_document;
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
inline bool layout_box_clips(const aether::style::StyleScalars& style) {
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

/* Complete independent C callback copies; pinned immutable input is never exposed. */
inline aether_text_metrics_t engine_measure_text(
    const aether_layout_engine_s* e, const char* text,
    const BoxStyleSnapshot* style, float max_width, aether_node_s* node = nullptr)
{
    if (!e || (!e->contextual_text_measurer && !e->text_measurer)) return {0.f, 0.f, 0.f};
    const auto lifetime = e->measure_document;
    const CallbackEpochGuard guard(lifetime);
    std::shared_ptr<node_identity_s> identity;
    if (node && e->measure_styles) {
        if (const auto* record = e->measure_styles->lookup_record(node)) identity = record->node_lifetime();
    }
    const auto valid_node = [&]() { return !identity || (identity->owner == node && identity->alive && identity->attached); };
    if (!valid_node()) throw std::runtime_error("Retired text measurement node");
    aether::style::CallbackStyleCopy copy;
    const auto status = copy.prepare(style->view());
    if (status == AETHER_ERROR_OUT_OF_MEMORY) throw std::bad_alloc();
    if (status != AETHER_OK) throw std::runtime_error("Invalid callback style");
    aether_text_metrics_t metrics{};
    if (e->contextual_text_measurer) {
        const auto measures = e->measure_styles ? e->measure_styles->lookup_measures(node) : std::nullopt;
        const float used = aether::style::measure_used_line_height(measures ? &*measures : nullptr, style->scalars());
        if (!std::isfinite(used)) throw std::overflow_error("Non-finite callback line height");
        const auto callback = e->contextual_text_measurer; void* data = e->contextual_text_measurer_data;
        metrics = callback(text, copy.get(), used, max_width, data);
    } else {
        const auto callback = e->text_measurer; void* data = e->text_measurer_data;
        metrics = callback(text, copy.get(), max_width, data);
    }
    if (!guard.valid() || !valid_node()) throw std::runtime_error("DOM mutated during text measurement");
    if (!std::isfinite(metrics.width) || !std::isfinite(metrics.height) || !std::isfinite(metrics.baseline) || metrics.width < 0.f || metrics.height < 0.f)
        throw std::overflow_error("Invalid text metrics");
    return metrics;
}
