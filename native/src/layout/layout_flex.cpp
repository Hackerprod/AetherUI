/* ── Flex layout — anonymous items, §9 resolution, lines and alignment ─ */
#include "layout_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

static std::string normalize_whitespace(const std::string& value) {
    std::string result;
    bool prev_space = true; // start true so leading spaces are skipped
    for (char c : value) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!prev_space) {
                result += ' ';
                prev_space = true;
            }
        } else {
            result += c;
            prev_space = false;
        }
    }
    if (!result.empty() && result.back() == ' ')
        result.pop_back();
    return result;
}

// Flexbox §4 wraps each contiguous run of direct in-flow text in an anonymous
// flex item.  The node is layout-owned, while its text children remain the
// authored DOM nodes; this preserves DOM order without exposing a synthetic
// element through the public DOM ABI.
static aether_node_s* create_anonymous_flex_item(
    aether_style_map_s* styles,
    aether_layout_result_s* result,
    aether_node_s* parent,
    const aether_computed_style_t& parent_style,
    const std::vector<aether_node_s*>& text_nodes)
{
    bool has_non_collapsible_content = false;
    for (auto* node : text_nodes) {
        if (node && node->type == AETHER_NODE_TEXT &&
            !normalize_whitespace(node->text).empty()) {
            has_non_collapsible_content = true;
            break;
        }
    }
    // A whitespace-only anonymous flex item is discarded by Flexbox §4.
    if (!has_non_collapsible_content || !styles || !result) return nullptr;

    auto* anonymous = new aether_node_s();
    anonymous->type = AETHER_NODE_ELEMENT;
    anonymous->parent = parent;
    anonymous->tag_name = "aether-anonymous-flex-item";
    anonymous->style_origin = parent;
    anonymous->anonymous_flex_owner = parent;
    anonymous->children = text_nodes;

    // Anonymous flex items are transparent formatting wrappers: only inherited
    // text properties transfer from the flex container; box decoration and
    // flex longhands use their CSS initial values.
    auto anonymous_style = aether_computed_style_default();
    anonymous_style.color = parent_style.color;
    std::strncpy(anonymous_style.font_family, parent_style.font_family,
                 sizeof(anonymous_style.font_family) - 1);
    anonymous_style.font_size = parent_style.font_size;
    anonymous_style.font_weight = parent_style.font_weight;
    anonymous_style.line_height = parent_style.line_height;
    anonymous_style.text_align = parent_style.text_align;
    anonymous_style.text_transform = parent_style.text_transform;
    anonymous_style.white_space = parent_style.white_space;
    anonymous_style.cursor = parent_style.cursor;
    anonymous_style.visible = parent_style.visible;
    anonymous_style.pointer_events = parent_style.pointer_events;
    (void)aether_shadow_list_copy(&parent_style.text_shadows, &anonymous_style.text_shadows);
    anonymous_style.display = AETHER_DISPLAY_BLOCK;

    styles->styles.emplace(anonymous, anonymous_style);
    result->layout_only_style_map = styles;
    result->layout_only_nodes.push_back(anonymous);
    result->layout_only_style_nodes.push_back(anonymous);
    return anonymous;
}

const std::vector<aether_node_s*>& collect_flex_items(
    aether_style_map_s* styles,
    aether_layout_result_s* result,
    aether_node_s* parent,
    const aether_computed_style_t& parent_style)
{
    static const std::vector<aether_node_s*> empty;
    if (!parent || !result) return empty;
    const auto existing = result->anonymous_flex_items.find(parent);
    if (existing != result->anonymous_flex_items.end()) return existing->second;

    auto [entry, inserted] = result->anonymous_flex_items.emplace(parent, std::vector<aether_node_s*>{});
    (void)inserted;
    auto& items = entry->second;
    std::vector<aether_node_s*> direct_text_run;
    const auto flush_direct_text = [&]() {
        if (auto* anonymous = create_anonymous_flex_item(
                styles, result, parent, parent_style, direct_text_run)) {
            items.push_back(anonymous);
        }
        direct_text_run.clear();
    };

    for (auto* node : parent->children) {
        if (node->type == AETHER_NODE_TEXT) {
            direct_text_run.push_back(node);
            continue;
        }
        flush_direct_text();
        if (node->type != AETHER_NODE_ELEMENT) continue;
        const auto child_style = get_style(styles, node);
        const bool out_of_flow = child_style.position == AETHER_POSITION_ABSOLUTE ||
            child_style.position == AETHER_POSITION_FIXED;
        if (child_style.display != AETHER_DISPLAY_NONE && !out_of_flow) {
            items.push_back(node);
        }
    }
    flush_direct_text();
    return items;
}

static void add_auto_margin_space(aether_layout_box_s* box,
                                  float left, float top, float right, float bottom) {
    if (left != 0.f || top != 0.f) offset_box(box, left, top);
    box->margin_box.x -= left;
    box->margin_box.y -= top;
    box->margin_box.width += left + right;
    box->margin_box.height += top + bottom;
}

static int auto_margin_count(const aether_computed_style_t& style, bool horizontal) {
    return horizontal
        ? static_cast<int>(is_auto_margin(style.margin.left)) + static_cast<int>(is_auto_margin(style.margin.right))
        : static_cast<int>(is_auto_margin(style.margin.top)) + static_cast<int>(is_auto_margin(style.margin.bottom));
}

static bool consume_flex_cross_auto_margins(aether_layout_box_s* box,
                                             const aether_computed_style_t& style,
                                             bool horizontal_cross_axis,
                                             float available_cross_size) {
    const bool before_auto = horizontal_cross_axis ? is_auto_margin(style.margin.left) : is_auto_margin(style.margin.top);
    const bool after_auto = horizontal_cross_axis ? is_auto_margin(style.margin.right) : is_auto_margin(style.margin.bottom);
    const float used_size = horizontal_cross_axis ? box->margin_box.width : box->margin_box.height;
    const auto resolved = aether_resolve_auto_margin_pair(
        available_cross_size, used_size, 0.f, 0.f, before_auto, after_auto);
    if (resolved.before <= 0.f && resolved.after <= 0.f) return false;
    if (horizontal_cross_axis) add_auto_margin_space(box, resolved.before, 0.f, resolved.after, 0.f);
    else add_auto_margin_space(box, 0.f, resolved.before, 0.f, resolved.after);
    return true;
}

int32_t flex_cross_alignment(const aether_computed_style_t& container,
                             const aether_computed_style_t& item) {
    return item.align_self == AETHER_ALIGN_SELF_AUTO
        ? container.align_items
        : item.align_self;
}

struct resolved_flex_children_s {
    std::vector<aether_layout_box_s*> boxes;
    aether_flex_resolve_result_s resolution;
};

static bool flex_length_is_definite(aether_css_length length, bool main_size_is_definite) {
    if (length.unit == AETHER_CSS_UNIT_PERCENT) return main_size_is_definite;
    if (length.unit == AETHER_CSS_UNIT_LINEAR && std::fabs(length.linear_percent) > .0001f) {
        return main_size_is_definite;
    }
    return length.unit != AETHER_CSS_UNIT_AUTO;
}

static float resolve_flex_main_border_size(const aether_layout_engine_s* engine,
                                           const aether_computed_style_t& style,
                                           const aether_rectf& content,
                                           bool horizontal, bool main_size_is_definite,
                                           aether_css_length length) {
    if (!flex_length_is_definite(length, main_size_is_definite)) return NAN;
    const float reference = horizontal ? content.width : content.height;
    const float value = resolve(engine, length, reference, style);
    if (!std::isfinite(value)) return NAN;
    const auto padding = resolve_padding_edges(engine, style.padding, content.width, style);
    const auto border = resolve_border_edges(engine, style, content.width);
    const float decorations = horizontal
        ? aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border)
        : aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border);
    return style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX
        ? std::max(0.f, value) + decorations
        : std::max(0.f, value);
}

static float flex_box_main_size(const aether_layout_box_s* box, bool horizontal) {
    return horizontal ? box->border_box.width : box->border_box.height;
}

static float flex_box_outer_main_size(const aether_layout_box_s* box, bool horizontal) {
    return horizontal ? box->margin_box.width : box->margin_box.height;
}

static float flex_box_cross_size(const aether_layout_box_s* box, bool horizontal) {
    return horizontal ? box->margin_box.height : box->margin_box.width;
}

static float flex_box_border_cross_size(const aether_layout_box_s* box, bool horizontal) {
    return horizontal ? box->border_box.height : box->border_box.width;
}

static bool flex_cross_margins_are_auto(const aether_computed_style_t& style, bool horizontal) {
    return horizontal
        ? is_auto_margin(style.margin.top) || is_auto_margin(style.margin.bottom)
        : is_auto_margin(style.margin.left) || is_auto_margin(style.margin.right);
}

// CSS Flexbox §4.5 uses a zero automatic minimum for scrollable overflow.
// Keep this classification separate from the engine's actual scrolling paths:
// overflow:hidden participates here even though it never creates scroll offsets.
// Enumerating the values deliberately keeps a future overflow:clip out of this
// category until that value has its own CSS Overflow semantics implemented.
static bool flex_automatic_minimum_is_zero_for_overflow(int32_t overflow) {
    switch (overflow) {
        case AETHER_OVERFLOW_HIDDEN:
        case AETHER_OVERFLOW_AUTO:
        case AETHER_OVERFLOW_SCROLL:
            return true;
        case AETHER_OVERFLOW_VISIBLE:
        default:
            return false;
    }
}

static bool flex_item_stretches_on_cross_axis(const aether_computed_style_t& container,
                                               const aether_computed_style_t& item,
                                               bool horizontal) {
    if (flex_cross_alignment(container, item) != AETHER_ALIGN_STRETCH ||
        flex_cross_margins_are_auto(item, horizontal)) {
        return false;
    }
    return horizontal ? item.height.unit == AETHER_CSS_UNIT_AUTO
                      : item.width.unit == AETHER_CSS_UNIT_AUTO;
}

static resolved_flex_children_s resolve_flex_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    const std::vector<aether_node_s*>& children,
    const aether_rectf& content,
    const aether_computed_style_t& parent_style,
    bool horizontal, bool main_size_is_definite, bool cross_size_is_definite,
    aether_layout_result_s* result) {
    resolved_flex_children_s output{};
    output.boxes.reserve(children.size());
    std::vector<aether_layout_box_s*> base_boxes;
    base_boxes.reserve(children.size());

    // CSS Flexbox §9.2.3: a single-line stretched item with an automatic
    // cross-size measures its flex base size against the container's definite
    // cross-size. Every other item retains its natural cross-size until §9.4.
    const bool single_line_container = !parent_style.flex_wrap;
    {
        // Base-size measurement is an implementation detail of flex resolution.
        // Its boxes must neither be visible through the final result nor outlive
        // this function.
        detached_layout_scope_s detached(result);
        for (auto* child : children) {
            const auto child_style = get_style(styles, child);
            const bool stretch_base_cross = single_line_container && cross_size_is_definite &&
                flex_item_stretches_on_cross_axis(parent_style, child_style, horizontal);
            const auto margin = resolve_margin_edges(engine, child_style.margin, content.width, child_style);
            const float forced_border_cross = std::max(0.f,
                (horizontal ? content.height : content.width) -
                (horizontal ? aether_thicknessf_vertical(margin) : aether_thicknessf_horizontal(margin)));
            base_boxes.push_back(layout_element(engine, styles, child,
                {content.x, content.y, content.width, content.height}, content.y,
                false,
                horizontal ? 0.f : forced_border_cross, stretch_base_cross && !horizontal,
                horizontal ? forced_border_cross : 0.f, stretch_base_cross && horizontal,
                true, result));
        }
    }

    std::vector<aether_flex_item_input_s> inputs;
    inputs.reserve(children.size());
    for (size_t index = 0; index < children.size(); ++index) {
        const auto child_style = get_style(styles, children[index]);
        const auto* base_box = base_boxes[index];
        aether_flex_item_input_s item{};
        item.basis.kind = child_style.flex_basis.kind;
        item.basis.content_main_size = flex_box_main_size(base_box, horizontal);
        item.basis.specified_main_size = resolve_flex_main_border_size(
            engine, child_style, content, horizontal, main_size_is_definite,
            horizontal ? child_style.width : child_style.height);
        item.basis.resolved_length = resolve_flex_main_border_size(
            engine, child_style, content, horizontal, main_size_is_definite,
            child_style.flex_basis.length);
        item.basis.length_is_definite = child_style.flex_basis.kind == AETHER_FLEX_BASIS_LENGTH &&
            flex_length_is_definite(child_style.flex_basis.length, main_size_is_definite);
        item.min_main_size = resolve_flex_main_border_size(
            engine, child_style, content, horizontal, main_size_is_definite,
            horizontal ? child_style.min_width : child_style.min_height);
        item.max_main_size = resolve_flex_main_border_size(
            engine, child_style, content, horizontal, main_size_is_definite,
            horizontal ? child_style.max_width : child_style.max_height);
        const auto child_padding = resolve_padding_edges(engine, child_style.padding, content.width, child_style);
        const auto child_border = resolve_border_edges(engine, child_style, content.width);
        // The automatic minimum on a row's main axis is a min-content inline-size.
        // On a column's main axis it is a block-size at the item's resolved cross-size.
        // The base layout has already established that cross-size (including nested flex
        // allocation, controls and real line breaking), so it is the authoritative source
        // for the latter. Re-measuring through a parallel intrinsic-height walk loses the
        // horizontal constraints of nested rows and can let a column shrink an item below
        // its painted descendant extent.
        const auto main_size_property = horizontal ? child_style.width : child_style.height;
        const bool automatic_main_size = main_size_property.unit == AETHER_CSS_UNIT_AUTO;
        const float content_size_suggestion = horizontal
            ? measure_intrinsic_width(engine, styles, children[index], child_style,
                                      child_padding, child_border, content.width, result,
                                      aether_intrinsic_size_mode_e::min_content)
            : automatic_main_size
                ? flex_box_main_size(base_box, false)
                : measure_intrinsic_height(engine, styles, children[index], child_style,
                                           child_padding, child_border, content.width);
        const int32_t main_axis_overflow = horizontal ? child_style.overflow_x : child_style.overflow_y;
        item.automatic_minimum = {
            flex_automatic_minimum_is_zero_for_overflow(main_axis_overflow),
            content_size_suggestion, item.basis.specified_main_size, NAN, item.max_main_size
        };
        // Unlike every other flex input, explicit margins are signed.  A
        // negative margin reduces the item's outer main size and must take
        // part in line formation and free-space resolution (Flexbox §9.7).
        // Auto margins are still zero here and consumed in their later phase.
        item.outer_main_size =
            flex_box_outer_main_size(base_box, horizontal) - flex_box_main_size(base_box, horizontal);
        item.flex_grow = child_style.flex_grow;
        item.flex_shrink = child_style.flex_shrink;
        inputs.push_back(item);
    }

    const float available_main_size = main_size_is_definite
        ? (horizontal ? content.width : content.height)
        : NAN;
    output.resolution = aether_resolve_flexible_lengths({
        &inputs, available_main_size, std::max(0.f, parent_style.gap),
        parent_style.flex_wrap && main_size_is_definite
    });

    for (auto* base_box : base_boxes) delete base_box;
    base_boxes.clear();

    for (size_t index = 0; index < children.size(); ++index) {
        const float target_main_size = output.resolution.items[index].target_main_size;
        output.boxes.push_back(layout_element(engine, styles, children[index],
            {content.x, content.y, content.width, content.height}, content.y,
            false,
            horizontal ? target_main_size : 0.f, horizontal,
            horizontal ? 0.f : target_main_size,
            !horizontal,
            true, result));
    }
    return output;
}

static float flex_line_used_main_size(const std::vector<aether_layout_box_s*>& boxes,
                                      const aether_flex_line_s& line,
                                      bool horizontal, float gap) {
    float used = gap * static_cast<float>(line.item_indices.empty() ? 0 : line.item_indices.size() - 1);
    for (const size_t index : line.item_indices) used += flex_box_outer_main_size(boxes[index], horizontal);
    return used;
}

static float flex_line_cross_size(const std::vector<aether_layout_box_s*>& boxes,
                                  const aether_flex_line_s& line, bool horizontal) {
    float size = 0.f;
    for (const size_t index : line.item_indices) {
        size = std::max(size, flex_box_cross_size(boxes[index], horizontal));
    }
    return size;
}

/* CSS Flexbox §9.4 determines a line's cross size from items laid out at their
   final main size. Stretch then re-lays out only auto-sized items against that
   line size. Keeping this here, rather than in flex_resolver.cpp, preserves the
   resolver's pure main-axis contract. */
static float resolve_flex_line_cross_size(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    const std::vector<aether_node_s*>& children,
    resolved_flex_children_s& resolved,
    const aether_flex_line_s& line,
    const aether_rectf& content,
    const aether_computed_style_t& container_style,
    bool horizontal, bool cross_size_is_definite, bool is_single_line,
    aether_layout_result_s* result) {
    float target_cross_size = flex_line_cross_size(resolved.boxes, line, horizontal);
    if (is_single_line && cross_size_is_definite) {
        target_cross_size = horizontal ? content.height : content.width;
    }

    // A min/max cross constraint can expand an auto-sized stretched item beyond
    // the provisional line. Re-run until that monotonic line size is stable so
    // every eligible sibling receives the same final line extent.
    const size_t maximum_passes = line.item_indices.size() + 1;
    for (size_t pass = 0; pass < maximum_passes; ++pass) {
        bool line_grew = false;
        for (const size_t index : line.item_indices) {
            const auto child_style = get_style(styles, children[index]);
            if (!flex_item_stretches_on_cross_axis(container_style, child_style, horizontal)) continue;

            auto* previous = resolved.boxes[index];
            const float main_size = flex_box_main_size(previous, horizontal);
            const float cross_decoration_and_margins = std::max(0.f,
                flex_box_cross_size(previous, horizontal) - flex_box_border_cross_size(previous, horizontal));
            const float forced_border_cross_size = std::max(0.f, target_cross_size - cross_decoration_and_margins);
            delete previous;

            resolved.boxes[index] = layout_element(engine, styles, children[index],
                {content.x, content.y, content.width, content.height}, content.y,
                false,
                horizontal ? main_size : forced_border_cross_size, true,
                horizontal ? forced_border_cross_size : main_size, true,
                true, result);

            if (!(is_single_line && cross_size_is_definite)) {
                const float actual_cross_size = flex_box_cross_size(resolved.boxes[index], horizontal);
                if (actual_cross_size > target_cross_size + .0001f) {
                    target_cross_size = actual_cross_size;
                    line_grew = true;
                }
            }
        }
        if (!line_grew) break;
    }
    return target_cross_size;
}

static void consume_flex_main_auto_margins(const std::vector<aether_layout_box_s*>& boxes,
                                           const std::vector<aether_node_s*>& children,
                                           aether_style_map_s* styles,
                                           const aether_flex_line_s& line,
                                           bool horizontal, float available_main_size,
                                           float& used_main_size) {
    const float free = available_main_size - used_main_size;
    if (free <= 0.f) return;
    int auto_sides = 0;
    for (const size_t index : line.item_indices) {
        auto_sides += auto_margin_count(get_style(styles, children[index]), horizontal);
    }
    if (auto_sides == 0) return;
    const float share = free / static_cast<float>(auto_sides);
    for (const size_t index : line.item_indices) {
        const auto child_style = get_style(styles, children[index]);
        if (horizontal) {
            add_auto_margin_space(boxes[index],
                is_auto_margin(child_style.margin.left) ? share : 0.f, 0.f,
                is_auto_margin(child_style.margin.right) ? share : 0.f, 0.f);
        } else {
            add_auto_margin_space(boxes[index], 0.f,
                is_auto_margin(child_style.margin.top) ? share : 0.f, 0.f,
                is_auto_margin(child_style.margin.bottom) ? share : 0.f);
        }
    }
    used_main_size += free;
}

/* ── LayoutFlexChildren ────────────────────────────────────────────── */

float layout_flex_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf content,
    const aether_computed_style_t& style,
    bool has_definite_height,
    aether_layout_result_s* result)
{
    const auto& children = collect_flex_items(styles, result, parent, style);

    if (children.empty()) {
        // flex container with only text content — fall back to inline flow
        std::vector<inline_item_s> inline_items;
        for (auto* node : parent->children) {
            if (node->type == AETHER_NODE_TEXT) {
                collect_text_items(parent, style, node->text, inline_items);
            }
        }
        if (inline_items.empty()) return 0.f;
        // Text nodes are anonymous flex items. In a row they align on the main axis through
        // justify-content; in a column, align-items controls their horizontal position.
        auto inline_style = style;
        if (style.flex_direction == AETHER_FLEX_DIRECTION_ROW) {
            inline_style.text_align = style.justify_content == AETHER_JUSTIFY_CENTER
                ? AETHER_TEXT_ALIGN_CENTER
                : style.justify_content == AETHER_JUSTIFY_END
                    ? AETHER_TEXT_ALIGN_END
                    : AETHER_TEXT_ALIGN_START;
        } else {
            inline_style.text_align = style.align_items == AETHER_ALIGN_CENTER
                ? AETHER_TEXT_ALIGN_CENTER
                : style.align_items == AETHER_ALIGN_END
                    ? AETHER_TEXT_ALIGN_END
                    : AETHER_TEXT_ALIGN_START;
        }
        float used_height = layout_inline_items(engine, styles, parent_box, inline_items,
                                                content.x, content.y, content.width, inline_style, result);
        if (has_definite_height) {
            float vertical_free = std::max(0.f, content.height - used_height);
            float dy = 0.f;
            const int vertical_alignment = style.flex_direction == AETHER_FLEX_DIRECTION_ROW
                ? style.align_items
                : style.justify_content;
            if (vertical_alignment == AETHER_ALIGN_CENTER || vertical_alignment == AETHER_JUSTIFY_CENTER) dy = vertical_free / 2;
            else if (vertical_alignment == AETHER_ALIGN_END || vertical_alignment == AETHER_JUSTIFY_END) dy = vertical_free;
            if (dy != 0.f) {
                for (auto* child : parent_box->children) {
                    offset_box(child, 0, dy);
                }
            }
        }
        return used_height;
    }

    const bool horizontal = style.flex_direction != AETHER_FLEX_DIRECTION_COLUMN;
    const bool main_size_is_definite = horizontal || has_definite_height;
    const bool cross_size_is_definite = horizontal ? has_definite_height : true;
    auto resolved = resolve_flex_children(engine, styles, children, content, style,
                                          horizontal, main_size_is_definite, cross_size_is_definite, result);
    const float gap = std::max(0.f, style.gap);

    if (!horizontal) {
        float cross_cursor = content.x;
        float total_main_size = 0.f;
        for (const auto& line : resolved.resolution.lines) {
            float used_main_size = flex_line_used_main_size(resolved.boxes, line, false, gap);
            if (main_size_is_definite) {
                consume_flex_main_auto_margins(resolved.boxes, children, styles, line, false,
                                               content.height, used_main_size);
            }
            const float free = main_size_is_definite ? std::max(0.f, content.height - used_main_size) : 0.f;
            float spacing = gap;
            float cursor_y = content.y;
            if (style.justify_content == AETHER_JUSTIFY_CENTER) cursor_y += free / 2.f;
            else if (style.justify_content == AETHER_JUSTIFY_END) cursor_y += free;
            if (style.justify_content == AETHER_JUSTIFY_SPACE_BETWEEN && line.item_indices.size() > 1) {
                spacing += free / static_cast<float>(line.item_indices.size() - 1);
            }

            const float cross_extent = resolve_flex_line_cross_size(
                engine, styles, children, resolved, line, content, style,
                false, cross_size_is_definite, resolved.resolution.lines.size() == 1, result);
            for (const size_t index : line.item_indices) {
                auto* child_box = resolved.boxes[index];
                const auto child_style = get_style(styles, children[index]);
                const bool consumed_cross_auto = consume_flex_cross_auto_margins(
                    child_box, child_style, true, cross_extent);
                float x = cross_cursor;
                if (!consumed_cross_auto) {
                    const int32_t alignment = flex_cross_alignment(style, child_style);
                    if (alignment == AETHER_ALIGN_CENTER) x += (cross_extent - child_box->margin_box.width) / 2.f;
                    else if (alignment == AETHER_ALIGN_END) x += cross_extent - child_box->margin_box.width;
                }
                const auto flow_margin = normal_flow_margin_box(child_box);
                offset_box(child_box, x - flow_margin.x, cursor_y - flow_margin.y);
                parent_box->add_child(child_box);
                cursor_y += child_box->margin_box.height + spacing;
            }
            total_main_size = std::max(total_main_size, used_main_size);
            cross_cursor += cross_extent + gap;
        }
        return has_definite_height ? std::max(total_main_size, content.height) : total_main_size;
    }

    float cross_cursor = content.y;
    float total_cross_size = 0.f;
    for (const auto& line : resolved.resolution.lines) {
        float used_main_size = flex_line_used_main_size(resolved.boxes, line, true, gap);
        consume_flex_main_auto_margins(resolved.boxes, children, styles, line, true,
                                       content.width, used_main_size);
        const float free = std::max(0.f, content.width - used_main_size);
        float spacing = gap;
        float cursor_x = content.x;
        if (style.justify_content == AETHER_JUSTIFY_CENTER) cursor_x += free / 2.f;
        else if (style.justify_content == AETHER_JUSTIFY_END) cursor_x += free;
        if (style.justify_content == AETHER_JUSTIFY_SPACE_BETWEEN && line.item_indices.size() > 1) {
            spacing += free / static_cast<float>(line.item_indices.size() - 1);
        }

        const float cross_extent = resolve_flex_line_cross_size(
            engine, styles, children, resolved, line, content, style,
            true, cross_size_is_definite, resolved.resolution.lines.size() == 1, result);
        for (const size_t index : line.item_indices) {
            auto* child_box = resolved.boxes[index];
            const auto child_style = get_style(styles, children[index]);
            const bool consumed_cross_auto = consume_flex_cross_auto_margins(
                child_box, child_style, false, cross_extent);
            float y = cross_cursor;
            if (!consumed_cross_auto) {
                const int32_t alignment = flex_cross_alignment(style, child_style);
                if (alignment == AETHER_ALIGN_CENTER) y += (cross_extent - child_box->margin_box.height) / 2.f;
                else if (alignment == AETHER_ALIGN_END) y += cross_extent - child_box->margin_box.height;
            }
            const auto flow_margin = normal_flow_margin_box(child_box);
            offset_box(child_box, cursor_x - flow_margin.x, y - flow_margin.y);
            parent_box->add_child(child_box);
            cursor_x += child_box->margin_box.width + spacing;
        }
        total_cross_size += cross_extent;
        cross_cursor += cross_extent + gap;
    }
    total_cross_size += gap * static_cast<float>(resolved.resolution.lines.empty() ? 0 : resolved.resolution.lines.size() - 1);
    return has_definite_height ? std::max(total_cross_size, content.height) : total_cross_size;
}
