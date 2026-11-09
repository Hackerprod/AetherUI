#include "../diagnostics/work_counters.h"
using aether::diagnostics::count_work;
using aether::diagnostics::WorkCounter;
/* ── Layout Engine — core box layout, flow, inline, and intrinsic sizing ─ */
#include "layout_internal.h"
#include <aether/aether_contextual_transitions.h>
#include "input_hit_test.h"
#include "../animations/transition_runtime.h"
#include <utility>

#include <algorithm>
#include "../primitives/presentation_text.h"
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

/* ─────────────────────────── helpers ────────────────────────────── */

struct opaque_control_size_s {
    float content_width;
    float content_height;
};

static bool is_opaque_single_line_control(aether_node_s* element) {
    return aether_form_control_is_opaque_single_line(element);
}

static opaque_control_size_s measure_opaque_single_line_control(
    const aether_layout_engine_s* engine,
    aether_node_s* element,
    const BoxStyleSnapshot& style)
{
    const float min_height = std::max(style.font_size * 1.1f, layout_used_line_height(engine, element, style));
    const std::string text = aether_opaque_control_display_text(element, style);
    if (text.empty()) return {0.f, min_height};
    const auto metrics = engine_measure_text(engine, text.c_str(), &style, 0.f, element);
    return {metrics.width, std::max(metrics.height, min_height)};
}


static float resolve_length(aether_css_length l, float reference,
                             float font_size, float root_font_size,
                             float vp_w, float vp_h) {
    return aether_css_length_resolve(l, reference, font_size, root_font_size, vp_w, vp_h);
}

float resolve(const aether_layout_engine_s* e,
              aether_css_length value, float reference,
              const BoxStyleSnapshot& style) {
    const float used = resolve_length(value, reference, style.font_size,
                          e ? e->root_font_size : 16.f,
                          e ? e->viewport_width : 0.f,
                          e ? e->viewport_height : 0.f);
    if (value.unit != AETHER_CSS_UNIT_AUTO && !std::isfinite(used))
        throw std::overflow_error("Non-finite used CSS length");
    return used;
}

// Keep all four validations ahead of normalization and border-style suppression.
static aether_thicknessf resolve_checked_edges(const aether_layout_engine_s* e,
                                               aether_css_edges edges, float reference,
                                               const BoxStyleSnapshot& style) {
    const float left = resolve(e, edges.left, reference, style);
    const float top = resolve(e, edges.top, reference, style);
    const float right = resolve(e, edges.right, reference, style);
    const float bottom = resolve(e, edges.bottom, reference, style);
    return {left, top, right, bottom};
}

static aether_thicknessf normalize_checked_edges(aether_thicknessf edges, bool allow_negative) {
    const auto normalize = [allow_negative](float value) {
        // resolve permits non-finite values only for AUTO, whose used edge is zero.
        if (!std::isfinite(value)) return 0.f;
        return allow_negative ? value : std::max(0.f, value);
    };
    return {normalize(edges.left), normalize(edges.top),
            normalize(edges.right), normalize(edges.bottom)};
}

aether_thicknessf resolve_padding_edges(const aether_layout_engine_s* e,
                                        aether_css_edges edges, float reference,
                                        const BoxStyleSnapshot& style) {
    return normalize_checked_edges(resolve_checked_edges(e, edges, reference, style), false);
}

aether_thicknessf resolve_margin_edges(const aether_layout_engine_s* e,
                                       aether_css_edges edges, float reference,
                                       const BoxStyleSnapshot& style) {
    return normalize_checked_edges(resolve_checked_edges(e, edges, reference, style), true);
}

aether_thicknessf resolve_border_edges(const aether_layout_engine_s* e,
                                       const BoxStyleSnapshot& style,
                                       float reference) {
    auto edges = normalize_checked_edges(resolve_checked_edges(e, style.border_width, reference, style), false);
    if (!aether_border_style_occupies_space(style.border_styles.left)) edges.left = 0;
    if (!aether_border_style_occupies_space(style.border_styles.top)) edges.top = 0;
    if (!aether_border_style_occupies_space(style.border_styles.right)) edges.right = 0;
    if (!aether_border_style_occupies_space(style.border_styles.bottom)) edges.bottom = 0;
    return edges;
}

BoxStyleSnapshot get_style(const StyleLookup* styles, aether_node_s* el) {
    auto owner = el ? styles->lookup_node_owner(el) : aether::style::FrozenStyleRef{};
    return BoxStyleSnapshot(owner ? std::move(owner) : styles->default_owner());
}

bool is_out_of_flow(const BoxStyleSnapshot& style) {
    return style.position == AETHER_POSITION_ABSOLUTE ||
           style.position == AETHER_POSITION_FIXED;
}

static void offset_rect(aether_rectf& r, float dx, float dy) {
    r.x += dx;
    r.y += dy;
}

static void offset_box_descendants(aether_layout_box_s* box, float dx, float dy,
                                   bool local_fixed_container) {
    offset_rect(box->margin_box, dx, dy);
    offset_rect(box->border_box, dx, dy);
    offset_rect(box->padding_box, dx, dy);
    offset_rect(box->content_box, dx, dy);
    const auto* result = box->owner;
    const bool prepared = result && result->effective_context_enabled;
    local_fixed_container = local_fixed_container ||
        (prepared && box->kind == LAYOUT_BOX_ELEMENT &&
         result->effective_fixed_container(box->element));
    for (auto* child : box->children) {
        float child_dx = dx, child_dy = dy;
        if ((prepared || box->style.display == AETHER_DISPLAY_FLEX) &&
            !local_fixed_container && child->kind == LAYOUT_BOX_ELEMENT &&
            child->style.position == AETHER_POSITION_FIXED) {
            if (child->style.inset.left.unit != AETHER_CSS_UNIT_AUTO ||
                child->style.inset.right.unit != AETHER_CSS_UNIT_AUTO) child_dx = 0.f;
            if (child->style.inset.top.unit != AETHER_CSS_UNIT_AUTO ||
                child->style.inset.bottom.unit != AETHER_CSS_UNIT_AUTO) child_dy = 0.f;
        }
        if (child_dx != 0.f || child_dy != 0.f)
            offset_box_descendants(child, child_dx, child_dy, local_fixed_container);
    }
}

void offset_box(aether_layout_box_s* box, float dx, float dy) {
    offset_box_descendants(box, dx, dy, false);
}

aether_rectf normal_flow_border_box(const aether_layout_box_s* box) {
    auto rect = box->border_box;
    offset_rect(rect, -box->own_relative_dx, -box->own_relative_dy);
    return rect;
}

aether_rectf normal_flow_margin_box(const aether_layout_box_s* box) {
    auto rect = box->margin_box;
    offset_rect(rect, -box->own_relative_dx, -box->own_relative_dy);
    return rect;
}

static void apply_relative_visual_offset(aether_layout_box_s* box, float dx, float dy) {
    offset_rect(box->margin_box, dx, dy);
    offset_rect(box->border_box, dx, dy);
    offset_rect(box->padding_box, dx, dy);
    offset_rect(box->content_box, dx, dy);
    for (auto* child : box->children) {
        float child_dx = dx, child_dy = dy;
        if (child->style.position == AETHER_POSITION_FIXED) {
            // Only direct flex static axes follow the parent's visual movement.
            // Explicit viewport insets and unrelated non-flex fixed anchors stay put.
            if (box->style.display != AETHER_DISPLAY_FLEX) continue;
            if (child->style.inset.left.unit != AETHER_CSS_UNIT_AUTO ||
                child->style.inset.right.unit != AETHER_CSS_UNIT_AUTO) child_dx = 0.f;
            if (child->style.inset.top.unit != AETHER_CSS_UNIT_AUTO ||
                child->style.inset.bottom.unit != AETHER_CSS_UNIT_AUTO) child_dy = 0.f;
        }
        if (child_dx != 0.f || child_dy != 0.f)
            apply_relative_visual_offset(child, child_dx, child_dy);
    }
}

bool is_auto_margin(aether_css_length value) {
    return value.unit == AETHER_CSS_UNIT_AUTO;
}

/* ── apply line alignment ─────────────────────────────────────────── */

struct line_range {
    int start;
    int end;
    float width;
    bool ends_with_forced_break;
};

static bool is_whitespace_fragment(const aether_layout_box_s* box) {
    return box && box->kind == LAYOUT_BOX_TEXT && box->text == " ";
}

struct line_content_bounds_s {
    int first = 0;
    int last = -1;
};

static line_content_bounds_s find_line_content_bounds(const aether_layout_box_s* parent,
                                                       const line_range& line) {
    line_content_bounds_s bounds{line.start, line.end - 1};
    while (bounds.first < line.end && is_whitespace_fragment(parent->children[bounds.first])) {
        ++bounds.first;
    }
    while (bounds.last >= bounds.first && is_whitespace_fragment(parent->children[bounds.last])) {
        --bounds.last;
    }
    return bounds;
}

static int count_distributable_spaces(const aether_layout_box_s* parent, const line_range& line) {
    const auto bounds = find_line_content_bounds(parent, line);
    int count = 0;
    for (int index = bounds.first; index <= bounds.last; ++index) {
        count += is_whitespace_fragment(parent->children[index]) ? 1 : 0;
    }
    return count;
}

static void expand_box_width(aether_layout_box_s* box, float extra_width) {
    box->margin_box.width += extra_width;
    box->border_box.width += extra_width;
    box->padding_box.width += extra_width;
    box->content_box.width += extra_width;
}

static void justify_line(aether_layout_box_s* parent, const line_range& line,
                         float available_width) {
    const auto bounds = find_line_content_bounds(parent, line);
    const int gap_count = count_distributable_spaces(parent, line);
    const float extra_per_gap =
        aether_text_justify_extra_per_gap(available_width, line.width, gap_count);
    if (extra_per_gap <= 0.f) return;

    float accumulated_offset = 0.f;
    for (int index = line.start; index < line.end; ++index) {
        auto* child = parent->children[index];
        if (accumulated_offset > 0.f) offset_box(child, accumulated_offset, 0.f);
        if (index >= bounds.first && index <= bounds.last && is_whitespace_fragment(child)) {
            expand_box_width(child, extra_per_gap);
            accumulated_offset += extra_per_gap;
        }
    }
}

static void apply_line_alignment(aether_layout_box_s* parent,
                                 const std::vector<line_range>& lines,
                                 float available_width,
                                 int32_t align) {
    for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const auto& line = lines[line_index];
        const bool is_last_line = line_index + 1 == lines.size();
        const int gap_count = count_distributable_spaces(parent, line);
        if (aether_text_line_should_justify(align, is_last_line,
                                             line.ends_with_forced_break, gap_count,
                                             available_width, line.width)) {
            justify_line(parent, line, available_width);
            continue;
        }

        const bool align_to_end = align == AETHER_TEXT_ALIGN_END ||
                                  align == AETHER_TEXT_ALIGN_RIGHT;
        const bool align_to_center = align == AETHER_TEXT_ALIGN_CENTER;
        if (!align_to_end && !align_to_center) continue;
        float offset = align_to_center
            ? (available_width - line.width) / 2.f
            : available_width - line.width;
        if (offset <= 0) continue;
        for (int i = line.start; i < line.end; i++) {
            offset_box(parent->children[i], offset, 0);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   LayoutEngine — core layout methods
   ══════════════════════════════════════════════════════════════════════ */

static float layout_atomic_control(
    const aether_layout_engine_s* engine,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    float content_width,
    float explicit_height, bool has_explicit_height,
    aether_thicknessf padding,
    aether_thicknessf border);

// One stack-local context per collected native inline sequence. It never escapes
// collection, so measurement callbacks cannot alter another sequence's state.
struct inline_capitalization_context_s {
    bool starts_word = true;
};

static void collect_text_items(aether_node_s* owner,
    const BoxStyleSnapshot& style, const std::string& text,
    std::vector<inline_item_s>& output, inline_capitalization_context_s& context);

static void collect_inline_element(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    std::vector<inline_item_s>& output, inline_capitalization_context_s& context);

enum class inline_node_role_e { ignored, inline_content, block_content };

static inline_node_role_e append_inline_node(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* parent,
    const BoxStyleSnapshot& parent_style,
    aether_node_s* node,
    std::vector<inline_item_s>& output, inline_capitalization_context_s& context);

static float measure_intrinsic_flex_width(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode);

static float measure_inline_items_intrinsic_width(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    const std::vector<inline_item_s>& items,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode);

static float measure_inline_items_height(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    const std::vector<inline_item_s>& items,
    float available_width,
    const BoxStyleSnapshot& parent_style,
    aether_layout_result_s* result, aether_node_s* owner);

/* ── Register ──────────────────────────────────────────────────────── */

void register_box(aether_layout_result_s* result, aether_layout_box_s* box) {
    if (!result->records_boxes) return;
    auto existing = result->primary_boxes.find(box->element);
    if (existing != result->primary_boxes.end()) {
        auto previous = std::find(result->paint_order.begin(), result->paint_order.end(), existing->second);
        if (previous != result->paint_order.end()) result->paint_order.erase(previous);
        existing->second = box;
    } else {
        result->primary_boxes.emplace(box->element, box);
    }
    result->paint_order.push_back(box);
}

/* ── LayoutElement ─────────────────────────────────────────────────── */

bool length_depends_on_percentage(aether_css_length value) {
    return value.unit == AETHER_CSS_UNIT_PERCENT ||
        (value.unit == AETHER_CSS_UNIT_LINEAR && value.linear_percent != 0.f);
}

// An unresolved percent acts as auto before opposing-inset precedence. A finite
// zero reference also keeps percentage-free LINEAR values free of NaN * 0.
static float resolve_semantic_height(const aether_layout_engine_s* engine,
                                     aether_css_length value,
                                     percentage_height_s height,
                                     const BoxStyleSnapshot& style, aether_node_s* element = nullptr, const char* property = nullptr) {
    const auto measures = layout_measures(engine ? engine->measure_styles : nullptr, element);
    const bool percentage = layout_measure_has_percentage(measures, property ? property : "", value);
    if (percentage && !height) return NAN;
    return resolve(engine, value, height.value_or(0.f), style);
}

float clamp_border_height(const aether_layout_engine_s* engine,
                                 const BoxStyleSnapshot& style,
                                 float height, float reference, float decorations,
                                 bool semantic_reference, aether_node_s* element) {
    const auto constraint = [&](aether_css_length value, const char* property) {
        if (semantic_reference && length_depends_on_percentage(value) && !std::isfinite(reference))
            return NAN;
        return resolve_semantic_height(engine, value, std::isfinite(reference) ? percentage_height_s(reference) : std::nullopt, style, element, property);
    };
    const float maximum = constraint(style.max_height, "max-height");
    if (std::isfinite(maximum)) height = std::min(height,
        maximum + (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX ? decorations : 0.f));
    const float minimum = constraint(style.min_height, "min-height");
    if (std::isfinite(minimum)) height = std::max(height,
        minimum + (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX ? decorations : 0.f));
    return std::max(decorations, height);
}

aether_layout_box_s* layout_element(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* element,
    aether_rectf containing_block,
    percentage_height_s containing_height,
    float normal_flow_y,
    bool force_available_width,
    float forced_border_width, bool has_forced_border_width,
    float forced_border_height, forced_height_kind_e forced_height_kind,
    bool is_independent_layout_item,
    aether_layout_result_s* result,
    const int32_t* viewport_overflow_override,
    element_completion_s* deferred_completion)
{
    auto style = get_style(styles, element);
    auto* box = result->create_box();
    box->kind = LAYOUT_BOX_ELEMENT;
    box->element = element;
    box->style = style;
    box->presentation_owner = element ? element->anonymous_flex_owner : nullptr;
    register_box(result, box);

    if (style.display == AETHER_DISPLAY_NONE) {
        return box;
    }

    auto margin     = resolve_margin_edges(engine, style.margin, containing_block.width, style);
    auto padding    = resolve_padding_edges(engine, style.padding, containing_block.width, style);
    auto border     = resolve_border_edges(engine, style, containing_block.width);
    box->used_border = border;
    box->collapsed_margin_before = margin.top;
    box->collapsed_margin_after = margin.bottom;

    float available_border_width = std::max(0.f, containing_block.width - aether_thicknessf_horizontal(margin));
    float specified_width = resolve(engine, style.width, containing_block.width, style);
    float border_width = specified_width;

    if (has_forced_border_width) {
        border_width = forced_border_width;
    } else if (!std::isfinite(border_width)) {
        border_width = force_available_width
            ? available_border_width
            : measure_intrinsic_width(engine, styles, element, style, padding, border,
                                      containing_block.width, result);
    } else if (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX) {
        border_width += aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    }

    float max_width = resolve(engine, style.max_width, containing_block.width, style);
    if (std::isfinite(max_width)) {
        float max_border = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? max_width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border)
            : max_width;
        border_width = std::min(border_width, max_border);
    }

    float min_width = resolve(engine, style.min_width, containing_block.width, style);
    if (std::isfinite(min_width)) {
        float min_border = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? min_width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border)
            : min_width;
        border_width = std::max(border_width, min_border);
    }

    // A definite CSS width is not limited by the containing block.  It may
    // legitimately overflow in either normal flow or out-of-flow positioning;
    // only auto/forced widths above are resolved from the available space.
    // Keep the decoration floor so a content-box cannot become smaller than its
    // own padding and border.
    border_width = std::max(aether_thicknessf_horizontal(border) + aether_thicknessf_horizontal(padding),
                            border_width);

    // Independent items (flex items, positioned boxes and atomic inline boxes)
    // resolve their margins in their owning layout algorithm rather than here.
    if (!is_independent_layout_item) {
        const auto resolved = aether_resolve_auto_margin_pair(
            containing_block.width, border_width, margin.left, margin.right,
            is_auto_margin(style.margin.left), is_auto_margin(style.margin.right));
        margin.left = resolved.before;
        margin.right = resolved.after;
    }

    float border_x = containing_block.x + margin.left;
    float border_y = normal_flow_y + margin.top;
    float content_x = border_x + border.left + padding.left;
    float content_y = border_y + border.top + padding.top;
    float content_width = std::max(0.f, border_width - aether_thicknessf_horizontal(border) - aether_thicknessf_horizontal(padding));

    float explicit_height;
    bool has_explicit_height;
    if (forced_height_kind != forced_height_kind_e::none) {
        explicit_height = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? std::max(0.f, forced_border_height - aether_thicknessf_vertical(padding) - aether_thicknessf_vertical(border))
            : forced_border_height;
        has_explicit_height = true;
    } else {
        explicit_height = resolve_semantic_height(engine, style.height, containing_height, style, element, "height");
        has_explicit_height = std::isfinite(explicit_height);
    }

    bool intrinsic_control = is_opaque_single_line_control(element);
    box->is_atomic_control = intrinsic_control;
    if (intrinsic_control) {
        box->control_display_text = aether_opaque_control_display_text(element, style);
        const auto value = element->attributes.find("value");
        box->control_shows_placeholder = value == element->attributes.end() || value->second.empty();
    }

    float definite_content_height;
    if (has_explicit_height) {
        definite_content_height = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? std::max(0.f, explicit_height)
            : std::max(0.f, explicit_height - aether_thicknessf_vertical(padding) - aether_thicknessf_vertical(border));
    } else {
        definite_content_height = NAN;
    }

    float child_containing_height = std::isfinite(definite_content_height)
        ? definite_content_height : containing_block.height;

    // Certify only the initial, content-independent height, before children.
    // A final numeric auto size or a min-height on auto is not such a proof.
    const float height_decorations = aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border);
    percentage_height_s child_percentage_height;
    float semantic_border_height = NAN;
    if (forced_height_kind == forced_height_kind_e::definite) {
        semantic_border_height = forced_border_height;
    } else if (forced_height_kind == forced_height_kind_e::none) {
        const float specified = resolve_semantic_height(engine, style.height, containing_height, style, element, "height");
        if (std::isfinite(specified)) semantic_border_height = specified +
            (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX ? height_decorations : 0.f);
    }
    if (std::isfinite(semantic_border_height)) {
        semantic_border_height = clamp_border_height(engine, style, semantic_border_height,
            containing_height.value_or(NAN), height_decorations, true, element);
        child_percentage_height = std::max(0.f, semantic_border_height - height_decorations);
    }
    if (child_percentage_height) child_containing_height = *child_percentage_height;

    float content_height;
    if (intrinsic_control) {
        content_height = layout_atomic_control(engine, element, style,
                                               content_width,
                                               explicit_height, has_explicit_height,
                                               padding, border);
    } else if (style.display == AETHER_DISPLAY_FLEX) {
        content_height = layout_flex_children(engine, styles, box, element,
                                              {content_x, content_y, content_width, child_containing_height},
                                              child_percentage_height,
                                              style,
                                              std::isfinite(definite_content_height),
                                              containing_height, height_decorations,
                                              result);
    } else if (style.display == AETHER_DISPLAY_TABLE) {
        content_height = layout_table_children(engine, styles, box, element,
                                               {content_x, content_y, content_width, child_containing_height},
                                              child_percentage_height,
                                               result);
    } else {
        content_height = layout_flow_children(engine, styles, box, element,
                                              {content_x, content_y, content_width, child_containing_height},
                                              child_percentage_height,
                                              style, result);
        // Chromium centers in-flow content inside form controls that carry it
        // (buttons): the line block floats in the middle of the control's definite
        // height instead of hugging the top — even when the line block is TALLER
        // than the content box (the overflow distributes symmetrically, e.g. a
        // large glyph in a small fixed-size close button). Ordinary block
        // containers keep the top-anchored flow position.
        if (aether_form_control_content_model(element) == AETHER_FORM_CONTROL_FLOW_CONTENT &&
            std::isfinite(definite_content_height)) {
            const float dy = aether_control_flow_centering_offset(definite_content_height, content_height);
            for (auto* child : box->children) offset_box(child, 0.f, dy);
            for (auto& anchor : box->block_static_anchors) anchor.block_offset += dy;
        }
    }

    // Parent/child collapsing is represented as an outward margin on this box and a
    // consumed margin inside its content extent. The child is moved to the content edge;
    // the enclosing block will subsequently position this box using the collapsed value.
    if (!intrinsic_control && style.display == AETHER_DISPLAY_BLOCK) {
        // Flex items, positioned boxes and inline-block/atomic layout items
        // establish a BFC just as overflow containers do. This prevents child
        // margins from collapsing through an independently laid-out item.
        const bool has_bfc = is_independent_layout_item ||
                             style.overflow_x != AETHER_OVERFLOW_VISIBLE ||
                             style.overflow_y != AETHER_OVERFLOW_VISIBLE;
        const auto first = box->children.empty() ? nullptr : box->children.front();
        const auto last = box->children.empty() ? nullptr : box->children.back();
        const bool first_is_block = first && first->kind == LAYOUT_BOX_ELEMENT && first->element &&
            (first->style.display == AETHER_DISPLAY_BLOCK || first->style.display == AETHER_DISPLAY_FLEX);
        const bool last_is_block = last && last->kind == LAYOUT_BOX_ELEMENT && last->element &&
            (last->style.display == AETHER_DISPLAY_BLOCK || last->style.display == AETHER_DISPLAY_FLEX);
        const float specified_min_height = resolve_semantic_height(engine, style.min_height, containing_height, style, element, "min-height");

        aether_margin_adjacency_facts start_facts{};
        start_facts.has_border_or_padding_before = aether_box_has_before_decoration(padding, border);
        start_facts.has_in_flow_content_before = !first_is_block;
        start_facts.establishes_block_formatting_context = has_bfc;
        if (first_is_block && aether_parent_start_margin_adjoins_child(start_facts)) {
            box->collapsed_margin_before = aether_collapse_adjoining_margins(margin.top, first->collapsed_margin_before);
            const float dy = content_y - normal_flow_border_box(first).y;
            offset_box(first, 0, dy);
            for (auto& anchor : box->block_static_anchors) {
                if (anchor.cursor_owner == first->element) anchor.block_offset += dy;
            }
            content_height = std::max(0.f, content_height - first->collapsed_margin_before);
        }

        aether_margin_adjacency_facts end_facts{};
        end_facts.has_border_or_padding_after = aether_box_has_after_decoration(padding, border);
        end_facts.has_in_flow_content_after = !last_is_block;
        end_facts.establishes_block_formatting_context = has_bfc;
        end_facts.has_definite_height = has_explicit_height;
        end_facts.has_positive_min_height = std::isfinite(specified_min_height) && specified_min_height > 0;
        if (last_is_block && aether_parent_end_margin_adjoins_child(end_facts)) {
            box->collapsed_margin_after = aether_collapse_adjoining_margins(margin.bottom, last->collapsed_margin_after);
            content_height = std::max(0.f, content_height - last->collapsed_margin_after);
        }
    }

    // Cursor provenance has served its construction-only correction purpose.
    for (auto& anchor : box->block_static_anchors) anchor.cursor_owner = nullptr;

    // Resolve the final border height (content + explicit + min/max clamps) before laying
    // out positioned descendants: their containing block is the finished padding box, so a
    // min-height:100vh body anchors bottom-inset descendants to the viewport bottom like Chrome.
    float border_height = has_explicit_height
        ? (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX
           ? explicit_height + aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border)
           : explicit_height)
        : content_height + aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border);

    border_height = clamp_border_height(engine, style, border_height,
        containing_height.value_or(NAN), height_decorations, true, element);

    box->scroll_flow_height = content_height;
    box->scroll_base_height = viewport_overflow_override
        ? engine->viewport_height : definite_content_height;
    const int32_t overflow_x = viewport_overflow_override
        ? viewport_overflow_override[0] : style.overflow_x;
    const int32_t overflow_y = viewport_overflow_override
        ? viewport_overflow_override[1] : style.overflow_y;
    box->scroll_completion_flags = LAYOUT_SCROLL_VALID |
        (aether_overflow_is_scroll_container(overflow_x) ? LAYOUT_SCROLL_X : 0) |
        (aether_overflow_is_scroll_container(overflow_y) ? LAYOUT_SCROLL_Y : 0);
    const element_completion_s completion{box, margin, padding, border,
                                         containing_block, containing_height};
    const aether_rectf natural_border{border_x, border_y, border_width, border_height};
    if (deferred_completion) {
        // Internal ancestor lookup can see meaningful natural geometry, but no
        // positioned/scroll/relative completion has run on this cell yet.
        box->border_box = natural_border;
        box->padding_box = aether_rectf_deflate(natural_border, border);
        box->content_box = aether_rectf_deflate(box->padding_box, padding);
        box->margin_box = {containing_block.x, border_y - box->collapsed_margin_before,
            border_width + aether_thicknessf_horizontal(margin),
            border_height + box->collapsed_margin_before + box->collapsed_margin_after};
        *deferred_completion = completion;
    } else {
        layout_complete_element(engine, styles, completion, natural_border, result);
    }
    return box;
}

void layout_recompute_saved_scroll(aether_layout_box_s* box) {
    const auto flags = box->scroll_completion_flags;
    if (!(flags & LAYOUT_SCROLL_VALID)) return;
    const bool scrolls_x = (flags & LAYOUT_SCROLL_X) != 0;
    const bool scrolls_y = (flags & LAYOUT_SCROLL_Y) != 0;
    const auto& content = box->content_box;
    const float flow_height = box->scroll_flow_height;
    const float base_height = box->scroll_base_height;
    // Never change stored offsets or sample callbacks. Placement/relative
    // translations have already changed this live content origin when repairing.
    box->scroll.scrollable_overflow_x = 0.f;
    box->scroll.scrollable_overflow_y = 0.f;
    if (scrolls_x || (scrolls_y && std::isfinite(base_height))) {
        float right = content.x + content.width;
        float bottom = content.y + std::max(0.f, flow_height);
        accumulate_scrollable_descendant_extent(box, scrolls_x, scrolls_y, right, bottom);
        if (scrolls_x) {
            const float extent = std::max(content.width, right - content.x);
            box->scroll.scrollable_overflow_x = std::max(0.f, extent - content.width);
        }
        if (scrolls_y && std::isfinite(base_height)) {
            const float extent = std::max(flow_height, bottom - content.y);
            box->scroll.scrollable_overflow_y = std::max(0.f, extent - base_height);
        }
    }
}

void layout_complete_element(const aether_layout_engine_s* engine,
    const StyleLookup* styles, const element_completion_s& completion,
    aether_rectf final_border, aether_layout_result_s* result, bool table_allocation)
{
    auto* box = completion.box;
    const auto& style = box->style;
    const auto& containing_block = completion.containing_block;
    const auto containing_height = completion.containing_height;
    const auto final_padding = aether_rectf_deflate(final_border, completion.border);
    const auto final_content = aether_rectf_deflate(final_padding, completion.padding);
    layout_positioned_children(engine, styles, box, box->element,
                               final_padding, final_content, result);
    box->border_box = final_border;
    box->padding_box = final_padding;
    box->content_box = final_content;
    box->margin_box = {containing_block.x, final_border.y - box->collapsed_margin_before,
        final_border.width + aether_thicknessf_horizontal(completion.margin),
        final_border.height + box->collapsed_margin_before + box->collapsed_margin_after};
    if (table_allocation) {
        box->scroll_base_height = final_content.height;
        if (result->records_boxes) {
            box->scroll_completion_flags |= LAYOUT_SCROLL_TABLE_CELL;
            result->has_finalized_table_cells = true;
        }
    }
    layout_recompute_saved_scroll(box);

    // Relative positioning preserves the element's space in normal flow while moving its visual
    // box (and its descendants) by the requested inset. A specified left/top wins over right/bottom.
    if (style.position == AETHER_POSITION_RELATIVE) {
        const float left = resolve(engine, style.inset.left, containing_block.width, style);
        const float right = resolve(engine, style.inset.right, containing_block.width, style);
        const float top = resolve_semantic_height(engine, style.inset.top, containing_height, style);
        const float bottom = resolve_semantic_height(engine, style.inset.bottom, containing_height, style);
        const float dx = std::isfinite(left) ? left : (std::isfinite(right) ? -right : 0.f);
        const float dy = std::isfinite(top) ? top : (std::isfinite(bottom) ? -bottom : 0.f);
        box->own_relative_dx = dx;
        box->own_relative_dy = dy;
        if (dx != 0.f || dy != 0.f) apply_relative_visual_offset(box, dx, dy);
    }
}

/* ── LayoutAtomicControl ───────────────────────────────────────────── */

static float layout_atomic_control(
    const aether_layout_engine_s* engine,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    float content_width,
    float explicit_height, bool has_explicit_height,
    aether_thicknessf padding,
    aether_thicknessf border)
{
    (void)content_width;
    float measured_height = 0.f;
    if (!has_explicit_height) {
        measured_height = measure_opaque_single_line_control(engine, element, style).content_height;
    }
    return aether_control_opaque_content_height_used(
        measured_height,
        style.font_size,
        layout_used_line_height(engine, element, style),
        has_explicit_height,
        explicit_height,
        style.box_sizing,
        aether_thicknessf_vertical(padding),
        aether_thicknessf_vertical(border));
}

// Anonymous flex wrappers have one inherited style and own a single CSS text
// sequence. Join raw DOM text before transformation/tokenization so node and
// comment boundaries cannot change shaping, whitespace or break opportunities.
// The children remain borrowed aliases; no authored node or text is modified.
static bool collect_anonymous_flex_text_items(
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    std::vector<inline_item_s>& output)
{
    if (!element->anonymous_flex_owner) return false;
    std::string text;
    for (const auto* child : element->children) {
        if (child->type == AETHER_NODE_TEXT) text.append(child->text);
    }
    inline_capitalization_context_s context;
    collect_text_items(element, style, text, output, context);
    return true;
}

/* ── LayoutFlowChildren ────────────────────────────────────────────── */

float layout_flow_children(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf content,
    percentage_height_s containing_height,
    const BoxStyleSnapshot& parent_style,
    aether_layout_result_s* result)
{
    float cursor_y = content.y;
    std::vector<inline_item_s> inline_items;
    inline_capitalization_context_s context;
    if (collect_anonymous_flex_text_items(parent, parent_style, inline_items)) {
        return layout_inline_items(engine, styles, parent_box, inline_items,
            content.x, content.y, content.width, content.height, containing_height, parent_style, result);
    }
    bool has_pending_margin = false;
    float pending_margin = 0.f;
    aether_node_s* cursor_owner = nullptr;

    auto flush_inline = [&]() {
        if (inline_items.empty()) return;
        if (has_pending_margin) {
            cursor_y += pending_margin;
            has_pending_margin = false;
            pending_margin = 0.f;
        }
        const float used = layout_inline_items(engine, styles, parent_box, inline_items,
                                               content.x, cursor_y, content.width,
                                               content.height, containing_height, parent_style, result);
        cursor_y += used;
        // A non-empty inline line separates adjacent block margins. Whitespace
        // alone has zero height and must not prevent the normal CSS collapse.
        if (used > 0.f) {
            cursor_owner = nullptr;
            has_pending_margin = false;
            pending_margin = 0.f;
        }
        inline_items.clear();
    };

    for (auto* node : parent->children) {
        if (node->type == AETHER_NODE_ELEMENT && node->pseudo_element == aether_pseudo_element_t::none) {
            const auto style = get_style(styles, node);
            if (style.position == AETHER_POSITION_ABSOLUTE &&
                (style.display == AETHER_DISPLAY_BLOCK || style.display == AETHER_DISPLAY_FLEX ||
                 style.display == AETHER_DISPLAY_TABLE)) {
                const size_t index = parent_box->block_static_anchors.size();
                parent_box->block_static_anchors.push_back({node, cursor_owner, 0.f,
                    cursor_y - content.y + (has_pending_margin ? pending_margin : 0.f)});
                // A marker never creates a run or flushes a pending block margin.
                // Existing real runs retain their existing whitespace/flush behavior.
                if (!inline_items.empty()) inline_items.push_back(inline_item_s::block_anchor(index));
                continue;
            }
        }
        const auto role = append_inline_node(engine, styles, parent, parent_style, node, inline_items, context);
        if (role == inline_node_role_e::block_content) {
            const auto child_style = get_style(styles, node);
                flush_inline();
                auto* child_box = layout_element(engine, styles, node,
                                                 {content.x, cursor_y, content.width, content.height}, containing_height,
                                                 cursor_y, true,
                                                 0, false, 0, forced_height_kind_e::none, false, result);
                // A pending margin exists only when no in-flow separator has been
                // laid out since the preceding block. Inline content flushes it, so
                // the remaining siblings are adjoining at this point.
                const float collapsed_before = has_pending_margin
                    ? aether_collapse_adjoining_margins(pending_margin, child_box->collapsed_margin_before)
                    : child_box->collapsed_margin_before;
                const float border_y = cursor_y + collapsed_before;
                offset_box(child_box, 0, border_y - normal_flow_border_box(child_box).y);
                parent_box->add_child(child_box);
                aether_margin_adjacency_facts empty_facts{};
                empty_facts.has_in_flow_content_before = !child_box->children.empty();
                empty_facts.has_in_flow_content_after = !child_box->children.empty();
                const auto child_padding = resolve_padding_edges(engine, child_style.padding, content.width, child_style);
                const auto child_border = resolve_border_edges(engine, child_style, content.width);
                empty_facts.has_border_or_padding_before = aether_box_has_before_decoration(child_padding, child_border);
                empty_facts.has_border_or_padding_after = aether_box_has_after_decoration(child_padding, child_border);
                empty_facts.has_definite_height = std::isfinite(resolve_semantic_height(engine, child_style.height, containing_height, child_style, child_box->element, "height"));
                const float child_min_height = resolve_semantic_height(engine, child_style.min_height, containing_height, child_style, child_box->element, "min-height");
                empty_facts.has_positive_min_height = std::isfinite(child_min_height) && child_min_height > 0;
                if (child_box->border_box.height <= .001f && aether_empty_block_margins_adjoin(empty_facts)) {
                    pending_margin = aether_collapse_adjoining_margins(collapsed_before, child_box->collapsed_margin_after);
                } else {
                    const auto flow_border = normal_flow_border_box(child_box);
                    cursor_y = flow_border.y + flow_border.height;
                    cursor_owner = node;
                    pending_margin = child_box->collapsed_margin_after;
                }
                has_pending_margin = true;
        }
    }

    flush_inline();
    return std::max(0.f, cursor_y - content.y + (has_pending_margin ? pending_margin : 0.f));
}

/* ── LayoutInlineItems ─────────────────────────────────────────────── */

float layout_inline_items(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_layout_box_s* parent_box,
    const std::vector<inline_item_s>& items,
    float start_x, float start_y, float available_width,
    float available_height, percentage_height_s containing_height,
    const BoxStyleSnapshot& parent_style,
    aether_layout_result_s* result)
{
    float cursor_x = start_x;
    float cursor_y = start_y;
    const float minimum_line_height = std::max(layout_used_line_height(engine, parent_box->element, parent_style), 1.f);
    float line_height = minimum_line_height;
    float prefix_ascent = 0.f, prefix_descent = 0.f;
    int line_start_index = static_cast<int>(parent_box->children.size());
    std::vector<line_range> line_widths;

    auto finish_line = [&](bool force_advance) {
        int end = static_cast<int>(parent_box->children.size());
        float width = std::max(0.f, cursor_x - start_x);
        if (end > line_start_index || force_advance) {
            // CSS inline layout aligns glyph runs to a shared baseline. Top-aligning every run
            // makes smaller inline text float upward beside larger text.
            float ascent = 0.f;
            float descent = 0.f;
            for (int index = line_start_index; index < end; ++index) {
                auto* child = parent_box->children[index];
                if (child->kind != LAYOUT_BOX_TEXT) continue;
                const auto metrics = engine_measure_text(engine, child->text.c_str(), &child->style, 0.f, child->element);
                ascent = std::max(ascent, metrics.baseline);
                descent = std::max(descent, std::max(0.f, metrics.height - metrics.baseline));
            }
            line_height = std::max(line_height, ascent + descent);
            if (ascent > 0.f) {
                for (int index = line_start_index; index < end; ++index) {
                    auto* child = parent_box->children[index];
                    if (child->kind != LAYOUT_BOX_TEXT) continue;
                    const auto metrics = engine_measure_text(engine, child->text.c_str(), &child->style, 0.f, child->element);
                    offset_box(child, 0.f, std::max(0.f, ascent - metrics.baseline));
                }
            }
            line_widths.push_back({line_start_index, end, width, force_advance});
            cursor_y += line_height;
        }
        cursor_x = start_x;
        line_height = minimum_line_height;
        prefix_ascent = prefix_descent = 0.f;
        line_start_index = end;
    };

    bool previous_collapsible_space = false;
    for (auto& item : items) {
        if (item.block_anchor_index != static_cast<size_t>(-1)) {
            const bool has_prefix = static_cast<int>(parent_box->children.size()) > line_start_index;
            // Use only already measured prefix metrics. Later atoms, text or wraps
            // cannot change this hypothetical block insertion point.
            auto& anchor = parent_box->block_static_anchors[item.block_anchor_index];
            anchor.block_offset += cursor_y - start_y +
                (has_prefix ? std::max(line_height, prefix_ascent + prefix_descent) : 0.f);
            if (has_prefix || cursor_y != start_y) anchor.cursor_owner = nullptr;
            continue;
        }
        if (item.is_line_break) {
            finish_line(true);
            previous_collapsible_space = false;
            continue;
        }

        if (item.is_atomic) {
            auto* provisional = layout_element(engine, styles, item.element,
                                              {start_x, cursor_y, available_width, available_height}, containing_height,
                                              cursor_y, false,
                                              0, false, 0, forced_height_kind_e::none, true, result);
            float w = provisional->margin_box.width;
            if (cursor_x > start_x && cursor_x + w > start_x + available_width) {
                const float old_y = normal_flow_margin_box(provisional).y;
                finish_line(false);
                offset_box(provisional, 0, cursor_y - old_y);
            }
            const float dx = cursor_x - normal_flow_margin_box(provisional).x;
            offset_box(provisional, dx, 0);
            parent_box->add_child(provisional);
            cursor_x += provisional->margin_box.width;
            line_height = std::max(line_height, provisional->margin_box.height);
            previous_collapsible_space = false;
            continue;
        }

        // text item
        for (const auto& token : aether_tokenize_inline_text(item.text, item.style.white_space)) {
            if (token.kind == aether_inline_text_token_kind_e::forced_break) {
                finish_line(true);
                previous_collapsible_space = false;
                continue;
            }
            const bool collapsible = token.kind == aether_inline_text_token_kind_e::collapsible_space;
            const bool whitespace = collapsible || token.kind == aether_inline_text_token_kind_e::preserved_space;
            if (collapsible && (cursor_x <= start_x || previous_collapsible_space)) continue;
            const bool allow_wrap = aether_white_space_allows_soft_wrap(item.style.white_space);
            const auto metrics = engine_measure_text(engine, token.text.c_str(), &item.style, 0.f, item.element);
            if (allow_wrap && cursor_x > start_x && cursor_x + metrics.width > start_x + available_width &&
                (!whitespace || token.kind == aether_inline_text_token_kind_e::preserved_space)) {
                finish_line(false);
            }

            auto* fragment = result->create_box();
            fragment->kind = LAYOUT_BOX_TEXT;
            fragment->element = item.element;
            fragment->style = item.style;
            fragment->presentation_owner = item.element ? item.element->anonymous_flex_owner : nullptr;
            fragment->text = token.text;
            fragment->content_box = { cursor_x, cursor_y, metrics.width, metrics.height };
            fragment->padding_box = fragment->content_box;
            fragment->border_box  = fragment->content_box;
            fragment->margin_box  = fragment->content_box;
            parent_box->add_child(fragment);
            if (result->records_boxes) result->paint_order.push_back(fragment);
            cursor_x += metrics.width;
            line_height = std::max(line_height, metrics.height);
            prefix_ascent = std::max(prefix_ascent, metrics.baseline);
            prefix_descent = std::max(prefix_descent, std::max(0.f, metrics.height - metrics.baseline));
            previous_collapsible_space = collapsible;
        }
    }

    if (static_cast<int>(parent_box->children.size()) > line_start_index) {
        finish_line(false);
    }

    apply_line_alignment(parent_box, line_widths, available_width, parent_style.text_align);
    return line_widths.empty() ? 0.f : std::max(0.f, cursor_y - start_y);
}

/* ── CollectInlineElement ──────────────────────────────────────────── */

static void collect_inline_element(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    std::vector<inline_item_s>& output, inline_capitalization_context_s& context)
{
    for (auto* node : element->children) {
        if (node->type == AETHER_NODE_TEXT) {
            collect_text_items(element, style, node->text, output, context);
        } else if (node->type == AETHER_NODE_ELEMENT) {
            auto child_style = get_style(styles, node);
            if (child_style.display == AETHER_DISPLAY_NONE) continue;
            const bool isolated = is_out_of_flow(child_style);
            const bool block = child_style.display == AETHER_DISPLAY_BLOCK ||
                child_style.display == AETHER_DISPLAY_FLEX || child_style.display == AETHER_DISPLAY_TABLE;
            inline_capitalization_context_s independent;
            auto& child_context = (isolated || block) ? independent : context;
            if (child_style.display == AETHER_DISPLAY_LINE_BREAK) {
                output.push_back(inline_item_s::line_break(node, child_style));
                child_context.starts_word = true;
            } else if (is_opaque_single_line_control(node) || child_style.display == AETHER_DISPLAY_INLINE_BLOCK) {
                output.push_back(inline_item_s::atomic(node, child_style));
                child_context.starts_word = true;
            } else {
                // Nested positioned/block content keeps its existing flattened
                // output and geometry; only capitalization is isolated here.
                collect_inline_element(engine, styles, node, child_style, output, child_context);
            }
            if (block && !isolated) context.starts_word = true;
        }
    }
}

/* ── CollectTextItems ──────────────────────────────────────────────── */

static void collect_text_items(aether_node_s* owner,
                        const BoxStyleSnapshot& style,
                        const std::string& text,
                        std::vector<inline_item_s>& output, inline_capitalization_context_s& context)
{
    if (!text.empty()) {
        // The text item's element must be the OWNER (for generated content, the
        // pseudo-element) so the paint path re-resolves the fragment's style from
        // the style map correctly. Using style_origin (the host) here made the
        // display-list builder apply the HOST's font-size to ::before/::after text
        // (e.g. a weather-widget degree symbol measured at 40px but painted at the
        // host's 80px). Selector combinators still navigate via style_origin.
        output.push_back(inline_item_s::text_item(
            owner, style,
            aether_apply_text_transform(text, style.text_transform, context.starts_word)));
        context.starts_word = aether::presentation_space_byte(static_cast<unsigned char>(output.back().text.back())) != 0;
    }
}

static inline_node_role_e append_inline_node(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* parent,
    const BoxStyleSnapshot& parent_style,
    aether_node_s* node,
    std::vector<inline_item_s>& output, inline_capitalization_context_s& context)
{
    if (node->type == AETHER_NODE_TEXT) {
        if (!aether_inline_text_has_visible_content(node->text, parent_style.white_space)) {
            if (!node->text.empty())
                context.starts_word = aether::presentation_space_byte(static_cast<unsigned char>(node->text.back())) != 0;
            return inline_node_role_e::ignored;
        }
        collect_text_items(parent, parent_style, node->text, output, context);
        return inline_node_role_e::inline_content;
    }
    if (node->type != AETHER_NODE_ELEMENT) return inline_node_role_e::ignored;

    const auto child_style = get_style(styles, node);
    if (child_style.display == AETHER_DISPLAY_NONE || is_out_of_flow(child_style)) {
        return inline_node_role_e::ignored;
    }
    if (child_style.display == AETHER_DISPLAY_BLOCK || child_style.display == AETHER_DISPLAY_FLEX ||
        child_style.display == AETHER_DISPLAY_TABLE) {
        context.starts_word = true;
        return inline_node_role_e::block_content;
    }
    if (child_style.display == AETHER_DISPLAY_LINE_BREAK) {
        output.push_back(inline_item_s::line_break(node, child_style));
        context.starts_word = true;
    } else if (is_opaque_single_line_control(node) || child_style.display == AETHER_DISPLAY_INLINE_BLOCK) {
        output.push_back(inline_item_s::atomic(node, child_style));
        context.starts_word = true;
    } else {
        collect_inline_element(engine, styles, node, child_style, output, context);
    }
    return inline_node_role_e::inline_content;
}

static float measure_inline_items_intrinsic_width(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    const std::vector<inline_item_s>& items,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode)
{
    std::vector<aether_inline_intrinsic_fragment_s> fragments;
    for (const auto& item : items) {
        if (item.is_line_break) {
            fragments.push_back({aether_inline_fragment_kind_e::forced_break, 0.f});
            continue;
        }
        if (item.is_atomic) {
            fragments.push_back({aether_inline_fragment_kind_e::atomic,
                measure_child_natural_width(engine, styles, item.element, maximum, result, mode)});
            continue;
        }
        for (const auto& token : aether_tokenize_inline_text(item.text, item.style.white_space)) {
            if (token.kind == aether_inline_text_token_kind_e::forced_break) {
                fragments.push_back({aether_inline_fragment_kind_e::forced_break, 0.f});
                continue;
            }
            const bool whitespace = token.kind != aether_inline_text_token_kind_e::word;
            fragments.push_back({
                whitespace ? aether_inline_fragment_kind_e::whitespace : aether_inline_fragment_kind_e::word,
                engine_measure_text(engine, token.text.c_str(), &item.style, 0.f, item.element).width});
        }
    }
    const auto sizes = aether_measure_inline_intrinsic_sizes(fragments);
    return mode == aether_intrinsic_size_mode_e::min_content ? sizes.min_content : sizes.max_content;
}

/* ── MeasureIntrinsicWidth ─────────────────────────────────────────── */

float measure_intrinsic_width(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode)
{
    if (is_opaque_single_line_control(element)) {
        const float preferred = 180.f +
            aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
        return std::min(maximum, preferred);
    }
    if (style.display == AETHER_DISPLAY_FLEX) {
        return measure_intrinsic_flex_width(engine, styles, element, style, padding, border, maximum, result, mode);
    }
    if (style.display == AETHER_DISPLAY_TABLE) {
        return measure_intrinsic_table_width(engine, styles, element, maximum, result, mode) +
            aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    }

    // Use the exact inline-item producer used by layout. Intrinsic sizing must
    // never choose a different DOM traversal from the visible formatter.
    float content_width = 0.f;
    std::vector<inline_item_s> inline_items;
    inline_capitalization_context_s context;
    auto flush_inline = [&]() {
        if (inline_items.empty()) return;
        content_width = std::max(content_width,
            measure_inline_items_intrinsic_width(engine, styles, inline_items, maximum, result, mode));
        inline_items.clear();
    };

    if (collect_anonymous_flex_text_items(element, style, inline_items)) {
        flush_inline();
        return content_width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    }

    for (auto* child : element->children) {
        const auto role = append_inline_node(engine, styles, element, style, child, inline_items, context);
        if (role != inline_node_role_e::block_content) continue;
        flush_inline();
        content_width = std::max(content_width,
            measure_child_natural_width(engine, styles, child, maximum, result, mode));
    }
    flush_inline();
    return content_width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
}

float measure_intrinsic_height(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_layout_result_s* result,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border,
    float available_width)
{
    const float content_width = std::max(0.f, available_width -
        aether_thicknessf_horizontal(padding) - aether_thicknessf_horizontal(border));
    if (is_opaque_single_line_control(element)) {
        return layout_atomic_control(engine, element, style, content_width, NAN, false, padding, border) +
            aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border);
    }

    const bool flex = style.display == AETHER_DISPLAY_FLEX;
    const bool row = flex && style.flex_direction == AETHER_FLEX_DIRECTION_ROW;
    float content_height = 0.f;
    const auto add_height = [&](float height) {
        if (row) content_height = std::max(content_height, height);
        else content_height += height;
    };
    const auto measure_child = [&](aether_node_s* child) {
        const auto child_style = get_style(styles, child);
        const auto child_padding = resolve_padding_edges(engine, child_style.padding, content_width, child_style);
        const auto child_border = resolve_border_edges(engine, child_style, content_width);
        const auto child_margin = resolve_margin_edges(engine, child_style.margin, content_width, child_style);
        const bool percentage_height = layout_measure_has_percentage(layout_measures(styles, child), "height", child_style.height);
        float child_height = percentage_height ? NAN : resolve(engine, child_style.height, 0.f, child_style);
        if (std::isfinite(child_height)) {
            if (child_style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX) {
                child_height += aether_thicknessf_vertical(child_padding) + aether_thicknessf_vertical(child_border);
            }
        } else {
            child_height = measure_intrinsic_height(engine, styles, result, child, child_style,
                                                    child_padding, child_border, content_width);
        }
        add_height(child_height + aether_thicknessf_vertical(child_margin));
    };

    if (flex) {
        // Reuse this result's exact item sequence and synthetic overlay. Inline
        // elements, text wrappers and block elements are all real flex items.
        const auto& children = collect_flex_items(styles, result, element, style);
        for (auto* child : children) measure_child(child);
        if (!row && !children.empty()) {
            content_height += (children.size() - 1) * layout_used_gap(styles, element, style, true, 0.f, false);
        }
    } else {
        std::vector<inline_item_s> inline_items;
        inline_capitalization_context_s context;
        const auto flush_inline = [&]() {
            if (inline_items.empty()) return;
            add_height(measure_inline_items_height(
                engine, styles, inline_items, content_width, style, result, element));
            inline_items.clear();
        };
        if (!collect_anonymous_flex_text_items(element, style, inline_items)) {
            for (auto* child : element->children) {
                const auto role = append_inline_node(engine, styles, element, style, child, inline_items, context);
                if (role != inline_node_role_e::block_content) continue;
                flush_inline();
                measure_child(child);
            }
        }
        flush_inline();
    }
    return content_height + aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border);
}

static float measure_inline_items_height(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    const std::vector<inline_item_s>& items,
    float available_width,
    const BoxStyleSnapshot& parent_style,
    aether_layout_result_s* result, aether_node_s* owner)
{
    const float line_minimum = std::max(1.f, layout_used_line_height(engine, owner, parent_style));
    float line_height = line_minimum;
    float cursor_x = 0.f;
    float total_height = 0.f;
    bool has_line_content = false;

    auto finish_line = [&](bool forced) {
        if (has_line_content || forced) total_height += line_height;
        cursor_x = 0.f;
        line_height = line_minimum;
        has_line_content = false;
    };
    auto append_fragment = [&](float width, float height, bool whitespace) {
        if (whitespace && !has_line_content) return;
        if (!whitespace && has_line_content && cursor_x + width > available_width) finish_line(false);
        cursor_x += width;
        line_height = std::max(line_height, height);
        has_line_content = true;
    };

    for (const auto& item : items) {
        if (item.is_line_break) {
            finish_line(true);
            continue;
        }
        if (item.is_atomic) {
            const auto style = get_style(styles, item.element);
            const auto padding = resolve_padding_edges(engine, style.padding, available_width, style);
            const auto border = resolve_border_edges(engine, style, available_width);
            const float width = measure_child_natural_width(
                engine, styles, item.element, available_width, result, aether_intrinsic_size_mode_e::max_content);
            const float height = measure_intrinsic_height(
                engine, styles, result, item.element, style, padding, border, available_width);
            append_fragment(width, height, false);
            continue;
        }
        for (const auto& token : aether_tokenize_inline_text(item.text, item.style.white_space)) {
            if (token.kind == aether_inline_text_token_kind_e::forced_break) {
                finish_line(true);
                continue;
            }
            const bool whitespace = token.kind != aether_inline_text_token_kind_e::word;
            const auto metrics = engine_measure_text(engine, token.text.c_str(), &item.style, 0.f, item.element);
            append_fragment(metrics.width, metrics.height, whitespace);
        }
    }
    finish_line(false);
    return total_height;
}

static float measure_intrinsic_flex_width(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode)
{
    // This must be the exact same item sequence consumed by layout_flex_children.
    // Measuring only element children makes a nested flex item forget its direct
    // text run and gives flex-shrink an artificially small automatic minimum.
    const auto& children = collect_flex_items(styles, result, element, style);
    if (children.empty()) {
        return aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    }

    std::vector<float> child_widths;
    for (auto* child : children) {
        child_widths.push_back(measure_child_natural_width(engine, styles, child, maximum, result, mode));
    }
    const float content = aether_intrinsic_flex_main_contribution(
        child_widths, style.flex_direction == AETHER_FLEX_DIRECTION_COLUMN,
        style.flex_wrap, layout_used_gap(styles, element, style, false, 0.f, false), mode);
    return content + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
}

float measure_child_natural_width(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* child, float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode)
{
    auto child_style = get_style(styles, child);
    auto child_margin = resolve_margin_edges(engine, child_style.margin, maximum, child_style);
    // A percentage width is indefinite while this intrinsic contribution is
    // calculated. CSS Sizing therefore treats it as auto at this stage.
    const bool percentage_dependent_width =
        layout_measure_has_percentage(layout_measures(styles, child), "width", child_style.width);
    const float specified = percentage_dependent_width
        ? NAN
        : resolve(engine, child_style.width, maximum, child_style);
    auto child_padding = resolve_padding_edges(engine, child_style.padding, maximum, child_style);
    auto child_border  = resolve_border_edges(engine, child_style, maximum);

    float width;
    if (std::isfinite(specified)) {
        width = (child_style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? specified + aether_thicknessf_horizontal(child_padding) + aether_thicknessf_horizontal(child_border)
            : specified;
    } else {
        width = measure_intrinsic_width(engine, styles, child, child_style,
                                        child_padding, child_border, maximum, result, mode);
    }
    return width + aether_thicknessf_horizontal(child_margin);
}

/* ══════════════════════════════════════════════════════════════════════
   C ABI implementation
   ══════════════════════════════════════════════════════════════════════ */

static void validate_used_geometry(const aether_layout_result_s* result) {
    const auto rect_valid = [](aether_rectf r, bool sized) {
        return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
            (!sized || (r.width >= 0.f && r.height >= 0.f));
    };
    for (const auto* box : result->allocated_boxes) {
        if (!rect_valid(box->border_box, true) || !rect_valid(box->padding_box, true) ||
            !rect_valid(box->content_box, true) || !rect_valid(box->margin_box, false))
            throw std::overflow_error("Invalid used box geometry");
        for (const auto value : {box->scroll.scrollable_overflow_x, box->scroll.scrollable_overflow_y,
             box->scroll.scroll_offset_x, box->scroll.scroll_offset_y, box->visual_opacity,
             box->used_border.left, box->used_border.top, box->used_border.right, box->used_border.bottom})
            if (!std::isfinite(value)) throw std::overflow_error("Non-finite used visual geometry");
        if (box->visual_sample_valid) {
            const auto& m = box->visual_transform;
            for (const auto value : {m.m11,m.m12,m.m21,m.m22,m.dx,m.dy})
                if (!std::isfinite(value)) throw std::overflow_error("Non-finite visual transform");
        }
    }
}

// Complete helper is the fallback and the differential oracle. It intentionally
// traverses every selected map entry, including zero static-owner fixtures.
static aether::style::ContextOwnersRef effective_fixed_containers(
    const StyleLookup* styles, const std::unordered_set<aether_node_s*>& active) {
    using namespace aether::style;
    count_work(WorkCounter::transition_context_builds);
    std::vector<ContextOwnerIndex::Entry> effective;
    const auto version = styles ? styles->selected_version() : MapVersionRef{};
    // Legacy records may omit the token. Their raw nodes must be live during
    // the operation, as before D2. A known dead document is never dereferenced.
    if (version && (!version->document_lifetime() || version->document_lifetime()->alive)) version->for_each_node([&](const BoundNodeView& view) {
        count_work(WorkCounter::transition_context_entries_visited);
        count_work(WorkCounter::context_backing_visits);
        const auto& record = view.payload_owner();
        auto* node = record->node();
        const auto identity = record->node_lifetime() ? record->node_lifetime() : node ? node->identity : nullptr;
        if (!identity || identity->owner != node || !identity->alive || !identity->attached) return;
        const auto style = record->style_view();
        if (node && node->type == AETHER_NODE_ELEMENT && !node->anonymous_flex_owner &&
            (!style.transforms().empty() || !style.backdrop_filters().empty() || active.count(node)))
            append_context_entry(effective,{node,identity,1});
    });
    return publish_context_owners({},ContextOwnerIndex::build(std::move(effective)));
}
static bool context_membership_equal(const aether::style::ContextOwnersRef& a,
    const aether::style::ContextOwnersRef& b) {
    if(a==b) return true;
    if(!a || !b || a->owners.size()!=b->owners.size()) return false;
    bool equal=true;
    a->owners.diff(b->owners,[&](uintptr_t,const auto* before,const auto* after) {
        if(!before || !after || before->identity!=after->identity || before->node!=after->node) equal=false;
    });
    return equal;
}
static aether::style::ContextOwnersRef prepare_context_owners(aether_layout_engine_s* engine,
    aether_transition_runtime_t runtime,const aether::style::MapVersionRef& map) {
    using namespace aether::style;
    auto& cache=engine->context_cache;
    const auto active=aether_active_transform_snapshot(runtime);
    const auto statics=map?map->static_context_owners():ContextOwnersRef{};
    const auto topology=map?map->context_topology():ContextLineageRef{};
    const auto document=map?map->document_lifetime():std::shared_ptr<node_identity_s>{};
    // 1 first use; 2 unknown map/transition coverage; 3 topology/document; 4 disabled.
    uint64_t fallback=4;
#if defined(AETHERUI_ENABLE_CONTEXT_OWNERS)
    fallback=(!statics || !document || (runtime && !active) || !structure_valid(map.get()) ||
        document->style_tree_epoch!=map->style_tree_epoch())?2:
        (!cache.effective?1:cache.document!=document || cache.structure_epoch!=document->structure_epoch ||
        cache.topology!=topology || cache.statics->lineage!=statics->lineage ||
        bool(cache.active)!=bool(active) || (active && cache.active->lineage!=active->lineage)?3:0);
#endif
    ContextOwnersRef effective;
    if(fallback) {
        count_work(WorkCounter::context_fallbacks);
        count_work(WorkCounter::context_fallback_reason,fallback);
        const StyleLookup lookup(map,nullptr,{});
        effective=effective_fixed_containers(&lookup,aether_active_transform_owners(runtime));
        if(context_membership_equal(cache.effective,effective)) effective=cache.effective;
    } else if(cache.statics==statics && cache.active==active) {
        count_work(WorkCounter::context_reuses);
        return cache.effective;
    } else {
        auto index=cache.effective->owners;
        const auto update=[&](uintptr_t key,const ContextOwnerEntry*,const ContextOwnerEntry*) {
            const auto* static_entry=statics->owners.find(key);
            const auto* active_entry=active?active->owners.find(key):nullptr;
            const auto* entry=static_entry?static_entry:active_entry;
            bool present=false;
            if(entry && entry->identity->alive && entry->identity->attached && entry->identity->owner==entry->node) {
                // Active-only nodes must belong to this exact selected map.
                const auto view=map->lookup(entry->node);
                present=view && view.payload_owner()->node_lifetime()==entry->identity &&
                    entry->node->type==AETHER_NODE_ELEMENT && !entry->node->anonymous_flex_owner;
            }
            if(present) index=update_context_owner(std::move(index),{entry->node,entry->identity,1});
            else index=erase_context_owner(std::move(index),key);
        };
        cache.statics->owners.diff(statics->owners,update);
        if(active) cache.active->owners.diff(active->owners,update);
        effective=publish_context_owners(cache.effective,std::move(index));
        if(effective==cache.effective) count_work(WorkCounter::context_reuses);
    }
    // Store cache only when all certificate components are available and current.
    if(statics && document && (!runtime || active) && structure_valid(map.get()) &&
        document->style_tree_epoch==map->style_tree_epoch())
        cache={statics,active,effective,topology,document,document->structure_epoch};
    else cache={};
    return effective;
}

extern "C" {

AETHER_API aether_status_t aether_layout_create(
    aether_text_measurer_fn text_measurer, void* text_measurer_data,
    aether_layout_engine_t* out_engine)
{
    if (!out_engine) return AETHER_ERROR_NULL_ARG;
    auto* e = new(std::nothrow) aether_layout_engine_s();
    if (!e) return AETHER_ERROR_OUT_OF_MEMORY;
    e->text_measurer = text_measurer;
    e->text_measurer_data = text_measurer_data;
    *out_engine = e;
    return AETHER_OK;
}

AETHER_API void aether_layout_destroy(aether_layout_engine_t engine) {
    delete engine;
}

AETHER_API aether_status_t aether_layout_prepare_transition_context(
    aether_layout_engine_t engine, aether_transition_runtime_t runtime,
    aether_style_map_t current_styles, aether_layout_result_t previous_layout,
    aether_bool_t* out_changed) {
    if (out_changed) *out_changed = AETHER_FALSE;
    if (!engine || !current_styles || !out_changed) return AETHER_ERROR_NULL_ARG;
    engine->transition_context_prepared=false;
    engine->prepared_context_map.reset();
    engine->prepared_context_owners.reset();
    engine->prepared_active_snapshot.reset();
    engine->prepared_active_nodes.clear();
    try {
        count_work(WorkCounter::context_calls);
        const auto map=aether::style::pin_style_map(current_styles);
        const auto effective=prepare_context_owners(engine,runtime,map);
        *out_changed = !previous_layout || !previous_layout->effective_context_enabled ||
            !context_membership_equal(effective,previous_layout->effective_context_owners);
        engine->prepared_context_owners=effective;
        engine->prepared_active_snapshot=aether_active_transform_snapshot(runtime);
        engine->prepared_active_nodes.clear();
        if(runtime && !engine->prepared_active_snapshot) engine->prepared_active_nodes=aether_active_transform_owners(runtime);
        engine->prepared_context_map=map;
        engine->transition_context_prepared = true;
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}

AETHER_API aether_status_t aether_layout_layout(
    aether_layout_engine_t engine, aether_doc_t doc, aether_style_map_t source_styles,
    float viewport_width, float viewport_height,
    aether_scroll_offset_fn scroll_fn, void* scroll_data,
    aether_layout_result_t* out_result)
{
    if (!engine) return AETHER_ERROR_NULL_ARG;
    if (engine->layout_in_progress) return AETHER_ERROR_INVALID_STATE;
    engine->layout_in_progress = true;
    struct layout_scope_s { aether_layout_engine_s* e; ~layout_scope_s() { e->layout_in_progress = false; } } layout_scope{engine};
    const bool prepared = std::exchange(engine->transition_context_prepared, false);
    auto active = std::move(engine->prepared_active_nodes);
    engine->prepared_active_nodes.clear();
    auto prepared_active=std::move(engine->prepared_active_snapshot);
    auto prepared_owners = std::move(engine->prepared_context_owners);
    auto prepared_map = std::move(engine->prepared_context_map);
    if (!out_result || !doc) return AETHER_ERROR_NULL_ARG;
    const auto version = aether::style::pin_style_map(source_styles);
    if (!aether::style::structure_valid(version.get()) || !aether::style::inputs_current(version.get())) return AETHER_ERROR_INVALID_STATE;
    if (version && version->contextual() && (version->document_lifetime() != doc->identity || version->document_identity() != aether_node_identity(doc))) return AETHER_ERROR_INVALID_STATE;
    const auto document_identity = doc->identity;
    const auto document_structure_epoch = document_identity->structure_epoch;
    const CallbackEpochGuard operation_guard(document_identity);
    if (!std::isfinite(viewport_width) || !std::isfinite(viewport_height) || viewport_width <= 0 || viewport_height <= 0) return AETHER_ERROR_INVALID_STATE;

    engine->scroll_offset_fn = scroll_fn;
    engine->scroll_offset_data = scroll_data;
    engine->viewport_width = viewport_width;
    engine->viewport_height = viewport_height;

    auto* result = new(std::nothrow) aether_layout_result_s();
    if (!result) return AETHER_ERROR_OUT_OF_MEMORY;
    result->viewport = {viewport_width, viewport_height};
    result->abandoned = true;
    struct measure_scope_s { aether_layout_engine_s* e; ~measure_scope_s() { e->measure_styles = nullptr; e->measure_document.reset(); } } measure_scope{engine};
    try {
    aether::style::FrozenStyleRef defaults;
    const auto default_flat = aether_computed_style_default();
    const auto default_status = aether::style::freeze_style(default_flat, defaults);
    if (default_status == AETHER_ERROR_OUT_OF_MEMORY) throw std::bad_alloc();
    if (default_status != AETHER_OK) throw std::runtime_error("Invalid default style");
    result->build_overlay = std::make_unique<aether::layout::MutableLayoutOverlay>();
    result->assets = std::make_shared<const aether::layout::LayoutAssets>(version, nullptr, defaults, document_identity, document_structure_epoch);
    const StyleLookup build_lookup(version, result->build_overlay.get(), defaults);
    const auto* styles = &build_lookup;
    engine->measure_styles = styles;
    engine->measure_document = document_identity;
    if (version && version->contextual() && !version->revision()->entries().empty()) {
        const auto& document = version->revision()->document()->values();
        if (document.viewport_width != viewport_width || document.viewport_height != viewport_height)
            throw std::runtime_error("Layout viewport differs from style document");
    }
    const auto freeze_assets = [&]() {
        auto assets = std::make_shared<const aether::layout::LayoutAssets>(version, result->build_overlay->freeze(), defaults, document_identity, document_structure_epoch);
        result->assets = std::move(assets);
        result->build_overlay.reset();
    };
    result->effective_context_enabled = prepared || (version && version->contextual());
    if (result->effective_context_enabled) {
        try {
            const bool same_source=prepared && prepared_map && version &&
                (prepared_map==version || (prepared_map->projection_source() &&
                    prepared_map->projection_source()==version->projection_source()));
            bool valid_pin=same_source && structure_valid(prepared_map.get()) &&
                prepared_map->document_lifetime()==document_identity &&
                prepared_map->style_tree_epoch()==document_identity->style_tree_epoch;
#if !defined(AETHERUI_ENABLE_CONTEXT_OWNERS)
            valid_pin=false; // OFF preserves the complete legacy preparation/layout path.
#endif
            if(valid_pin) result->effective_context_owners=std::move(prepared_owners);
            else {
                // A one-shot pin for another map is never silently applied.
                count_work(WorkCounter::context_fallbacks);
                count_work(WorkCounter::context_fallback_reason,5);
                if(prepared_active) for(const auto& item:prepared_active->owners.entries()) {
                    count_work(WorkCounter::context_active_visits); active.insert(item.second.node);
                }
                result->effective_context_owners=effective_fixed_containers(styles,active);
            }
        }
        catch (const std::bad_alloc&) { delete result; return AETHER_ERROR_OUT_OF_MEMORY; }
    }

    // Find root element (using C API from document_node.cpp)
    aether_node_s* root_el = aether_doc_get_body(doc);
    if (!root_el) root_el = aether_doc_get_document_element(doc);
    if (!root_el) { freeze_assets(); result->abandoned = false; *out_result = result; return AETHER_OK; } // empty doc

    auto root_style = get_style(styles, root_el);
    engine->root_font_size = get_style(styles, aether_doc_get_document_element(doc)).font_size;
    result->root_font_size = engine->root_font_size;

    // CSS Overflow 3.3 viewport propagation. The root (html) overflow applies
    // to the viewport; when html is visible in both axes and a body child is
    // laid out, the body's overflow propagates instead. The propagated element
    // keeps its own used overflow for paint, and visible on the viewport is
    // interpreted as auto — so a page with no overflow rule scrolls by default.
    aether_node_s* html_el = aether_doc_get_document_element(doc);
    aether_node_s* overflow_source = root_el;
    if (html_el && html_el != root_el) {
        const auto html_style = get_style(styles, html_el);
        const bool html_visible =
            html_style.overflow_x == AETHER_OVERFLOW_VISIBLE &&
            html_style.overflow_y == AETHER_OVERFLOW_VISIBLE;
        if (!html_visible) {
            overflow_source = html_el;
        } else if (root_style.display == AETHER_DISPLAY_NONE) {
            overflow_source = html_el;
        }
    }
    const auto source_style = get_style(styles, overflow_source);
    result->viewport_overflow_x = source_style.overflow_x;
    result->viewport_overflow_y = source_style.overflow_y;
    if (result->viewport_overflow_x == AETHER_OVERFLOW_VISIBLE)
        result->viewport_overflow_x = AETHER_OVERFLOW_AUTO;
    if (result->viewport_overflow_y == AETHER_OVERFLOW_VISIBLE)
        result->viewport_overflow_y = AETHER_OVERFLOW_AUTO;

    const int32_t viewport_overflow[2] = {result->viewport_overflow_x, result->viewport_overflow_y};
    auto* root_box = layout_element(engine, styles, root_el,
                                    {0, 0, viewport_width, viewport_height}, viewport_height,
                                    0, true,
                                    0, false, 0, forced_height_kind_e::none, false, result,
                                    viewport_overflow);
    result->root = root_box;
    layout_finalize_fixed_containers(engine, styles, result);
    if (!aether::style::structure_valid(version.get()) || !operation_guard.valid()) {
        delete result; return AETHER_ERROR_INVALID_STATE;
    }
    freeze_assets();
    if (version && version->contextual()) {
        const auto status = aether_contextual_transition_finalize_layout(result, nullptr);
        if (status != AETHER_OK) { delete result; return status; }
    }
    validate_used_geometry(result);
    result->abandoned = false;
    *out_result = result;
    return AETHER_OK;
    } catch (const std::bad_alloc&) {
        result->abandoned = true; delete result; return AETHER_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        result->abandoned = true; delete result; return AETHER_ERROR_INVALID_STATE;
    }
}

AETHER_API void aether_layout_set_contextual_text_measurer(aether_layout_engine_t engine,
    aether_text_measurer_contextual_fn callback, void* data) {
    if (!engine) return;
    engine->contextual_text_measurer = callback;
    engine->contextual_text_measurer_data = data;
}

AETHER_API void aether_layout_result_destroy(aether_layout_result_t result) {
    if (result && --result->references == 0) delete result;
}

AETHER_API aether_layout_box_t aether_layout_result_get_root(aether_layout_result_t result) {
    if (!result) return nullptr;
    return result->root;
}

AETHER_API int32_t aether_layout_result_get_viewport_overflow_x(aether_layout_result_t result) {
    return result ? result->viewport_overflow_x : AETHER_OVERFLOW_VISIBLE;
}

AETHER_API int32_t aether_layout_result_get_viewport_overflow_y(aether_layout_result_t result) {
    return result ? result->viewport_overflow_y : AETHER_OVERFLOW_VISIBLE;
}

AETHER_API aether_layout_box_t aether_layout_result_find_primary_box(aether_layout_result_t result, aether_node_t element) {
    if (!result || !element) return nullptr;
    return result->find_primary_box(element);
}

// The operation owns geometry and immutable versions independently from caller handles.
struct input_callback_context_s {
    const StyleLookup& lookup;
    aether_scroll_offset_fn callback;
    void* data;
    CallbackEpochGuard epochs;
    CallbackEpochGuard provenance_epochs;
    bool valid() const noexcept { return epochs.valid() && provenance_epochs.valid(); }
    input_callback_context_s(const StyleLookup& styles, aether_scroll_offset_fn fn, void* user_data)
        : lookup(styles), callback(fn), data(user_data), epochs(styles.document_lifetime()), provenance_epochs(styles.base_version()) {}
};
static bool input_record_alive(const aether::style::BoundNodeView& record, const aether_node_s* node) {
    if (!record || !record.payload_owner()->node_lifetime()) return true;
    const auto& identity = record.payload_owner()->node_lifetime();
    return identity->owner == node && identity->alive && identity->attached;
}
static aether_scroll_offset_t checked_input_offsets(aether_node_t node, void* data) {
    auto& context = *static_cast<input_callback_context_s*>(data);
    // Historical input guards use base provenance, even for an external paint override.
    const auto& base = context.lookup.base_version();
    const auto record = base ? base->lookup(node) : aether::style::BoundNodeView{};
    if (!input_record_alive(record, node)) throw std::invalid_argument("retired input node");
    const auto offset = context.callback(node, context.data);
    if (!context.valid() || !input_record_alive(record, node))
        throw std::invalid_argument("input callback changed document");
    return offset;
}
static bool live_input_owner(const StyleLookup& lookup, aether_node_t node) {
    if (!node) return true;
    const auto& base = lookup.base_version();
    if (!base || !base->contextual()) return true;
    const auto record = base->lookup(node);
    return record && record.payload_owner()->node_lifetime() && input_record_alive(record, node);
}
static aether_node_t hit_with_pin(aether_layout_result_t result, aether_style_map_t styles,
    float x, float y, aether_scroll_offset_fn offsets, void* user_data) {
    if (!result) return nullptr;
    LayoutOperationPin geometry(result);
    StyleOperationPin pin(result->assets, aether::style::pin_style_map(styles), styles != nullptr);
    if (!pin.structure_valid() || !pin.accepts_override()) return nullptr;
    const auto policy = styles ? BoxStylePolicy::effective_map : BoxStylePolicy::snapshot;
    input_callback_context_s context(pin.lookup(), offsets, user_data);
    auto* hit = geometry.geometry()->hit_test(x, y, pin.lookup(), policy,
        offsets ? checked_input_offsets : nullptr, offsets ? &context : user_data);
    return pin.structure_valid() && context.valid() && live_input_owner(pin.lookup(), hit) ? hit : nullptr;
}
AETHER_API aether_node_t aether_layout_result_hit_test(aether_layout_result_t result, float x, float y) {
    try { return hit_with_pin(result, nullptr, x, y, nullptr, nullptr); } catch (...) { return nullptr; }
}
AETHER_API aether_node_t aether_layout_result_hit_test_with_scroll_offsets(
    aether_layout_result_t result, float x, float y,
    aether_scroll_offset_fn offsets, void* user_data) {
    try { return hit_with_pin(result, nullptr, x, y, offsets, user_data); } catch (...) { return nullptr; }
}
AETHER_API aether_node_t aether_layout_result_hit_test_with_styles_and_scroll_offsets(
    aether_layout_result_t result, aether_style_map_t styles, float x, float y,
    aether_scroll_offset_fn offsets, void* user_data) {
    try { return hit_with_pin(result, styles, x, y, offsets, user_data); } catch (...) { return nullptr; }
}
AETHER_API aether_status_t aether_layout_result_hit_test_input(
    aether_layout_result_t result, aether_style_map_t current_styles, float x, float y,
    aether_scroll_offset_fn offsets, void* user_data,
    aether_input_hit_part_t* out_part, aether_node_t* out_owner) {
    if (out_part) *out_part = AETHER_INPUT_HIT_NONE;
    if (out_owner) *out_owner = nullptr;
    if (!result || !out_part || !out_owner) return AETHER_ERROR_NULL_ARG;
    LayoutOperationPin geometry(result);
    StyleOperationPin pin(result->assets, aether::style::pin_style_map(current_styles), current_styles != nullptr);
    if (!pin.structure_valid() || !pin.accepts_override()) return AETHER_ERROR_INVALID_STATE;
    try {
        const auto policy = current_styles ? BoxStylePolicy::effective_map : BoxStylePolicy::snapshot;
        input_callback_context_s context(pin.lookup(), offsets, user_data);
        const auto hit = layout_hit_test_input(geometry.geometry(), geometry.geometry()->root, pin.lookup(), policy, x, y,
            offsets ? checked_input_offsets : nullptr, offsets ? &context : user_data);
        if (!pin.structure_valid() || !context.valid() || !live_input_owner(pin.lookup(), hit.owner))
            return AETHER_ERROR_INVALID_STATE;
        *out_part = hit.part;
        *out_owner = hit.owner;
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
    catch (...) { return AETHER_ERROR_INVALID_STATE; }
}

AETHER_API int32_t aether_layout_result_get_paint_order_count(aether_layout_result_t result) {
    return result ? static_cast<int32_t>(result->paint_order.size()) : 0;
}

AETHER_API aether_layout_box_t aether_layout_result_get_paint_order_box(aether_layout_result_t result, int32_t index) {
    if (!result || index < 0 || index >= static_cast<int32_t>(result->paint_order.size())) return nullptr;
    return result->paint_order[index];
}

/* ── LayoutBox queries ─────────────────────────────────────────────── */

AETHER_API int32_t aether_layout_box_get_kind(aether_layout_box_t box) {
    return box ? static_cast<int32_t>(box->kind) : -1;
}

AETHER_API float aether_layout_box_get_x(aether_layout_box_t box) {
    return box ? box->border_box.x : 0;
}
AETHER_API float aether_layout_box_get_y(aether_layout_box_t box) {
    return box ? box->border_box.y : 0;
}
AETHER_API float aether_layout_box_get_width(aether_layout_box_t box) {
    return box ? box->border_box.width : 0;
}
AETHER_API float aether_layout_box_get_height(aether_layout_box_t box) {
    return box ? box->border_box.height : 0;
}
AETHER_API aether_rectf aether_layout_box_get_margin_box(aether_layout_box_t box) {
    return box ? box->margin_box : aether_rectf{};
}
AETHER_API aether_rectf aether_layout_box_get_border_box(aether_layout_box_t box) {
    return box ? box->border_box : aether_rectf{};
}
AETHER_API aether_rectf aether_layout_box_get_padding_box(aether_layout_box_t box) {
    return box ? box->padding_box : aether_rectf{};
}
AETHER_API aether_rectf aether_layout_box_get_content_box(aether_layout_box_t box) {
    return box ? box->content_box : aether_rectf{};
}
AETHER_API aether_bool_t aether_layout_box_is_atomic(aether_layout_box_t box) {
    return box && box->is_atomic_control ? AETHER_TRUE : AETHER_FALSE;
}

AETHER_API char* aether_layout_box_get_text(aether_layout_box_t box) {
    if (!box || box->text.empty()) return nullptr;
    return aether::strdup(box->text.c_str());
}

AETHER_API aether_scroll_metrics_t aether_layout_box_get_scroll_metrics(aether_layout_box_t box) {
    return box ? box->scroll : aether_scroll_metrics_t{};
}
AETHER_API aether_scrollbar_geometry_t aether_layout_box_get_scrollbar_geometry(aether_layout_box_t box) {
    return box ? aether_resolve_scrollbar_geometry(*box) : aether_scrollbar_geometry_t{};
}
AETHER_API aether_scrollbar_geometry_t aether_layout_box_get_scrollbar_geometry_at_offset(
    aether_layout_box_t box,
    aether_scroll_offset_t offset) {
    return box ? aether_resolve_scrollbar_geometry(*box, offset) : aether_scrollbar_geometry_t{};
}
AETHER_API float aether_layout_box_get_scrollable_overflow_x(aether_layout_box_t box) {
    return box ? box->scroll.scrollable_overflow_x : 0;
}
AETHER_API float aether_layout_box_get_scrollable_overflow_y(aether_layout_box_t box) {
    return box ? box->scroll.scrollable_overflow_y : 0;
}
AETHER_API float aether_layout_box_get_scroll_offset_x(aether_layout_box_t box) {
    return box ? box->scroll.scroll_offset_x : 0;
}
AETHER_API float aether_layout_box_get_scroll_offset_y(aether_layout_box_t box) {
    return box ? box->scroll.scroll_offset_y : 0;
}
AETHER_API aether_node_t aether_layout_box_get_element(aether_layout_box_t box) {
    return box ? box->element : nullptr;
}
AETHER_API int32_t aether_layout_box_get_child_count(aether_layout_box_t box) {
    return box ? static_cast<int32_t>(box->children.size()) : 0;
}
AETHER_API aether_layout_box_t aether_layout_box_get_child(aether_layout_box_t box, int32_t index) {
    if (!box || index < 0 || index >= static_cast<int32_t>(box->children.size())) return nullptr;
    return box->children[index];
}

} // extern "C"

// D2 diagnostic API. Never invoked by production preparation or rendering.
extern "C" {
AETHER_API aether_status_t aether_layout_context_validate_full(aether_transition_runtime_t runtime,
    aether_style_map_t map,aether_layout_result_t layout,aether_bool_t* out_equal) {
    if(out_equal)*out_equal=AETHER_FALSE;
    if(!map || !layout || !out_equal)return AETHER_ERROR_NULL_ARG;
    try {
        const StyleLookup styles(aether::style::pin_style_map(map),nullptr,{});
        *out_equal=context_membership_equal(effective_fixed_containers(&styles,aether_active_transform_owners(runtime)),layout->effective_context_owners);
        return AETHER_OK;
    } catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}
}
AETHER_API aether_status_t aether_layout_context_snapshot_info(aether_layout_result_t layout,uint64_t* out_serial,uint64_t* out_size) {
    if(!layout || !out_serial || !out_size)return AETHER_ERROR_NULL_ARG;
    *out_serial=layout->effective_context_owners?layout->effective_context_owners->membership_serial:0;
    *out_size=layout->effective_context_owners?layout->effective_context_owners->owners.size():0;
    return AETHER_OK;
}
}
