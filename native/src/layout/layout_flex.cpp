/* ── Flex layout — anonymous items, §9 resolution, lines and alignment ─ */
#include "layout_internal.h"

#include <algorithm>
#include "../primitives/presentation_text.h"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

static std::string normalize_whitespace(const std::string& value) {
    std::string result;
    bool prev_space = true; // start true so leading spaces are skipped
    for (char c : value) {
        if (aether::presentation_space_byte(static_cast<unsigned char>(c))) {
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
    const StyleLookup* styles,
    aether_layout_result_s* result,
    aether_node_s* parent,
    const BoxStyleSnapshot& parent_style,
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

    using namespace aether::style;
    auto anonymous_owner = std::make_shared<aether_node_s>();
    auto* anonymous = anonymous_owner.get();
    anonymous->type = AETHER_NODE_ELEMENT;
    anonymous->parent = parent;
    anonymous->tag_name = "aether-anonymous-flex-item";
    anonymous->style_origin = parent;
    anonymous->anonymous_flex_owner = parent;
    anonymous->children = text_nodes;

    // Fresh scalar values and only the inherited shadow payload, never box decoration.
    auto scalars = styles->default_style().scalars();
    scalars.color = parent_style.color;
    std::strncpy(scalars.font_family.data(), parent_style.font_family.data(), scalars.font_family.size() - 1);
    scalars.font_size = parent_style.font_size;
    scalars.font_weight = parent_style.font_weight;
    scalars.line_height = parent_style.line_height;
    scalars.text_align = parent_style.text_align;
    scalars.text_transform = parent_style.text_transform;
    scalars.white_space = parent_style.white_space;
    scalars.cursor = parent_style.cursor;
    scalars.visible = parent_style.visible;
    scalars.pointer_events = parent_style.pointer_events;
    scalars.display = AETHER_DISPLAY_BLOCK;
    const auto shadows = parent_style.view().text_shadows();
    auto payload = std::make_shared<const FrozenStylePayload>(ImmutableArray<FrozenBackgroundLayer>{},
        ImmutableArray<aether_backdrop_filter>{}, FrozenGradient{}, ImmutableArray<aether_transform_operation>{},
        ImmutableArray<aether_shadow>::generate(shadows.size(), [&](size_t i) { return shadows[i]; }),
        ImmutableArray<aether_shadow>{});
    auto frozen_style = std::make_shared<const FrozenStyle>(scalars, std::move(payload));
    FrozenMeasuresRef frozen_measures;
    if (const auto parent_measures = styles->lookup_measures_owner(parent)) {
        auto values = parent_measures->scalars();
        values.row_gap = values.column_gap = aether_css_length_zero();
        const auto positions = parent_measures->background_positions();
        const auto sizes = parent_measures->background_sizes();
        const auto dependencies = parent_measures->property_dependencies();
        frozen_measures = std::make_shared<const FrozenMeasures>(values, parent_measures->document_owner(),
            ImmutableArray<MeasureBackgroundPosition>::generate(positions.size(), [&](size_t i) { return positions[i]; }),
            ImmutableArray<MeasureBackgroundSize>::generate(sizes.size(), [&](size_t i) { return sizes[i]; }),
            ImmutableArray<FrozenPropertyDependency>::generate(dependencies.size(), [&](size_t i) {
                const auto name = dependencies[i].name();
                return FrozenPropertyDependency({std::string_view(name.c_str(), name.size()), dependencies[i].flags()});
            }));
    }
    result->build_overlay->insert(std::move(anonymous_owner), std::move(frozen_style), std::move(frozen_measures));
    return anonymous;
}

const std::vector<aether_node_s*>& collect_flex_items(
    const StyleLookup* styles,
    aether_layout_result_s* result,
    aether_node_s* parent,
    const BoxStyleSnapshot& parent_style)
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
        // Comments and boxes suppressed by display:none do not interrupt the
        // CSS text sequence. Visible out-of-flow boxes do form a boundary.
        if (node->type != AETHER_NODE_ELEMENT) continue;
        const auto child_style = get_style(styles, node);
        if (child_style.display == AETHER_DISPLAY_NONE) continue;
        flush_direct_text();
        const bool out_of_flow = child_style.position == AETHER_POSITION_ABSOLUTE ||
            child_style.position == AETHER_POSITION_FIXED;
        if (!out_of_flow) {
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

static int auto_margin_count(const BoxStyleSnapshot& style, bool horizontal) {
    return horizontal
        ? static_cast<int>(is_auto_margin(style.margin.left)) + static_cast<int>(is_auto_margin(style.margin.right))
        : static_cast<int>(is_auto_margin(style.margin.top)) + static_cast<int>(is_auto_margin(style.margin.bottom));
}

static bool consume_flex_cross_auto_margins(aether_layout_box_s* box,
                                             const BoxStyleSnapshot& style,
                                             bool horizontal_cross_axis,
                                             float available_cross_size) {
    const bool before_auto = horizontal_cross_axis ? is_auto_margin(style.margin.left) : is_auto_margin(style.margin.top);
    const bool after_auto = horizontal_cross_axis ? is_auto_margin(style.margin.right) : is_auto_margin(style.margin.bottom);
    const float used_size = horizontal_cross_axis ? box->margin_box.width : box->margin_box.height;
    const auto resolved = aether_resolve_auto_margin_pair(
        available_cross_size, used_size, 0.f, 0.f, before_auto, after_auto);
    if (!before_auto && !after_auto) return false;
    // Auto margins override cross alignment by presence, even during overflow.
    if (horizontal_cross_axis) add_auto_margin_space(box, resolved.before, 0.f, resolved.after, 0.f);
    else add_auto_margin_space(box, 0.f, resolved.before, 0.f, resolved.after);
    return true;
}

int32_t flex_cross_alignment(const BoxStyleSnapshot& container,
                             const BoxStyleSnapshot& item) {
    return item.align_self == AETHER_ALIGN_SELF_AUTO
        ? container.align_items
        : item.align_self;
}

struct resolved_flex_children_s {
    std::vector<aether_layout_box_s*> boxes;
    aether_flex_resolve_result_s resolution;
    std::vector<bool> definite_main_heights;
};

static bool flex_length_is_definite(aether_css_length length, bool main_size_is_definite,
    const std::optional<aether::style::MeasuresReadView>& measures, const char* property) {
    if (layout_measure_has_percentage(measures, property, length)) return main_size_is_definite;
    return length.unit != AETHER_CSS_UNIT_AUTO;
}

static float resolve_flex_main_border_size(const aether_layout_engine_s* engine,
                                           const BoxStyleSnapshot& style,
                                           const aether_rectf& content,
                                           bool horizontal, bool main_size_is_definite,
                                           aether_css_length length, const std::optional<aether::style::MeasuresReadView>& measures, const char* property) {
    if (!flex_length_is_definite(length, main_size_is_definite, measures, property)) return NAN;
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

static bool flex_cross_margins_are_auto(const BoxStyleSnapshot& style, bool horizontal) {
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

static bool flex_item_stretches_on_cross_axis(const BoxStyleSnapshot& container,
                                               const BoxStyleSnapshot& item,
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
    const StyleLookup* styles,
    const std::vector<aether_node_s*>& children,
    aether_node_s* parent, const aether_rectf& content,
    percentage_height_s containing_height,
    const BoxStyleSnapshot& parent_style,
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
                {content.x, content.y, content.width, content.height}, containing_height, content.y,
                false,
                horizontal ? 0.f : forced_border_cross, stretch_base_cross && !horizontal,
                horizontal ? forced_border_cross : 0.f, stretch_base_cross && horizontal
                    ? forced_height_kind_e::definite : forced_height_kind_e::none,
                true, result));
        }
    }

    std::vector<aether_flex_item_input_s> inputs;
    inputs.reserve(children.size());
    for (size_t index = 0; index < children.size(); ++index) {
        const auto child_style = get_style(styles, children[index]);
        const auto measures = layout_measures(styles, children[index]);
        const char* main_property = horizontal ? "width" : "height";
        const auto* base_box = base_boxes[index];
        aether_flex_item_input_s item{};
        item.basis.kind = child_style.flex_basis.kind;
        item.basis.content_main_size = flex_box_main_size(base_box, horizontal);
        item.basis.specified_main_size = resolve_flex_main_border_size(
            engine, child_style, content, horizontal, main_size_is_definite,
            horizontal ? child_style.width : child_style.height, measures, main_property);
        item.basis.resolved_length = resolve_flex_main_border_size(
            engine, child_style, content, horizontal, main_size_is_definite,
            child_style.flex_basis.length, measures, "flex-basis");
        item.basis.length_is_definite = child_style.flex_basis.kind == AETHER_FLEX_BASIS_LENGTH &&
            flex_length_is_definite(child_style.flex_basis.length, main_size_is_definite, measures, "flex-basis");
        // Follow the used basis: auto can transfer a definite specified main size;
        // content and unresolved percentage bases remain content-dependent.
        const auto semantic_main_is_definite = [&](aether_css_length length, float resolved, const char* property) {
            return std::isfinite(resolved) &&
                (!layout_measure_has_percentage(measures, property, length) || horizontal || containing_height.has_value());
        };
        const bool used_basis_is_definite =
            (item.basis.kind == AETHER_FLEX_BASIS_LENGTH &&
             semantic_main_is_definite(child_style.flex_basis.length, item.basis.resolved_length, "flex-basis")) ||
            (item.basis.kind == AETHER_FLEX_BASIS_AUTO &&
             semantic_main_is_definite(horizontal ? child_style.width : child_style.height,
                                       item.basis.specified_main_size, main_property));
        output.definite_main_heights.push_back(!horizontal &&
            (containing_height.has_value() || used_basis_is_definite));
        item.min_main_size = resolve_flex_main_border_size(
            engine, child_style, content, horizontal, main_size_is_definite,
            horizontal ? child_style.min_width : child_style.min_height, measures, horizontal ? "min-width" : "min-height");
        item.max_main_size = resolve_flex_main_border_size(
            engine, child_style, content, horizontal, main_size_is_definite,
            horizontal ? child_style.max_width : child_style.max_height, measures, horizontal ? "max-width" : "max-height");
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
        const bool automatic_main_size = !flex_length_is_definite(main_size_property, main_size_is_definite, measures, main_property);
        const float content_size_suggestion = horizontal
            ? measure_intrinsic_width(engine, styles, children[index], child_style,
                                      child_padding, child_border, content.width, result,
                                      aether_intrinsic_size_mode_e::min_content)
            : automatic_main_size
                ? flex_box_main_size(base_box, false)
                : measure_intrinsic_height(engine, styles, result, children[index], child_style,
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
        &inputs, available_main_size, layout_used_gap(styles, parent, parent_style, !horizontal, horizontal ? content.width : content.height, horizontal || containing_height.has_value()),
        parent_style.flex_wrap && main_size_is_definite
    });

    for (auto* base_box : base_boxes) delete base_box;
    base_boxes.clear();

    for (size_t index = 0; index < children.size(); ++index) {
        const float target_main_size = output.resolution.items[index].target_main_size;
        output.boxes.push_back(layout_element(engine, styles, children[index],
            {content.x, content.y, content.width, content.height}, containing_height, content.y,
            false,
            horizontal ? target_main_size : 0.f, horizontal,
            horizontal ? 0.f : target_main_size,
            horizontal ? forced_height_kind_e::none : output.definite_main_heights[index]
                ? forced_height_kind_e::definite : forced_height_kind_e::indefinite,
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

/* Once line sizes are final, stretch only eligible items. Constraints can make
   an item overflow its line, but cannot grow that line or re-stretch siblings. */
static void stretch_flex_line_items(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    const std::vector<aether_node_s*>& children,
    resolved_flex_children_s& resolved,
    const aether_flex_line_s& line,
    const aether_rectf& content,
    percentage_height_s containing_height,
    const BoxStyleSnapshot& container_style,
    bool horizontal, float target_cross_size,
    aether_layout_result_s* result) {
    for (const size_t index : line.item_indices) {
        const auto child_style = get_style(styles, children[index]);
        if (!flex_item_stretches_on_cross_axis(container_style, child_style, horizontal)) continue;

        auto* previous = resolved.boxes[index];
        const float main_size = flex_box_main_size(previous, horizontal);
        // The outer-minus-border difference is the signed margin sum. Negative
        // fixed margins enlarge the stretched border box; auto margins are zero.
        const float cross_margins = flex_box_cross_size(previous, horizontal) -
            flex_box_border_cross_size(previous, horizontal);
        const float forced_border_cross_size = std::max(0.f, target_cross_size - cross_margins);
        delete previous;

        resolved.boxes[index] = layout_element(engine, styles, children[index],
            {content.x, content.y, content.width, content.height}, containing_height, content.y,
            false,
            horizontal ? main_size : forced_border_cross_size, true,
            horizontal ? forced_border_cross_size : main_size,
            horizontal || resolved.definite_main_heights[index]
                ? forced_height_kind_e::definite : forced_height_kind_e::indefinite,
            true, result);
    }
}

static void consume_flex_main_auto_margins(const std::vector<aether_layout_box_s*>& boxes,
                                           const std::vector<aether_node_s*>& children,
                                           const StyleLookup* styles,
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
    const StyleLookup* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf content,
    percentage_height_s containing_height,
    const BoxStyleSnapshot& style,
    bool has_definite_height,
    percentage_height_s container_height_reference,
    float height_decorations,
    aether_layout_result_s* result)
{
    const auto& children = collect_flex_items(styles, result, parent, style);

    // The collector is authoritative, including suppression of whitespace-only
    // sequences. Never reconstruct discarded text through an inline fallback.
    if (children.empty()) return 0.f;

    const bool horizontal = style.flex_direction != AETHER_FLEX_DIRECTION_COLUMN;
    const bool main_size_is_definite = horizontal || has_definite_height;
    const bool cross_size_is_definite = !horizontal || containing_height.has_value();
    auto resolved = resolve_flex_children(engine, styles, children, parent, content, containing_height, style,
                                          horizontal, main_size_is_definite, cross_size_is_definite, result);
    const float gap = layout_used_gap(styles, parent, style, !horizontal,
        horizontal ? content.width : content.height, horizontal || containing_height.has_value());
    const float cross_gap = layout_used_gap(styles, parent, style, horizontal,
        horizontal ? content.height : content.width, !horizontal || containing_height.has_value());

    // Measure all natural line sizes before any box consumes auto margins or
    // is reconstructed for stretch. Lines own disjoint item indices, so the
    // second pass can reread each untouched line without temporary allocation.
    const auto& lines = resolved.resolution.lines;
    float natural_cross_size = cross_gap * static_cast<float>(lines.empty() ? 0 : lines.size() - 1);
    for (const auto& line : lines) {
        natural_cross_size += flex_line_cross_size(resolved.boxes, line, horizontal);
    }
    const float used_cross_size = horizontal
        ? clamp_border_height(engine, style,
              (has_definite_height ? content.height : natural_cross_size) + height_decorations,
              container_height_reference.value_or(NAN), height_decorations, true, parent) - height_decorations
        : content.width;
    // Only the effective initial/stretch policy is represented by the style.
    // A wrapping container keeps natural lines on overflow, even for one line.
    const float line_stretch = style.flex_wrap
        ? std::max(0.f, used_cross_size - natural_cross_size) / static_cast<float>(lines.empty() ? 1 : lines.size())
        : 0.f;

    float cross_cursor = horizontal ? content.y : content.x;
    float total_main_size = 0.f;
    for (const auto& line : lines) {
        const float cross_extent = style.flex_wrap
            ? flex_line_cross_size(resolved.boxes, line, horizontal) + line_stretch
            : used_cross_size;
        stretch_flex_line_items(engine, styles, children, resolved, line,
            content, containing_height, style, horizontal, cross_extent, result);

        // Stretch can replace a box. Consume main auto margins only on the
        // surviving boxes, otherwise reconstruction discards their used space.
        float used_main_size = flex_line_used_main_size(resolved.boxes, line, horizontal, gap);
        const float available_main_size = horizontal ? content.width : content.height;
        if (main_size_is_definite) {
            consume_flex_main_auto_margins(resolved.boxes, children, styles, line, horizontal,
                available_main_size, used_main_size);
        }
        // Positioning preserves overflow; only distributed spacing is nonnegative.
        const float free = main_size_is_definite ? available_main_size - used_main_size : 0.f;
        float spacing = gap;
        float main_cursor = horizontal ? content.x : content.y;
        if (style.justify_content == AETHER_JUSTIFY_CENTER) main_cursor += free / 2.f;
        else if (style.justify_content == AETHER_JUSTIFY_END) main_cursor += free;
        if (style.justify_content == AETHER_JUSTIFY_SPACE_BETWEEN && line.item_indices.size() > 1) {
            spacing += std::max(0.f, free) / static_cast<float>(line.item_indices.size() - 1);
        }

        for (const size_t index : line.item_indices) {
            auto* child_box = resolved.boxes[index];
            const auto child_style = get_style(styles, children[index]);
            const bool has_cross_auto_margins = consume_flex_cross_auto_margins(
                child_box, child_style, !horizontal, cross_extent);
            float cross_position = cross_cursor;
            if (!has_cross_auto_margins) {
                const float cross_free = cross_extent - flex_box_cross_size(child_box, horizontal);
                const int32_t alignment = flex_cross_alignment(style, child_style);
                if (alignment == AETHER_ALIGN_CENTER) cross_position += cross_free / 2.f;
                else if (alignment == AETHER_ALIGN_END) cross_position += cross_free;
            }
            const auto flow_margin = normal_flow_margin_box(child_box);
            offset_box(child_box,
                (horizontal ? main_cursor : cross_position) - flow_margin.x,
                (horizontal ? cross_position : main_cursor) - flow_margin.y);
            parent_box->add_child(child_box);
            main_cursor += flex_box_outer_main_size(child_box, horizontal) + spacing;
        }
        total_main_size = std::max(total_main_size, used_main_size);
        cross_cursor += cross_extent + cross_gap;
    }
    // Row sizing returns the already-clamped used size, not overflow extent.
    // Descendant overflow is still collected separately by layout_element.
    if (horizontal) return used_cross_size;
    return has_definite_height ? std::max(total_main_size, content.height) : total_main_size;
}
