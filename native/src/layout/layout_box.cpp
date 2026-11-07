#include "layout_types.h"
#include "../css/style_map_types.h"
#include "../dom/dom_types.h"

void LayoutBoxRegistry::link(aether_layout_box_s* box) noexcept {
    assert(box && !box->owner && !box->registry_prev_ && !box->registry_next_);
    box->registry_next_ = head_;
    if (head_) head_->registry_prev_ = box;
    head_ = box;
}

void LayoutBoxRegistry::unlink(aether_layout_box_s* box) noexcept {
    assert(box && box->owner);
    if (box->registry_prev_) box->registry_prev_->registry_next_ = box->registry_next_;
    else { assert(head_ == box); head_ = box->registry_next_; }
    if (box->registry_next_) box->registry_next_->registry_prev_ = box->registry_prev_;
    box->registry_prev_ = nullptr;
    box->registry_next_ = nullptr;
}

void aether_layout_result_s::release_box(aether_layout_box_s* box) noexcept {
    assert(box && box->owner == this && live_box_count > 0);
    unregister_box(box);
    allocated_boxes.unlink(box);
    box->owner = nullptr;
    --live_box_count;
}

aether_layout_result_s::~aether_layout_result_s() noexcept {
    if (abandoned) {
        // Failed preparations may contain detached provisional trees. Sever their
        // edges before reclaiming the allocation registry, avoiding duplicate deletes.
        for (auto* box : allocated_boxes) box->children.clear();
        while (!allocated_boxes.empty()) delete *allocated_boxes.begin();
        root = nullptr;
    }
    delete root;
    assert(allocated_boxes.empty());
    assert(live_box_count == 0 && "all layout boxes must be owned by the final tree or released explicitly");

}

aether_layout_box_s::~aether_layout_box_s() noexcept {
    // Unpublish before recursively freeing descendants: a re-layout may delete
    // a previously published subtree while the result remains live.
    if (owner) owner->release_box(this);
    for (auto* child : children) delete child;
}

aether_layout_box_s* aether_layout_result_s::create_box() {
    auto box_owner = std::make_unique<aether_layout_box_s>();
    auto* box = box_owner.get();
    allocated_boxes.link(box);
    box->owner = this;
    ++live_box_count;
    return box_owner.release();
}

namespace {
aether_layout_box_s* clone_box_tree(aether_layout_result_s* destination, const aether_layout_box_s* source,
    std::unordered_map<const aether_layout_box_s*, aether_layout_box_s*>& boxes) {
    if (!source) return nullptr;
    auto* box = destination->create_box();
    boxes.emplace(source, box);
    box->presentation_owner = source->presentation_owner;
    box->kind = source->kind; box->element = source->element; box->style = source->style;
    box->text = source->text; box->is_atomic_control = source->is_atomic_control;
    box->control_display_text = source->control_display_text; box->control_shows_placeholder = source->control_shows_placeholder;
    box->scroll = source->scroll; box->visual_sample_valid = source->visual_sample_valid;
    box->visual_has_transform = source->visual_has_transform; box->visual_transform = source->visual_transform;
    box->visual_opacity = source->visual_opacity; box->visual_timestamp = source->visual_timestamp;
    box->visual_identity = source->visual_identity;
    box->collapsed_margin_before = source->collapsed_margin_before;
    box->collapsed_margin_after = source->collapsed_margin_after;
    box->own_relative_dx = source->own_relative_dx; box->own_relative_dy = source->own_relative_dy;
    box->used_border = source->used_border; box->margin_box = source->margin_box;
    box->border_box = source->border_box; box->padding_box = source->padding_box; box->content_box = source->content_box;
    box->children.reserve(source->children.size());
    for (const auto* child : source->children) box->children.push_back(clone_box_tree(destination, child, boxes));
    return box;
}
}
AETHER_API aether_status_t aether_layout_clone_snapshot(aether_layout_result_t source, aether_layout_result_t* out) {
    if (!source || !out) return AETHER_ERROR_NULL_ARG;
    if ((source->assets && !source->assets->structure_valid())) return AETHER_ERROR_INVALID_STATE;
    std::unique_ptr<aether_layout_result_s> result;
    try {
        result = std::make_unique<aether_layout_result_s>();
        result->abandoned = true;
        result->assets = source->assets;
        result->effective_context_enabled = source->effective_context_enabled;
        result->effective_fixed_container_nodes = source->effective_fixed_container_nodes;
        result->viewport = source->viewport; result->root_font_size = source->root_font_size;
        result->viewport_overflow_x = source->viewport_overflow_x;
        result->viewport_overflow_y = source->viewport_overflow_y;
        std::unordered_map<const aether_layout_box_s*, aether_layout_box_s*> boxes;
        result->root = clone_box_tree(result.get(), source->root, boxes);
        result->paint_order.reserve(source->paint_order.size());
        for (const auto* box : source->paint_order) result->paint_order.push_back(boxes.at(box));
        result->primary_boxes.reserve(source->primary_boxes.size());
        for (const auto& entry : source->primary_boxes) result->primary_boxes.emplace(entry.first, boxes.at(entry.second));
        result->abandoned = false;
        *out = result.release();
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
    catch (...) { return AETHER_ERROR_INVALID_STATE; }
}

namespace aether::layout {
ComputedStyleReadView StyleLookup::lookup_box(const aether_layout_box_s& box, BoxStylePolicy policy) const noexcept {
    if (policy == BoxStylePolicy::effective_map && box.element) {
        if (const auto found = lookup_node(box.element)) return *found;
    }
    return box.style.view();
}
Presentation StyleLookup::presentation(const aether_layout_box_s& box, BoxStylePolicy policy) const noexcept {
    if (box.presentation_owner) {
        if (policy == BoxStylePolicy::effective_map) {
            if (const auto found = lookup_node(box.presentation_owner))
                return {found->scalars().visible, found->scalars().pointer_events};
        }
        return {box.style.visible, box.style.pointer_events};
    }
    const auto style = lookup_box(box, policy);
    return {style.scalars().visible, style.scalars().pointer_events};
}
LayoutOperationPin::LayoutOperationPin(aether_layout_result_s* result) noexcept : result_(result) {
    if (result_) ++result_->references;
}
LayoutOperationPin::~LayoutOperationPin() { aether_layout_result_destroy(result_); }
} // namespace aether::layout
