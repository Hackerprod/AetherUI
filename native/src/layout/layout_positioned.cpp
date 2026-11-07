/* ── Out-of-flow positioned descendants ─────────────────────────────── */
#include "layout_internal.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
struct positioned_margin_axis {
    float span;
    float start;
    float end;
    bool auto_start;
    bool auto_end;
    bool eligible;

    positioned_margin_axis(float extent, float inset_start, float inset_end,
                           float fixed_start, float fixed_end,
                           aether_css_length css_start, aether_css_length css_end)
        : span(std::max(0.f, extent - inset_start - inset_end)),
          start(fixed_start), end(fixed_end),
          auto_start(is_auto_margin(css_start)), auto_end(is_auto_margin(css_end)),
          eligible(std::isfinite(inset_start) && std::isfinite(inset_end) &&
                   (auto_start || auto_end)) {}

    void resolve_used(float border_size, bool horizontal) {
        if (!eligible) return;
        const float residual = span - border_size - start - end;
        if (auto_start && auto_end) {
            start = horizontal && residual <= 0.f ? 0.f : residual / 2.f;
            end = residual - start;
        } else if (auto_start) {
            start = residual;
        } else {
            end = residual;
        }
    }
};
}

void layout_positioned_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf positioning_block,
    aether_layout_result_s* result, aether_node_s* only_node)
{
    auto flex_static_position = [&](const aether_computed_style_t& child_style,
                                    const aether_rectf& containing,
                                    const aether_rectf& child_margin_box) {
        const auto& flex = parent_box->style;
        const bool horizontal = flex.flex_direction == AETHER_FLEX_DIRECTION_ROW;
        const float main_extent = horizontal ? containing.width : containing.height;
        const float cross_extent = horizontal ? containing.height : containing.width;
        const float child_main = horizontal ? child_margin_box.width : child_margin_box.height;
        const float child_cross = horizontal ? child_margin_box.height : child_margin_box.width;

        // Absolutely positioned flex children are not flex items and must never influence one
        // another's anchor. CSS Flexbox §4.1 aligns each child independently inside the flex
        // container's content box as though it were the sole item.
        const float main_free = main_extent - child_main;
        float main_offset = 0.f;
        if (flex.justify_content == AETHER_JUSTIFY_CENTER) main_offset = main_free / 2.f;
        else if (flex.justify_content == AETHER_JUSTIFY_END) main_offset = main_free;
        // space-between has no distributable gap for a sole hypothetical item, so it is start.

        const int32_t cross_alignment = child_style.align_self == AETHER_ALIGN_SELF_AUTO
            ? flex.align_items
            : child_style.align_self;
        const float cross_free = cross_extent - child_cross;
        float cross_offset = 0.f;
        if (cross_alignment == AETHER_ALIGN_CENTER) cross_offset = cross_free / 2.f;
        else if (cross_alignment == AETHER_ALIGN_END) cross_offset = cross_free;
        // A fixed-size absolutely positioned child with stretch has no free sizing decision at
        // this stage; its static start edge is the correct alignment anchor. Auto cross-size
        // stretch is handled by the positioned sizing algorithm before this placement.

        return horizontal
            ? aether_rectf{containing.x + main_offset, containing.y + cross_offset, 0.f, 0.f}
            : aether_rectf{containing.x + cross_offset, containing.y + main_offset, 0.f, 0.f};
    };

    auto static_position = [&](aether_node_s* node) {
        // A positioned box with an auto inset keeps the position it would have occupied in
        // normal flow. The laid-out in-flow children give us that anchor without putting the
        // positioned box back into sizing. In particular this keeps ::after adjacent to the
        // preceding text even when the parent is center-aligned.
        if (node->pseudo_element == aether_pseudo_element_t::before) {
            if (!parent_box->children.empty()) {
                const auto* first = parent_box->children.front();
                const auto flow_margin = normal_flow_margin_box(first);
                return aether_rectf{flow_margin.x, flow_margin.y, 0.f, 0.f};
            }
            return aether_rectf{positioning_block.x, positioning_block.y, 0.f, 0.f};
        }

        auto node_it = std::find(parent->children.begin(), parent->children.end(), node);
        if (node_it != parent->children.begin()) {
            for (auto previous = node_it; previous != parent->children.begin();) {
                --previous;
                const auto* previous_node = *previous;
                for (auto box_it = parent_box->children.rbegin(); box_it != parent_box->children.rend(); ++box_it) {
                    const auto* candidate = *box_it;
                    const bool is_previous_element = previous_node->type == AETHER_NODE_ELEMENT &&
                        candidate->element == previous_node;
                    const bool is_parent_text = previous_node->type == AETHER_NODE_TEXT &&
                        candidate->kind == LAYOUT_BOX_TEXT && candidate->element == parent;
                    if (is_previous_element || is_parent_text) {
                        const auto flow_margin = normal_flow_margin_box(candidate);
                        return aether_rectf{
                            flow_margin.x + flow_margin.width,
                            flow_margin.y,
                            0.f, 0.f,
                        };
                    }
                }
            }
        }
        return aether_rectf{positioning_block.x, positioning_block.y, 0.f, 0.f};
    };

    for (auto* node : parent->children) {
        if (only_node && only_node != node) continue;
        if (node->type != AETHER_NODE_ELEMENT) continue;
        const auto style = get_style(styles, node);
        if (!is_out_of_flow(style) || style.display == AETHER_DISPLAY_NONE) continue;

        // Fixed-position descendants use the viewport; absolute descendants use the nearest
        // positioned ancestor supplied by their parent layout call.
        aether_rectf containing = style.position == AETHER_POSITION_FIXED
            ? aether_rectf{0.f, 0.f, result->viewport.width, result->viewport.height}
            : positioning_block;
        if (style.position == AETHER_POSITION_FIXED && result->effective_context_enabled) {
            for (auto* ancestor = parent; ancestor; ancestor = ancestor->parent) {
                if (!result->effective_fixed_container_nodes.count(ancestor)) continue;
                if (ancestor == parent) containing = positioning_block;
                else if (const auto* owner = result->find_primary_box(ancestor)) containing = owner->padding_box;
                break;
            }
        }
        const auto margin = resolve_margin_edges(engine, style.margin, containing.width, style);
        const float left = resolve(engine, style.inset.left, containing.width, style);
        const float right = resolve(engine, style.inset.right, containing.width, style);
        const float top = resolve(engine, style.inset.top, containing.height, style);
        const float bottom = resolve(engine, style.inset.bottom, containing.height, style);

        positioned_margin_axis horizontal(containing.width, left, right, margin.left, margin.right,
                                          style.margin.left, style.margin.right);
        positioned_margin_axis vertical(containing.height, top, bottom, margin.top, margin.bottom,
                                        style.margin.top, style.margin.bottom);

        const float specified_width = resolve(engine, style.width, containing.width, style);
        const float specified_height = resolve(engine, style.height, containing.height, style);
        const bool constrain_width = !std::isfinite(specified_width) &&
            std::isfinite(left) && std::isfinite(right);
        const bool constrain_height = !std::isfinite(specified_height) &&
            std::isfinite(top) && std::isfinite(bottom);
        // Clamp the inset-modified span before subtracting fixed margins: a negative
        // fixed margin can still provide size when the two insets cross.
        const float forced_width = std::max(0.f, (horizontal.eligible ? horizontal.span : containing.width - left - right) -
                                             aether_thicknessf_horizontal(margin));
        const float forced_height = std::max(0.f, (vertical.eligible ? vertical.span : containing.height - top - bottom) -
                                              aether_thicknessf_vertical(margin));

        auto* child_box = layout_element(engine, styles, node, containing, containing.height, containing.y,
                                         false, forced_width, constrain_width,
                                         forced_height, constrain_height ? forced_height_kind_e::definite : forced_height_kind_e::none, true, result);

        // Sizing owns min/max and the decoration floor. Resolve positioned auto
        // margins only after that single layout call has produced the final border box.
        horizontal.resolve_used(child_box->border_box.width, true);
        vertical.resolve_used(child_box->border_box.height, false);
        if (horizontal.eligible || vertical.eligible) {
            const auto& border = child_box->border_box;
            child_box->margin_box = {border.x - horizontal.start, border.y - vertical.start,
                border.width + horizontal.start + horizontal.end,
                border.height + vertical.start + vertical.end};
            child_box->collapsed_margin_before = vertical.start;
            child_box->collapsed_margin_after = vertical.end;
        }

        const auto static_anchor = parent_box->style.display == AETHER_DISPLAY_FLEX
            ? flex_static_position(style, containing, child_box->margin_box)
            : static_position(node);
        float x = static_anchor.x;
        if (std::isfinite(left)) {
            x = containing.x + left;
        } else if (std::isfinite(right)) {
            x = containing.x + containing.width - right - child_box->margin_box.width;
        }
        float y = static_anchor.y;
        if (std::isfinite(top)) {
            y = containing.y + top;
        } else if (std::isfinite(bottom)) {
            y = containing.y + containing.height - bottom - child_box->margin_box.height;
        }
        const auto flow_margin = normal_flow_margin_box(child_box);
        offset_box(child_box, x - flow_margin.x, y - flow_margin.y);
        parent_box->add_child(child_box);
    }
}

namespace {
void finalize_fixed_children(const aether_layout_engine_s* engine, aether_style_map_s* styles,
    aether_layout_result_s* result, aether_layout_box_s* parent) {
    for (size_t index = 0; index < parent->children.size(); ++index) {
        auto* child = parent->children[index];
        bool has_container = false;
        if (child->kind == LAYOUT_BOX_ELEMENT && child->style.position == AETHER_POSITION_FIXED) {
            for (auto* ancestor = child->element->parent; ancestor; ancestor = ancestor->parent) {
                if (result->effective_fixed_container_nodes.count(ancestor)) { has_container = true; break; }
            }
        }
        if (has_container) {
            auto* node = child->element;
            // Only after the complete flow pass are ancestor auto block sizes
            // known. Re-run positioned sizing, not just a visual translation:
            // percentages and opposing insets must see the final padding box.
            parent->children.erase(parent->children.begin() + index);
            delete child;
            const auto previous_count = parent->children.size();
            layout_positioned_children(engine, styles, parent, node->parent,
                parent->padding_box, result, node);
            if (parent->children.size() != previous_count + 1)
                throw std::runtime_error("Fixed child preparation did not produce a box");
            child = parent->children.back();
            parent->children.pop_back();
            parent->children.insert(parent->children.begin() + index, child);
        }
        finalize_fixed_children(engine, styles, result, child);
    }
}
}
void layout_finalize_fixed_containers(const aether_layout_engine_s* engine,
    aether_style_map_s* styles, aether_layout_result_s* result) {
    if (result->root && result->effective_context_enabled)
        finalize_fixed_children(engine, styles, result, result->root);
}
