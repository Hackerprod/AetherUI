#pragma once
#include "layout_types.h"
#include <optional>

namespace aether::layout {
// Diagnostic reason, not an error result: fallback must execute the old oracle.
enum class HitFallback : uint64_t { callback = 1, nonfinite_point, provenance, serial_exhausted, build_failure };
inline void count_hit_fallback(HitFallback reason) noexcept {
    diagnostics::count_work(diagnostics::WorkCounter::hit_fallbacks);
    diagnostics::count_work(diagnostics::WorkCounter::hit_fallback_reason, static_cast<uint64_t>(reason));
}

// One current index and one exact result, owned by geometry. No layout self-pin.
class HitAcceleration {
    using Counter = diagnostics::WorkCounter;
    static constexpr size_t none = std::numeric_limits<size_t>::max();
    struct Bounds {
        float left = 0, top = 0, right = 0, bottom = 0;
        bool unknown = false;
        static Bounds own(aether_rectf r) noexcept {
            // Match float addition/inclusive endpoints of aether_rectf_contains.
            Bounds b{r.x, r.y, r.x + r.width, r.y + r.height, false};
            b.unknown = !std::isfinite(r.x) || !std::isfinite(r.y) ||
                !std::isfinite(r.width) || !std::isfinite(r.height) ||
                !std::isfinite(b.right) || !std::isfinite(b.bottom) || r.width < 0 || r.height < 0;
            return b;
        }
        void unite(const Bounds& other) noexcept {
            unknown |= other.unknown;
            left = std::min(left, other.left); top = std::min(top, other.top);
            right = std::max(right, other.right); bottom = std::max(bottom, other.bottom);
        }
        bool contains(float x, float y) const noexcept {
            return unknown || (x >= left && x <= right && y >= top && y <= bottom);
        }
    };
    struct Box {
        aether_layout_box_s* box = nullptr; // Owned by containing layout, not DOM.
        Bounds bounds{};
        size_t children = none;
    };
    struct Range {
        Bounds bounds{};
        size_t lower = none, upper = none, child = none;
    };
    struct Epoch {
        std::shared_ptr<node_identity_s> document;
        uint64_t mutation = 0, structure = 0, style = 0;
        explicit Epoch(std::shared_ptr<node_identity_s> identity) : document(std::move(identity)) {
            if (document) { mutation = document->mutation_epoch; structure = document->structure_epoch; style = document->style_tree_epoch; }
        }
        bool valid() const noexcept {
            return document && document->alive && document->mutation_epoch == mutation &&
                document->structure_epoch == structure && document->style_tree_epoch == style;
        }
    };
    MapVersionRef selected_;
    BoxStylePolicy policy_;
    uint64_t presentation_;
    Epoch geometry_, base_, selected_document_;
    std::vector<Box> boxes_;
    std::vector<Range> ranges_;
    bool cached_ = false;
    float cached_x_ = 0, cached_y_ = 0;
    aether_node_s* cached_target_ = nullptr;
    std::shared_ptr<node_identity_s> cached_identity_;

    template<class T> static void reserve(std::vector<T>& values, size_t desired) {
        if (desired <= values.capacity()) return;
        const auto capacity = values.capacity();
        const auto next = std::max(desired, capacity > values.max_size() / 2 ? values.max_size() : std::max(size_t{8}, capacity * 2));
        diagnostics::count_work(Counter::hit_allocations);
        diagnostics::count_work(Counter::hit_bytes, next * sizeof(T));
        values.reserve(next);
    }
    size_t build_box(aether_layout_box_s* box, const StyleLookup& styles) {
        diagnostics::count_work(Counter::hit_build_boxes);
        const size_t index = boxes_.size();
        reserve(boxes_, index + 1); boxes_.push_back({box, Bounds::own(box->border_box), none});
        const auto order = layout_children_paint_order(box, styles, policy_);
        // Paint order helper allocation/classification is also build work. Its
        // vector capacity is reported, without claiming allocator metadata bytes.
        if (order.capacity()) {
            diagnostics::count_work(Counter::hit_allocations);
            diagnostics::count_work(Counter::hit_bytes, order.capacity() * sizeof(order[0]));
        }
        if (!box->children.empty()) {
            const auto children = build_range(box, order, 0, box->children.size(), styles);
            boxes_[index].children = children;
            boxes_[index].bounds.unite(ranges_[children].bounds);
        }
        // Do not project inverse-rounding acceptance into an approximate AABB.
        // Query enters this unbounded subtree and uses the exact current unmap.
        if (box->visual_sample_valid && box->visual_has_transform) boxes_[index].bounds.unknown = true;
        if (boxes_[index].bounds.unknown) diagnostics::count_work(Counter::hit_conservative_bounds);
        return index;
    }
    size_t build_range(aether_layout_box_s* box, const std::vector<aether::stacking::stack_item_t>& order,
        size_t begin, size_t end, const StyleLookup& styles) {
        diagnostics::count_work(Counter::hit_build_ranges);
        const size_t index = ranges_.size();
        reserve(ranges_, index + 1); ranges_.push_back({});
        if (end - begin == 1) {
            const size_t dom = order.empty() ? begin : static_cast<size_t>(order[begin].dom_index);
            const auto child = build_box(box->children[dom], styles);
            ranges_[index].child = child; ranges_[index].bounds = boxes_[child].bounds;
        } else {
            const size_t mid = begin + (end - begin) / 2;
            const auto lower = build_range(box, order, begin, mid, styles);
            const auto upper = build_range(box, order, mid, end, styles);
            ranges_[index].lower = lower; ranges_[index].upper = upper;
            ranges_[index].bounds = ranges_[lower].bounds;
            ranges_[index].bounds.unite(ranges_[upper].bounds);
        }
        return index;
    }
    aether_node_s* query_range(size_t index, float x, float y, const StyleLookup& styles) const {
        diagnostics::count_work(Counter::hit_query_ranges);
        const auto& range = ranges_[index];
        if (!range.bounds.contains(x, y)) return nullptr;
        if (range.child != none) return query_box(range.child, x, y, styles);
        if (auto* hit = query_range(range.upper, x, y, styles)) return hit;
        return query_range(range.lower, x, y, styles);
    }
    aether_node_s* query_box(size_t index, float x, float y, const StyleLookup& styles) const {
        diagnostics::count_work(Counter::hit_boxes_visited);
        diagnostics::count_work(Counter::hit_exact_boxes);
        const auto& entry = boxes_[index]; auto* box = entry.box;
        // Identical ordinary-content predicates to hit_test_recursive. E1 never
        // takes an external offset callback, so child_x/y equal x/y here.
        if (!layout_box_input_alive(box) || !layout_box_unmap_visual(box, x, y)) return nullptr;
        const auto style_view = layout_box_style(box, styles, policy_);
        const auto& style = style_view.scalars();
        if (style.display == AETHER_DISPLAY_NONE) return nullptr;
        const auto presentation = layout_box_presentation_style(box, styles, policy_);
        const bool clip_ok =
            (style.overflow_x == AETHER_OVERFLOW_VISIBLE || (x >= box->padding_box.x && x <= box->padding_box.x + box->padding_box.width)) &&
            (style.overflow_y == AETHER_OVERFLOW_VISIBLE || (y >= box->padding_box.y && y <= box->padding_box.y + box->padding_box.height));
        if (clip_ok && entry.children != none)
            if (auto* hit = query_range(entry.children, x, y, styles)) return hit;
        if (presentation.visible && presentation.pointer_events != AETHER_POINTER_EVENTS_NONE &&
            layout_box_contains_own_content(box, x, y)) return layout_box_hit_owner(box);
        return nullptr;
    }
public:
    HitAcceleration(const aether_layout_result_s& layout, const StyleLookup& styles, BoxStylePolicy policy)
        : selected_(styles.selected_version()), policy_(policy), presentation_(layout.hit_presentation_serial),
          geometry_(styles.document_lifetime()), base_(styles.base_version()->document_lifetime()),
          selected_document_(styles.selected_version()->document_lifetime()) {
        build_box(layout.root, styles);
    }
    static bool has_provenance(const StyleLookup& styles) noexcept {
        return styles.document_lifetime() && styles.base_version() && styles.base_version()->document_lifetime() &&
            styles.selected_version() && styles.selected_version()->document_lifetime();
    }
    bool matches(const aether_layout_result_s& layout, const StyleLookup& styles, BoxStylePolicy policy) const noexcept {
        return !layout.hit_acceleration_disabled && presentation_ == layout.hit_presentation_serial &&
            selected_ == styles.selected_version() && policy_ == policy &&
            geometry_.valid() && base_.valid() && selected_document_.valid();
    }
    std::optional<aether_node_s*> cached(float x, float y) const noexcept {
        if (!cached_ || x != cached_x_ || y != cached_y_) return std::nullopt;
        if (cached_target_ && (!cached_identity_ || !cached_identity_->alive || !cached_identity_->attached ||
            cached_identity_->owner != cached_target_)) return std::nullopt;
        return cached_target_;
    }
    aether_node_s* query(float x, float y, const StyleLookup& styles) const { return query_box(0, x, y, styles); }
    // Called only AFTER the wrapper's completed-query post guards. A null from
    // exception/invalidated operation never reaches this method.
    void remember(float x, float y, aether_node_s* target, const StyleLookup& styles) noexcept {
        cached_ = false; cached_identity_.reset(); cached_target_ = nullptr;
        if (target) {
            const auto record = styles.base_version()->lookup(target);
            if (!record || !record.payload_owner()->node_lifetime()) return;
            const auto& identity = record.payload_owner()->node_lifetime();
            if (identity->owner != target || !identity->alive || !identity->attached) return;
            cached_identity_ = identity;
        }
        cached_x_ = x; cached_y_ = y; cached_target_ = target; cached_ = true;
    }
};

inline std::shared_ptr<HitAcceleration> prepare_hit_acceleration(aether_layout_result_s& layout,
    const StyleLookup& styles, BoxStylePolicy policy, float x, float y) {
    if (layout.hit_acceleration_disabled) { count_hit_fallback(HitFallback::serial_exhausted); return {}; }
    if (!std::isfinite(x) || !std::isfinite(y)) { count_hit_fallback(HitFallback::nonfinite_point); return {}; }
    if (!layout.root || !HitAcceleration::has_provenance(styles)) { count_hit_fallback(HitFallback::provenance); return {}; }
    if (layout.hit_acceleration && layout.hit_acceleration->matches(layout, styles, policy)) return layout.hit_acceleration;
    // Drop A before staging B: one layout cannot accumulate old maps on failure.
    layout.hit_acceleration.reset();
    diagnostics::count_work(diagnostics::WorkCounter::hit_build_attempts);
    try {
        diagnostics::count_work(diagnostics::WorkCounter::hit_allocations);
        diagnostics::count_work(diagnostics::WorkCounter::hit_bytes, sizeof(HitAcceleration));
        auto candidate = std::make_shared<HitAcceleration>(layout, styles, policy);
        diagnostics::count_work(diagnostics::WorkCounter::hit_builds);
        return candidate;
    } catch (...) {
        count_hit_fallback(HitFallback::build_failure);
        return {};
    }
}
} // namespace aether::layout
