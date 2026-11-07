#include "layout_types.h"
#include "../css/style_map_types.h"
#include "../dom/dom_types.h"

aether_layout_result_s::~aether_layout_result_s() {
    if (abandoned) {
        // Failed preparations may contain detached provisional trees. Sever their
        // edges before reclaiming the allocation set, avoiding duplicate deletes.
        for (auto* box : allocated_boxes) box->children.clear();
        while (!allocated_boxes.empty()) delete *allocated_boxes.begin();
        root = nullptr;
    }
    delete root;
    assert(live_box_count == 0 && "all layout boxes must be owned by the final tree or released explicitly");
    if (layout_only_style_map) {
        for (auto* node : layout_only_style_nodes) {
            const auto it = layout_only_style_map->styles.find(node);
            if (it == layout_only_style_map->styles.end()) continue;
            aether_computed_style_free(&it->second);
            layout_only_style_map->styles.erase(it);
        }
    }
    for (auto* node : layout_only_nodes) delete node;
    aether_layout_result_destroy(retained_storage_owner);
}

aether_layout_box_s::~aether_layout_box_s() {
    // Unpublish before recursively freeing descendants: a re-layout may delete
    // a previously published subtree while the result remains live.
    if (owner) { owner->allocated_boxes.erase(this); owner->unregister_box(this); }
    for (auto* child : children) delete child;
    if (owner) owner->release_box();
}

aether_layout_box_s* aether_layout_result_s::create_box() {
    auto box_owner = std::make_unique<aether_layout_box_s>();
    auto* box = box_owner.get();
    allocated_boxes.insert(box);
    box_owner.release();
    box->owner = this;
    ++live_box_count;
    return box;
}

namespace {
aether_layout_box_s* clone_box_tree(aether_layout_result_s* destination, const aether_layout_box_s* source,
    std::unordered_map<const aether_layout_box_s*, aether_layout_box_s*>& boxes) {
    if (!source) return nullptr;
    auto* box = destination->create_box();
    boxes.emplace(source, box);
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
    if (!aether_style_map_structure_valid(source->style_map)) return AETHER_ERROR_INVALID_STATE;
    std::unique_ptr<aether_layout_result_s> result;
    try {
        result = std::make_unique<aether_layout_result_s>();
        result->abandoned = true;
        result->owned_style_map = aether_style_map_snapshot(source->style_map);
        result->style_map = result->owned_style_map.get();
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
        // Always retain the original storage owner, never the previous frame.
        // This preserves anonymous DOM aliases and shallow box-style buffers
        // while bounding retention across arbitrarily many opacity ticks.
        auto* owner = source->retained_storage_owner ? source->retained_storage_owner : source;
        ++owner->references;
        result->retained_storage_owner = owner;
        result->abandoned = false;
        *out = result.release();
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
    catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
