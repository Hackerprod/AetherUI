#include "initial_canvas.h"
/* ── Layout Engine — core box layout, flow, inline, and intrinsic sizing ─ */
#include "layout_internal.h"
#include "layout_scroll_bounds.h"
#include "inline_line_resolver.h"
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
                                        const BoxStyleSnapshot& style,
                                        padding_resolution_phase_e phase) {
    const auto& owner = style.measures_owner();
    const auto side = [&](const char* property, aether_css_length scalar) {
        if (owner) {
            const aether::style::MeasuresReadView measures(*owner);
            if (const auto tree = measures.contextual_tree(property)) {
                const bool percent = aether::css::math_has_percentage(tree);
                if (phase == padding_resolution_phase_e::indefinite && percent)
                    throw std::invalid_argument("Indefinite contextual padding requires a layout phase");
                const double basis = phase == padding_resolution_phase_e::intrinsic_cyclic ? 0. : reference;
                double value = 0.;
                if (!aether::css::resolve_contextual_math(tree, basis, value) ||
                    !std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                    throw std::overflow_error("Invalid contextual used padding");
                return std::max(0.f, static_cast<float>(value));
            }
        }
        if (phase == padding_resolution_phase_e::indefinite &&
            (scalar.unit == AETHER_CSS_UNIT_PERCENT ||
             (scalar.unit == AETHER_CSS_UNIT_LINEAR && scalar.linear_percent != 0)))
            throw std::invalid_argument("Indefinite padding requires a layout phase");
        return std::max(0.f, resolve(e, scalar,
            phase == padding_resolution_phase_e::intrinsic_cyclic ? 0.f : reference, style));
    };
    // Resolve all four before returning: failure cannot publish partial edges.
    return {side("padding-left", edges.left), side("padding-top", edges.top),
            side("padding-right", edges.right), side("padding-bottom", edges.bottom)};
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
    return BoxStyleSnapshot(owner ? std::move(owner) : styles->default_owner(),
        styles->lookup_measures_owner(el));
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
         result->effective_fixed_container_nodes.count(box->element) != 0);
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
    // Legacy justification never receives hanging-line fragments. Do not retain
    // a stale shape if this generic mutator is reused for such a box later.
    box->scroll_proxy.valid = false;
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

static initial_canvas_dimensions canvas_size(const aether_layout_engine_s* engine,
    const aether_node_s* node,const BoxStyleSnapshot& style,aether_thicknessf padding,
    aether_thicknessf border,float reference) {
    return initial_canvas_content_size(*initial_canvas_for(engine,node),
        resolve(engine,style.width,reference,style), resolve(engine,style.height,reference,style),
        style.box_sizing==AETHER_BOX_SIZING_BORDER_BOX,
        aether_thicknessf_horizontal(padding)+aether_thicknessf_horizontal(border),
        aether_thicknessf_vertical(padding)+aether_thicknessf_vertical(border));
}

static intrinsic_widths_s measure_intrinsic_flex_widths(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result,
    intrinsic_width_request_s request);

static intrinsic_widths_s measure_inline_items_intrinsic_widths(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    const std::vector<inline_item_s>& items,
    const BoxStyleSnapshot& parent_style, float maximum,
    aether_layout_result_s* result,
    intrinsic_width_request_s request);

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
    element_completion_s* deferred_completion,
    const intrinsic_measurement_s* intrinsic_measurement,
    float intrinsic_available_width)
{
    auto style = get_style(styles, element);
    if (style.display == AETHER_DISPLAY_CONTENTS)
        throw std::invalid_argument("CONTENTS_BOX_CONTEXT_UNSUPPORTED");
    const bool intrinsic_width = intrinsic_measurement &&
        intrinsic_measurement->ignores(element, intrinsic_axis_e::width);
    const bool intrinsic_height = intrinsic_measurement &&
        intrinsic_measurement->ignores(element, intrinsic_axis_e::height);
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
    box->is_initial_canvas = initial_canvas_for(engine,element)!=nullptr;
    initial_canvas_dimensions canvas{};
    if(box->is_initial_canvas) canvas=canvas_size(engine,element,style,padding,border,containing_block.width);
    box->collapsed_margin_before = margin.top;
    box->collapsed_margin_after = margin.bottom;

    const auto width_measures = layout_measures(styles, element);
    const auto is_keyword = [&](const char* property) {
        return width_measures && width_measures->intrinsic_keyword(property) != aether::css::intrinsic_size_keyword::none;
    };
    const auto used_width = [&](const char* property, aether_css_length value) {
        return is_keyword(property) ? measure_keyword_border_width(engine, styles, element, style, property,
            containing_block.width, std::isfinite(containing_block.width), result,
            intrinsic_keyword_phase_e::used, intrinsic_available_width) : resolve(engine, value, containing_block.width, style);
    };
    float available_border_width = std::max(0.f, containing_block.width - aether_thicknessf_horizontal(margin));
    float specified_width = intrinsic_width ? NAN
        : used_width("width", style.width);
    float border_width = specified_width;

    if (box->is_initial_canvas && !is_keyword("width")) {
        border_width = canvas.width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    } else if (has_forced_border_width) {
        border_width = forced_border_width;
    } else if (!std::isfinite(border_width)) {
        border_width = force_available_width
            ? available_border_width
            : measure_intrinsic_width(engine, styles, element, style, padding, border,
                                      containing_block.width, result);
    } else if (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX && !is_keyword("width")) {
        border_width += aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    }

    float max_width = intrinsic_width ? NAN
        : used_width("max-width", style.max_width);
    if (std::isfinite(max_width)) {
        float max_border = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX && !is_keyword("max-width"))
            ? max_width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border)
            : max_width;
        border_width = std::min(border_width, max_border);
    }

    float min_width = intrinsic_width ? NAN
        : used_width("min-width", style.min_width);
    if (std::isfinite(min_width)) {
        float min_border = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX && !is_keyword("min-width"))
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
        explicit_height = explicit_height_from_forced_border(style, forced_border_height,
            aether_thicknessf_vertical(padding), aether_thicknessf_vertical(border));
        has_explicit_height = true;
    } else {
        explicit_height = intrinsic_height ? NAN
            : resolve_semantic_height(engine, style.height, containing_height, style, element, "height");
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
    } else if (forced_height_kind == forced_height_kind_e::none && !intrinsic_height) {
        const float specified = resolve_semantic_height(engine, style.height, containing_height, style, element, "height");
        if (std::isfinite(specified)) semantic_border_height = specified +
            (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX ? height_decorations : 0.f);
    }
    if (std::isfinite(semantic_border_height)) {
        semantic_border_height = intrinsic_height ? std::max(height_decorations, semantic_border_height)
            : clamp_border_height(engine, style, semantic_border_height,
                containing_height.value_or(NAN), height_decorations, true, element);
        child_percentage_height = std::max(0.f, semantic_border_height - height_decorations);
    }
    if (child_percentage_height) child_containing_height = *child_percentage_height;

    flex_placement_plan_s flex_placement;
    float content_height;
    if (box->is_initial_canvas) {
        content_height = canvas.height;
    } else if (intrinsic_control) {
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
                                              containing_height, height_decorations, flex_placement,
                                              result, intrinsic_measurement);
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
    if (!intrinsic_control && !box->is_initial_canvas && style.display == AETHER_DISPLAY_BLOCK) {
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
        ? border_height_from_explicit(style, explicit_height,
            aether_thicknessf_vertical(padding), aether_thicknessf_vertical(border))
        : content_height + aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border);

    border_height = intrinsic_height ? std::max(height_decorations, border_height)
        : clamp_border_height(engine, style, border_height,
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
    element_completion_s completion{box, margin, padding, border,
                                    containing_block, containing_height, std::move(flex_placement)};
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
        *deferred_completion = std::move(completion);
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
    place_flex_children(styles, box, completion.flex_placement, final_content);
    if (!box->is_initial_canvas) layout_positioned_children(engine, styles, box, box->element,
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

// Retaining admission evidence must not create a legacy paragraph or consume
// a pending collapsed block margin.
static bool has_legacy_inline_content(const std::vector<inline_item_s>& items) {
    return std::any_of(items.begin(), items.end(),
        [](const inline_item_s& item) { return !item.legacy_ignored; });
}

static bool transparent_unit_inline(const BoxStyleSnapshot& style) {
    const auto zero = [](aether_css_length v) {
        return v.unit != AETHER_CSS_UNIT_AUTO && v.value == 0.f &&
            v.linear_percent == 0.f && v.linear_em == 0.f && v.linear_rem == 0.f &&
            v.linear_vw == 0.f && v.linear_vh == 0.f;
    };
    const auto edges_zero = [&](aether_css_edges e) {
        return zero(e.left) && zero(e.top) && zero(e.right) && zero(e.bottom);
    };
    return (style.display == AETHER_DISPLAY_INLINE || style.display == AETHER_DISPLAY_LINE_BREAK) &&
        style.position == AETHER_POSITION_STATIC &&
        edges_zero(style.margin) && edges_zero(style.padding) && edges_zero(style.border_width) &&
        style.background_color.a == 0.f && style.opacity == 1.f &&
        style.view().background_layers().empty() && style.view().transforms().empty() &&
        style.view().box_shadows().empty() && style.view().text_shadows().empty() &&
        style.view().backdrop_filters().empty() && !style.has_text_gradient;
}

static void retain_unit_admission(std::vector<inline_item_s>& output,
    aether_node_s* owner, const BoxStyleSnapshot& style, bool barrier) {
    auto marker = inline_item_s::text_item(owner, style, "");
    marker.legacy_ignored = true;
    marker.unit_barrier = barrier;
    output.push_back(std::move(marker));
}

// One side-effect-free admission decision on the entire IFC, including empty
// inline ancestors. There is no prefix admission or fallback after measuring.
// ASCII excludes bidi/shaping/Unicode line-breaking from this initial contract.
static bool uses_source_unit_resolver(const std::vector<inline_item_s>& items,
                                     const BoxStyleSnapshot& parent_style,
                                     bool intrinsic_width = false) {
    const auto mode = parent_style.white_space;
    // PRE_WRAP is admitted only for intrinsic widths. Its hanging/geometry
    // resolver stays unchanged; HTML newlines separated by markup remain two breaks.
    if (!(intrinsic_width && mode == AETHER_WHITE_SPACE_PRE_WRAP) &&
        mode != AETHER_WHITE_SPACE_NORMAL && mode != AETHER_WHITE_SPACE_PRE_LINE &&
        mode != AETHER_WHITE_SPACE_NOWRAP && mode != AETHER_WHITE_SPACE_PRE) return false;
    for (const auto& item : items) {
        if (item.unit_barrier || item.hanging_barrier || item.is_atomic ||
            item.block_anchor_index != static_cast<size_t>(-1) || item.style.white_space != mode)
            return false;
        for (const unsigned char c : item.text)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == ' ' || c == '\r' || c == '\n'))
                return false;
    }
    return true;
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
        if (!has_legacy_inline_content(inline_items)) { inline_items.clear(); return; }
        // An eligible all-collapsed sequence has no line at all. In particular,
        // nested whitespace must not consume the margin separating two blocks.
        if (uses_source_unit_resolver(inline_items, parent_style) &&
            std::none_of(inline_items.begin(), inline_items.end(), [&](const inline_item_s& item) {
                return item.is_line_break ||
                    aether_inline_text_has_visible_content(item.text, parent_style.white_space);
            })) {
            inline_items.clear();
            return;
        }
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
            if (is_out_of_flow(style) &&
                (style.display == AETHER_DISPLAY_BLOCK || style.display == AETHER_DISPLAY_FLEX ||
                 style.display == AETHER_DISPLAY_TABLE)) {
                const size_t index = parent_box->block_static_anchors.size();
                parent_box->block_static_anchors.push_back({node, cursor_owner, 0.f,
                    cursor_y - content.y + (has_pending_margin ? pending_margin : 0.f)});
                // A marker never creates a run or flushes a pending block margin.
                // Existing real runs retain their existing whitespace/flush behavior.
                if (has_legacy_inline_content(inline_items)) inline_items.push_back(inline_item_s::block_anchor(index));
                retain_unit_admission(inline_items, node, style, true);
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

// The phase-one path is deliberately sequence-wide. Unsupported content or a
// flattened inline decoration keeps the complete legacy sequence, rather than
// mixing incompatible line decisions halfway through a paragraph.
static bool uses_hanging_line_resolver(const std::vector<inline_item_s>& items) {
    bool pre_wrap = false;
    bool nowrap_content = false, nowrap_pending_space = false;
    for (const auto& item : items) {
        if (item.legacy_ignored) continue;
        if (item.hanging_barrier || item.block_anchor_index != static_cast<size_t>(-1)) return false;
        pre_wrap |= item.style.white_space == AETHER_WHITE_SPACE_PRE_WRAP;
        for (unsigned char c : item.text) {
            if (c >= 128 || c == '\t' || c == '\v' || c == '\f') return false;
        }
        // CSS trailing collapse precedes hanging. This phase does not replace
        // that legacy algorithm: mixed collapsible whitespace is admitted only
        // when it is provably internal to an unbreakable nowrap unit. Decide
        // before any measurement, never fall back after processing a prefix.
        if (item.is_atomic || item.is_line_break || item.style.white_space != AETHER_WHITE_SPACE_NOWRAP) {
            if (nowrap_pending_space) return false;
            nowrap_content = false;
        }
        for (const auto& token : aether_tokenize_inline_text(item.text, item.style.white_space)) {
            if (token.kind == aether_inline_text_token_kind_e::forced_break) {
                if (nowrap_pending_space) return false;
                nowrap_content = false;
            } else if (token.kind == aether_inline_text_token_kind_e::collapsible_space) {
                if (item.style.white_space != AETHER_WHITE_SPACE_NOWRAP || !nowrap_content) return false;
                nowrap_pending_space = true;
            } else if (item.style.white_space == AETHER_WHITE_SPACE_NOWRAP) {
                nowrap_content = true;
                nowrap_pending_space = false;
            }
        }
    }
    return pre_wrap && !nowrap_pending_space;
}

struct measured_inline_fragment {
    size_t item_index = 0;
    std::string text;
    inline_line_metrics metrics;
    aether_layout_box_s* atomic_box = nullptr;
    std::vector<aether_layout_box_s*> atomic_paint_order;
    aether::rendering::text::Placement native_text;
};

// Source-unit preparation is operation-local and never measures or joins text
// belonging to different owners. An inline/comment/empty-span boundary is not a
// line-break opportunity. Admission is performed once before calling this code.
struct inline_source_fragment {
    size_t item_index;
    std::string text;
};
enum class inline_source_kind { word, space, forced };
struct inline_source_unit {
    inline_source_kind kind;
    std::vector<inline_source_fragment> fragments;
};

static std::vector<inline_source_unit> collect_source_units(
    const std::vector<inline_item_s>& items, int32_t mode)
{
    std::vector<inline_source_unit> units;
    bool previous_cr = false, line_content = false, pending_space = false;
    size_t pending_owner = 0;
    const bool preserve_spaces = mode == AETHER_WHITE_SPACE_PRE ||
        mode == AETHER_WHITE_SPACE_PRE_WRAP;
    const bool preserve_breaks = preserve_spaces || mode == AETHER_WHITE_SPACE_PRE_LINE;
    auto append = [&](inline_source_kind kind, size_t owner, char c) {
        if (units.empty() || units.back().kind != kind)
            units.push_back({kind, {}});
        auto& fragments = units.back().fragments;
        if (fragments.empty() || fragments.back().item_index != owner)
            fragments.push_back({owner, {}});
        fragments.back().text += c;
    };
    auto forced = [&]() {
        units.push_back({inline_source_kind::forced, {}});
        pending_space = false;
        line_content = false;
    };
    for (size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (item.is_line_break) {
            forced();
            previous_cr = false;
            continue;
        }
        for (const char c : item.text) {
            if (c == '\n' && previous_cr) { previous_cr = false; continue; }
            previous_cr = c == '\r';
            if ((c == '\r' || c == '\n') && preserve_breaks) {
                forced();
                continue;
            }
            const bool space = c == ' ' || c == '\r' || c == '\n';
            if (space && !preserve_spaces) {
                if (line_content && !pending_space) {
                    pending_space = true;
                    pending_owner = index;
                }
                continue;
            }
            if (pending_space) {
                append(inline_source_kind::space, pending_owner, ' ');
                pending_space = false;
            }
            append(space ? inline_source_kind::space : inline_source_kind::word, index, c);
            line_content = true;
        }
    }
    return units;
}

static float checked_inline_sum(float left, float right) {
    const float sum = left + right;
    if (!std::isfinite(sum)) throw std::overflow_error("Non-finite inline unit advance");
    return sum;
}

struct measured_source_unit {
    std::vector<measured_inline_fragment> fragments;
    float width = 0.f;
};
static measured_source_unit measure_source_unit(const aether_layout_engine_s* engine,
    const std::vector<inline_item_s>& items, const inline_source_unit& source)
{
    measured_source_unit result;
    for (const auto& fragment : source.fragments) {
        const auto& item = items[fragment.item_index];
        const auto measured = engine_measure_text_transport(engine, fragment.text.c_str(), &item.style, 0.f, item.element);
        const auto metrics = measured.metrics;
        result.width = checked_inline_sum(result.width, metrics.width);
        result.fragments.push_back({fragment.item_index, fragment.text,
            {metrics.width, metrics.height, metrics.baseline, true,
             source.kind == inline_source_kind::space, item.style.white_space}, nullptr, {}, {measured.run, metrics.baseline}});
    }
    return result;
}

// Width, height and geometry share the same source units and callback slices.
// A collapsible separator waits for the next whole word, so wrapping drops it
// rather than painting trailing whitespace or allowing a break inside a word.
template<class EmitLine>
static float resolve_source_inline_lines(
    const aether_layout_engine_s* engine, const std::vector<inline_item_s>& items,
    int32_t mode, float available_width, float minimum_line_height, EmitLine emit_line)
{
    const auto units = collect_source_units(items, mode);
    const bool wrap = aether_white_space_allows_soft_wrap(mode);
    inline_line_resolver resolver(available_width, minimum_line_height);
    std::vector<measured_inline_fragment> line;
    measured_source_unit pending;
    float total_height = 0.f;
    auto close = [&](inline_line_end end) {
        if (!resolver.empty() || end == inline_line_end::forced) {
            const auto summary = resolver.close(end);
            emit_line(line, summary, total_height, end);
            total_height = checked_inline_sum(total_height, summary.height);
        }
        resolver.reset();
        line.clear();
    };
    auto accept = [&](measured_source_unit& unit) {
        for (auto& fragment : unit.fragments) {
            resolver.append(fragment.metrics);
            line.push_back(std::move(fragment));
        }
        unit.fragments.clear();
        unit.width = 0.f;
    };
    for (const auto& source : units) {
        if (source.kind == inline_source_kind::forced) {
            close(inline_line_end::forced);
            continue;
        }
        auto unit = measure_source_unit(engine, items, source);
        if (wrap && source.kind == inline_source_kind::space) {
            pending = std::move(unit);
            continue;
        }
        if (wrap && !pending.fragments.empty() &&
            resolver.needs_break(checked_inline_sum(pending.width, unit.width), true)) {
            close(inline_line_end::soft);
            pending.fragments.clear();
            pending.width = 0.f;
        }
        accept(pending);
        accept(unit);
    }
    close(inline_line_end::paragraph_end);
    return total_height;
}

static aether_inline_intrinsic_sizes_s measure_source_intrinsic_sizes(
    const aether_layout_engine_s* engine, const std::vector<inline_item_s>& items, int32_t mode)
{
    const auto units = collect_source_units(items, mode);
    aether_inline_intrinsic_sizes_s result{};
    float line_width = 0.f;
    const bool wrap = aether_white_space_allows_soft_wrap(mode);
    for (const auto& source : units) {
        if (source.kind == inline_source_kind::forced) {
            result.max_content = std::max(result.max_content, line_width);
            line_width = 0.f;
            continue;
        }
        const auto unit = measure_source_unit(engine, items, source);
        line_width = checked_inline_sum(line_width, unit.width);
        if (wrap && source.kind == inline_source_kind::word)
            result.min_content = std::max(result.min_content, unit.width);
    }
    result.max_content = std::max(result.max_content, line_width);
    if (!wrap) result.min_content = result.max_content;
    return result;
}

// Shared streaming adapter for height measurement and final geometry. It only
// buffers the current line and the next indivisible unit. Token strings stay
// individual, and immutable item indexes survive every vector reallocation.
// Every first measurement occurs in source order; closing a line never measures.
template<class MeasureAtomic, class EmitLine>
static float resolve_hanging_inline_lines(
    const aether_layout_engine_s* engine, const std::vector<inline_item_s>& items,
    float available_width, float minimum_line_height,
    MeasureAtomic measure_atomic, EmitLine emit_line)
{
    inline_line_resolver resolver(available_width, minimum_line_height);
    std::vector<measured_inline_fragment> line, unit;
    float total_height = 0.f, unit_width = 0.f;
    bool previous_collapsible = false;
    bool opportunity = false, preserved_opportunity = false;
    bool unit_can_wrap = false;
    auto close = [&](inline_line_end end) {
        if (!resolver.empty() || end == inline_line_end::forced) {
            const auto summary = resolver.close(end);
            emit_line(line, summary, total_height, end);
            total_height += summary.height;
            if (!std::isfinite(total_height)) throw std::overflow_error("Non-finite inline height");
        }
        resolver.reset(); line.clear();
        opportunity = preserved_opportunity = false;
    };
    auto accept = [&](measured_inline_fragment fragment) {
        resolver.append(fragment.metrics);
        line.push_back(std::move(fragment));
    };
    auto flush_unit = [&]() {
        if (unit.empty()) return;
        if (resolver.needs_break(unit_width, opportunity && (preserved_opportunity || unit_can_wrap)))
            close(inline_line_end::soft);
        for (auto& fragment : unit) accept(std::move(fragment));
        unit.clear(); unit_width = 0.f;
        opportunity = preserved_opportunity = false;
    };
    for (size_t item_index = 0; item_index < items.size(); ++item_index) {
        const auto& item = items[item_index];
        if (item.legacy_ignored) continue;
        if (item.is_line_break) {
            flush_unit(); close(inline_line_end::forced); previous_collapsible = false;
            continue;
        }
        if (item.is_atomic) {
            flush_unit();
            // The operation is performed once at its source position. Emission
            // translates the already-built atom if its complete width must wrap.
            auto fragment = measure_atomic(item_index, total_height);
            if (resolver.needs_break(fragment.metrics.width, true)) close(inline_line_end::soft);
            accept(std::move(fragment));
            opportunity = true; preserved_opportunity = false;
            previous_collapsible = false;
            continue;
        }
        for (const auto& token : aether_tokenize_inline_text(item.text, item.style.white_space)) {
            if (token.kind == aether_inline_text_token_kind_e::forced_break) {
                flush_unit(); close(inline_line_end::forced); previous_collapsible = false;
                continue;
            }
            const bool collapsible = token.kind == aether_inline_text_token_kind_e::collapsible_space;
            const bool whitespace = token.kind != aether_inline_text_token_kind_e::word;
            const bool wrap = aether_white_space_allows_soft_wrap(item.style.white_space);
            if (whitespace && wrap) flush_unit();
            if (collapsible && (previous_collapsible || (resolver.empty() && unit.empty()))) continue;
            const auto measured = engine_measure_text_transport(engine, token.text.c_str(), &item.style, 0.f, item.element);
            const auto metrics = measured.metrics;
            measured_inline_fragment fragment{item_index, token.text,
                {metrics.width, metrics.height, metrics.baseline, true, whitespace, item.style.white_space}, nullptr, {}, {measured.run, metrics.baseline}};
            if (whitespace && wrap) {
                // A preserved trailing group never wraps due to its own width.
                accept(std::move(fragment));
                opportunity = true;
                preserved_opportunity |= item.style.white_space == AETHER_WHITE_SPACE_PRE_WRAP;
            } else {
                if (unit.empty()) unit_can_wrap = wrap;
                unit_width += metrics.width;
                if (!std::isfinite(unit_width)) throw std::overflow_error("Non-finite inline unit");
                unit.push_back(std::move(fragment));
            }
            previous_collapsible = collapsible;
        }
    }
    flush_unit(); close(inline_line_end::paragraph_end);
    return total_height;
}

static float layout_resolved_inline_items(
    const aether_layout_engine_s* engine, const StyleLookup* styles,
    aether_layout_box_s* parent_box, const std::vector<inline_item_s>& items,
    float start_x, float start_y, float available_width, float available_height,
    percentage_height_s containing_height, const BoxStyleSnapshot& parent_style,
    aether_layout_result_s* result, bool source_units = false)
{
    const float minimum = std::max(layout_used_line_height(engine, parent_box->element, parent_style), 1.f);
    auto atomic = [&](size_t index, float y) {
        const auto& item = items[index];
        const size_t paint_start = result->paint_order.size();
        auto* box = layout_element(engine, styles, item.element,
            {start_x, start_y + y, available_width, available_height}, containing_height,
            start_y + y, false, 0, false, 0, forced_height_kind_e::none, true, result);
        measured_inline_fragment fragment{index, {},
            {box->margin_box.width, box->margin_box.height, 0.f, false, false, item.style.white_space}, box, {}};
        // Text prefixes are emitted when their baseline/alignment is known.
        // Keep the public paint-order index in DOM order despite measuring an
        // atom before closing its prefix. The already-built tree is not replayed.
        fragment.atomic_paint_order.assign(result->paint_order.begin() + paint_start, result->paint_order.end());
        result->paint_order.resize(paint_start);
        return fragment;
    };
    auto emit = [&](const std::vector<measured_inline_fragment>& line, const inline_line_summary& summary,
                    float y, inline_line_end end) {
        float x = start_x;
        const int align = parent_style.text_align;
        if (align == AETHER_TEXT_ALIGN_CENTER)
            x += std::max(0.f, (available_width - summary.alignment_width) / 2.f);
        else if (align == AETHER_TEXT_ALIGN_RIGHT || align == AETHER_TEXT_ALIGN_END)
            x += std::max(0.f, available_width - summary.alignment_width);
        int gaps = 0;
        for (size_t i = summary.justify_begin; i < summary.justify_end; ++i)
            if (line[i].metrics.whitespace) gaps += static_cast<int>(line[i].text.size());
        const float extra = end == inline_line_end::soft && align == AETHER_TEXT_ALIGN_JUSTIFY ?
            aether_text_justify_extra_per_gap(available_width, summary.alignment_width, gaps) : 0.f;
        const double line_left = x;
        double justify_advance = 0.;
        for (size_t i = summary.justify_begin; summary.hanging_width > 0.f && extra != 0.f && i < summary.justify_end; ++i) {
            if (!line[i].metrics.whitespace) continue;
            const float expanded = line[i].metrics.width + extra * static_cast<float>(line[i].text.size());
            justify_advance += double(expanded) - line[i].metrics.width;
        }
        // This is the line's real aligned interval, not the containing width.
        // An overlong word must retain its genuine overflow.
        const double line_right = line_left + summary.alignment_width + justify_advance;
        if (summary.hanging_width > 0.f) {
            aether_check_scroll_coordinate(line_left);
            aether_check_scroll_coordinate(line_right);
            if (line_right < line_left) throw std::invalid_argument("inverted hanging interval");
        }
        for (size_t i = 0; i < line.size(); ++i) {
            const auto& fragment = line[i];
            const auto& item = items[fragment.item_index];
            auto* box = fragment.atomic_box;
            float width = fragment.metrics.width;
            if (i >= summary.justify_begin && i < summary.justify_end && fragment.metrics.whitespace)
                width += extra * static_cast<float>(fragment.text.size());
            if (box) {
                const auto normal = normal_flow_margin_box(box);
                offset_box(box, x - normal.x, start_y + y - normal.y);
                result->paint_order.insert(result->paint_order.end(),
                    fragment.atomic_paint_order.begin(), fragment.atomic_paint_order.end());
            } else {
                box = result->create_box();
                box->kind = LAYOUT_BOX_TEXT;
                box->element = item.element;
                box->style = item.style;
                box->presentation_owner = item.element ? item.element->anonymous_flex_owner : nullptr;
                box->text = fragment.text;
                box->native_text = fragment.native_text;
                box->content_box = {x, start_y + y + std::max(0.f, summary.ascent - fragment.metrics.baseline),
                    width, fragment.metrics.height};
                box->padding_box = box->border_box = box->margin_box = box->content_box;
                if (summary.hanging_width > 0.f) {
                    const auto& rect = box->content_box;
                    box->scroll_proxy = {std::clamp(double(rect.x), line_left, line_right),
                        std::clamp(double(rect.x) + rect.width, line_left, line_right),
                        rect.x, true};
                    // Validate before publication, with the same helper used by
                    // both scroll domains. Physical paint/hit geometry is intact.
                    (void)aether_scroll_own_bounds(box);
                }
                if (result->records_boxes) result->paint_order.push_back(box);
            }
            parent_box->add_child(box);
            x += width;
        }
    };
    if (source_units)
        return resolve_source_inline_lines(engine, items, parent_style.white_space,
            available_width, minimum, emit);
    return resolve_hanging_inline_lines(engine, items, available_width, minimum, atomic, emit);
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
    if (uses_source_unit_resolver(items, parent_style))
        return layout_resolved_inline_items(engine, styles, parent_box, items,
            start_x, start_y, available_width, available_height, containing_height, parent_style, result, true);
    if (uses_hanging_line_resolver(items))
        return layout_resolved_inline_items(engine, styles, parent_box, items,
            start_x, start_y, available_width, available_height, containing_height, parent_style, result);
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
                const auto metrics = layout_fragment_metrics(engine, *child);
                ascent = std::max(ascent, metrics.baseline);
                descent = std::max(descent, std::max(0.f, metrics.height - metrics.baseline));
            }
            line_height = std::max(line_height, ascent + descent);
            if (ascent > 0.f) {
                for (int index = line_start_index; index < end; ++index) {
                    auto* child = parent_box->children[index];
                    if (child->kind != LAYOUT_BOX_TEXT) continue;
                    const auto metrics = layout_fragment_metrics(engine, *child);
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
        if (item.legacy_ignored) continue;
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
            const auto measured = engine_measure_text_transport(engine, token.text.c_str(), &item.style, 0.f, item.element);
            const auto metrics = measured.metrics;
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
            fragment->native_text = {measured.run,metrics.baseline};
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
    retain_unit_admission(output, element, style, !transparent_unit_inline(style));
    // Keep this fact before recursive flattening erases the inline boundaries.
    // Values other than a provable zero conservatively select the legacy path.
    const auto nonzero = [](aether_css_length value) { return value.value != 0.f; };
    if (nonzero(style.padding.left) || nonzero(style.padding.right) ||
        nonzero(style.border_width.left) || nonzero(style.border_width.right)) {
        auto marker = inline_item_s::text_item(element, style, "");
        marker.hanging_barrier = true;
        output.push_back(std::move(marker));
    }
    for (auto* node : element->children) {
        if (node->type == AETHER_NODE_TEXT) {
            collect_text_items(element, style, node->text, output, context);
        } else if (node->type == AETHER_NODE_ELEMENT) {
            auto child_style = get_style(styles, node);
            if (child_style.display == AETHER_DISPLAY_NONE) continue;
            const bool isolated = is_out_of_flow(child_style);
            if (isolated || child_style.display == AETHER_DISPLAY_LINE_BREAK)
                retain_unit_admission(output, node, child_style, !transparent_unit_inline(child_style));
            const bool block = child_style.display == AETHER_DISPLAY_BLOCK ||
                child_style.display == AETHER_DISPLAY_FLEX || child_style.display == AETHER_DISPLAY_TABLE;
            inline_capitalization_context_s independent;
            auto& child_context = (isolated || block) ? independent : context;
            if (child_style.display == AETHER_DISPLAY_LINE_BREAK) {
                output.push_back(inline_item_s::line_break(node, child_style));
                child_context.starts_word = true;
            } else if (initial_canvas_for(engine,node) || is_opaque_single_line_control(node) || child_style.display == AETHER_DISPLAY_INLINE_BLOCK) {
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
            if (!node->text.empty()) {
                // These bytes are exclusively presentation whitespace. Retain
                // the raw evidence without introducing a transformation step;
                // capitalization state follows the exact legacy update below.
                auto item = inline_item_s::text_item(parent, parent_style, node->text);
                item.legacy_ignored = true;
                output.push_back(std::move(item));
                context.starts_word = aether::presentation_space_byte(static_cast<unsigned char>(node->text.back())) != 0;
            }
            return inline_node_role_e::ignored;
        }
        collect_text_items(parent, parent_style, node->text, output, context);
        return inline_node_role_e::inline_content;
    }
    if (node->type != AETHER_NODE_ELEMENT) return inline_node_role_e::ignored;

    const auto child_style = get_style(styles, node);
    if (node->pseudo_element == aether_pseudo_element_t::none &&
        child_style.position == AETHER_POSITION_ABSOLUTE && has_legacy_inline_content(output) &&
        (child_style.display == AETHER_DISPLAY_BLOCK || child_style.display == AETHER_DISPLAY_FLEX ||
         child_style.display == AETHER_DISPLAY_TABLE)) {
        // The final flow collector emits a block-static anchor here. Its
        // measurement counterpart needs the same sequence-wide admission fact,
        // without installing a geometric anchor or consuming any line space.
        auto marker = inline_item_s::text_item(node, child_style, "");
        marker.hanging_barrier = true;
        output.push_back(std::move(marker));
    }
    if (child_style.display == AETHER_DISPLAY_NONE) return inline_node_role_e::ignored;
    if (is_out_of_flow(child_style)) {
        retain_unit_admission(output, node, child_style, true);
        return inline_node_role_e::ignored;
    }
    if (child_style.display == AETHER_DISPLAY_BLOCK || child_style.display == AETHER_DISPLAY_FLEX ||
        child_style.display == AETHER_DISPLAY_TABLE) {
        context.starts_word = true;
        return inline_node_role_e::block_content;
    }
    if (child_style.display == AETHER_DISPLAY_LINE_BREAK) {
        retain_unit_admission(output, node, child_style, !transparent_unit_inline(child_style));
        output.push_back(inline_item_s::line_break(node, child_style));
        context.starts_word = true;
    } else if (initial_canvas_for(engine,node) || is_opaque_single_line_control(node) || child_style.display == AETHER_DISPLAY_INLINE_BLOCK) {
        output.push_back(inline_item_s::atomic(node, child_style));
        context.starts_word = true;
    } else {
        collect_inline_element(engine, styles, node, child_style, output, context);
    }
    return inline_node_role_e::inline_content;
}

static intrinsic_widths_s measure_inline_items_intrinsic_widths(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    const std::vector<inline_item_s>& items,
    const BoxStyleSnapshot& parent_style, float maximum,
    aether_layout_result_s* result,
    intrinsic_width_request_s request)
{
    if (uses_source_unit_resolver(items, parent_style, true)) {
        const auto sizes = measure_source_intrinsic_sizes(engine, items, parent_style.white_space);
        return {sizes.min_content, sizes.max_content};
    }
    std::vector<aether_inline_intrinsic_fragment_s> minimum_fragments, maximum_fragments;
    const auto append = [&](aether_inline_fragment_kind_e kind, intrinsic_widths_s widths) {
        if (request.minimum) minimum_fragments.push_back({kind, widths.min_content});
        if (request.maximum) maximum_fragments.push_back({kind, widths.max_content});
    };
    for (const auto& item : items) {
        if (item.legacy_ignored) continue;
        if (item.is_line_break) {
            append(aether_inline_fragment_kind_e::forced_break, {});
            continue;
        }
        if (item.is_atomic) {
            append(aether_inline_fragment_kind_e::atomic,
                measure_child_natural_widths(engine, styles, item.element, maximum, result, request,
                    child_width_context_e::intrinsic_contribution));
            continue;
        }
        for (const auto& token : aether_tokenize_inline_text(item.text, item.style.white_space)) {
            if (token.kind == aether_inline_text_token_kind_e::forced_break) {
                append(aether_inline_fragment_kind_e::forced_break, {});
                continue;
            }
            const bool whitespace = token.kind != aether_inline_text_token_kind_e::word;
            const float width = engine_measure_text(
                engine, token.text.c_str(), &item.style, 0.f, item.element).width;
            append(whitespace ? aether_inline_fragment_kind_e::whitespace : aether_inline_fragment_kind_e::word,
                {width, width});
        }
    }
    intrinsic_widths_s widths;
    if (request.minimum) widths.min_content = aether_measure_inline_intrinsic_sizes(minimum_fragments).min_content;
    if (request.maximum) widths.max_content = aether_measure_inline_intrinsic_sizes(maximum_fragments).max_content;
    return widths;
}

/* ── MeasureIntrinsicWidth ─────────────────────────────────────────── */

intrinsic_widths_s measure_intrinsic_widths(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result,
    intrinsic_width_request_s request)
{
    const float decoration = aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    if(initial_canvas_for(engine,element)) { const auto size=canvas_size(engine,element,style,padding,border,maximum); return {size.width+decoration,size.width+decoration}; }
    if (is_opaque_single_line_control(element)) {
        const float preferred = std::min(maximum, 180.f + decoration);
        return {preferred, preferred};
    }
    if (style.display == AETHER_DISPLAY_FLEX) {
        return measure_intrinsic_flex_widths(engine, styles, element, style, padding, border, maximum, result, request);
    }
    if (style.display == AETHER_DISPLAY_TABLE) {
        const auto widths = measure_intrinsic_table_widths(engine, styles, element, maximum, result);
        return {widths.min_content + decoration, widths.max_content + decoration};
    }

    // Use the exact inline-item producer used by layout. Intrinsic sizing must
    // never choose a different DOM traversal from the visible formatter.
    intrinsic_widths_s content;
    const auto include = [&](intrinsic_widths_s child) {
        if (request.minimum) content.min_content = std::max(content.min_content, child.min_content);
        if (request.maximum) content.max_content = std::max(content.max_content, child.max_content);
    };
    std::vector<inline_item_s> inline_items;
    inline_capitalization_context_s context;
    const auto flush_inline = [&]() {
        if (inline_items.empty()) return;
        include(measure_inline_items_intrinsic_widths(engine, styles, inline_items, style, maximum, result, request));
        inline_items.clear();
    };

    if (!collect_anonymous_flex_text_items(element, style, inline_items)) {
        for (auto* child : element->children) {
            const auto role = append_inline_node(engine, styles, element, style, child, inline_items, context);
            if (role != inline_node_role_e::block_content) continue;
            flush_inline();
            include(measure_child_natural_widths(engine, styles, child, maximum, result, request,
                child_width_context_e::intrinsic_contribution));
        }
    }
    flush_inline();
    if (request.minimum) content.min_content += decoration;
    if (request.maximum) content.max_content += decoration;
    return content;
}

float resolve_intrinsic_keyword(aether::css::intrinsic_size_keyword keyword,
    const char* property, intrinsic_widths_s raw, float available, float margins,
    bool definite, intrinsic_keyword_phase_e phase) {
    using K = aether::css::intrinsic_size_keyword;
    if (keyword == K::none) return NAN;
    if (!std::isfinite(raw.min_content) || !std::isfinite(raw.max_content))
        throw std::overflow_error("Invalid intrinsic keyword inputs");
    if (keyword == K::min_content) return raw.min_content;
    if (keyword == K::max_content) return raw.max_content;
    if (phase == intrinsic_keyword_phase_e::min_contribution) return raw.min_content;
    if (phase == intrinsic_keyword_phase_e::max_contribution) return raw.max_content;
    if (!definite) {
        if (std::string_view(property) == "min-width") return raw.min_content;
        if (std::string_view(property) == "max-width") return raw.max_content;
        return NAN;
    }
    if (!std::isfinite(available) || !std::isfinite(margins))
        throw std::overflow_error("Invalid intrinsic keyword available width");
    return std::max(raw.min_content, std::min(raw.max_content, std::max(0.f, available - margins)));
}

float measure_keyword_border_width(const aether_layout_engine_s* engine,
    const StyleLookup* styles, aether_node_s* element, const BoxStyleSnapshot& style,
    const char* property, float available, bool definite, aether_layout_result_s* result,
    intrinsic_keyword_phase_e phase, float intrinsic_available_width) {
    const auto measures = layout_measures(styles, element);
    const auto keyword = measures ? measures->intrinsic_keyword(property) : aether::css::intrinsic_size_keyword::none;
    if (keyword == aether::css::intrinsic_size_keyword::none) return NAN;
    const bool cyclic = phase != intrinsic_keyword_phase_e::used;
    const auto padding = resolve_padding_edges(engine, style.padding, cyclic ? 0.f : available, style,
        cyclic ? padding_resolution_phase_e::intrinsic_cyclic : padding_resolution_phase_e::used);
    const auto border = resolve_border_edges(engine, style, available);
    const auto margin = resolve_margin_edges(engine, style.margin, cyclic ? 0.f : available, style);
    // measure_intrinsic_widths observes descendants, not this node's own sizing
    // keyword/constraints, so raw M/X never recursively resolve the same tag.
    const auto raw = measure_intrinsic_widths(engine, styles, element, style, padding, border, available, result, {true,true});
    // OOF fit-content uses the inset-modified available width, while percentages
    // in padding/margins and intrinsic measurements retain the containing block.
    const float fit_available = std::isfinite(intrinsic_available_width) ? intrinsic_available_width : available;
    return resolve_intrinsic_keyword(keyword, property, raw, fit_available, aether_thicknessf_horizontal(margin), definite, phase);
}

float measure_intrinsic_width(
    const aether_layout_engine_s* engine, const StyleLookup* styles,
    aether_node_s* element, const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border, float maximum,
    aether_layout_result_s* result, aether_intrinsic_size_mode_e mode)
{
    const bool minimum = mode == aether_intrinsic_size_mode_e::min_content;
    const auto widths = measure_intrinsic_widths(engine, styles, element, style, padding, border,
        maximum, result, {minimum, !minimum});
    return minimum ? widths.min_content : widths.max_content;
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
    if(initial_canvas_for(engine,element)) return canvas_size(engine,element,style,padding,border,available_width).height+aether_thicknessf_vertical(padding)+aether_thicknessf_vertical(border);
    const float content_width = std::max(0.f, available_width -
        aether_thicknessf_horizontal(padding) - aether_thicknessf_horizontal(border));
    if (is_opaque_single_line_control(element)) {
        return layout_atomic_control(engine, element, style, content_width, NAN, false, padding, border) +
            aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border);
    }

    const bool flex = style.display == AETHER_DISPLAY_FLEX;
    const auto writing = layout_measures(styles, element);
    const bool row = flex && flex_axis(style.flex_direction, false, writing ? writing->writing_mode() : 0).horizontal;
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
            content_height += (children.size() - 1) * layout_used_gap(styles, element, style, style.flex_direction == AETHER_FLEX_DIRECTION_COLUMN || style.flex_direction == AETHER_FLEX_DIRECTION_COLUMN_REVERSE, 0.f, false);
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
    const bool source_units = uses_source_unit_resolver(items, parent_style);
    const float line_minimum = std::max(1.f, layout_used_line_height(engine, owner, parent_style));
    if (source_units) {
        auto emit = [](const auto&, const inline_line_summary&, float, inline_line_end) {};
        return resolve_source_inline_lines(engine, items, parent_style.white_space,
            available_width, line_minimum, emit);
    }
    if (uses_hanging_line_resolver(items)) {
        auto atomic = [&](size_t index, float) {
            const auto& item = items[index];
            const auto style = get_style(styles, item.element);
            const auto padding = resolve_padding_edges(engine, style.padding, available_width, style);
            const auto border = resolve_border_edges(engine, style, available_width);
            const float width = measure_child_natural_width(engine, styles, item.element,
                available_width, result, aether_intrinsic_size_mode_e::max_content,
                child_width_context_e::inline_height);
            const float height = measure_intrinsic_height(engine, styles, result, item.element,
                style, padding, border, available_width);
            return measured_inline_fragment{index, {}, {width, height, 0.f, false, false, style.white_space}, nullptr, {}};
        };
        auto emit = [](const auto&, const inline_line_summary&, float, inline_line_end) {};
        return resolve_hanging_inline_lines(engine, items, available_width, line_minimum, atomic, emit);
    }
    float line_height = line_minimum;
    float line_ascent = 0.f, line_descent = 0.f;
    float cursor_x = 0.f;
    float total_height = 0.f;
    bool has_line_content = false;
    bool previous_collapsible_space = false;

    auto finish_line = [&](bool forced) {
        if (has_line_content || forced)
            total_height += std::max(line_height, line_ascent + line_descent);
        cursor_x = 0.f;
        line_height = line_minimum;
        line_ascent = line_descent = 0.f;
        has_line_content = false;
    };
    auto append_fragment = [&](float width, float height, bool whitespace,
                               bool discard_at_line_start, bool allow_wrap) {
        // Preserved spaces contribute width and metrics even at line start,
        // but retain their whitespace classification for soft wrapping.
        if (discard_at_line_start && !has_line_content) return false;
        if (allow_wrap && !whitespace && has_line_content && cursor_x + width > available_width) finish_line(false);
        cursor_x += width;
        line_height = std::max(line_height, height);
        has_line_content = true;
        return true;
    };

    for (const auto& item : items) {
        if (item.legacy_ignored) continue;
        if (item.is_line_break) {
            finish_line(true);
            previous_collapsible_space = false;
            continue;
        }
        if (item.is_atomic) {
            const auto style = get_style(styles, item.element);
            const auto padding = resolve_padding_edges(engine, style.padding, available_width, style);
            const auto border = resolve_border_edges(engine, style, available_width);
            const float width = measure_child_natural_width(
                engine, styles, item.element, available_width, result, aether_intrinsic_size_mode_e::max_content,
                child_width_context_e::inline_height);
            const float height = measure_intrinsic_height(
                engine, styles, result, item.element, style, padding, border, available_width);
            append_fragment(width, height, false, false, true);
            previous_collapsible_space = false;
            continue;
        }
        for (const auto& token : aether_tokenize_inline_text(item.text, item.style.white_space)) {
            if (token.kind == aether_inline_text_token_kind_e::forced_break) {
                finish_line(true);
                previous_collapsible_space = false;
                continue;
            }
            const bool collapsible = token.kind == aether_inline_text_token_kind_e::collapsible_space;
            // Carry collapse state across runs, just as final inline layout does.
            // Skip before measuring: a redundant space must not affect metrics.
            if (collapsible && previous_collapsible_space) continue;
            const bool whitespace = token.kind != aether_inline_text_token_kind_e::word;
            const auto measured = engine_measure_text_transport(engine, token.text.c_str(), &item.style, 0.f, item.element);
            const auto metrics = measured.metrics;
            // Match final inline layout: forced breaks remain unconditional, but
            // soft wrapping belongs to the text run rather than its parent.
            if (append_fragment(metrics.width, metrics.height, whitespace, collapsible,
                                aether_white_space_allows_soft_wrap(item.style.white_space))) {
                // Accumulate only accepted text on its post-wrap line, using the
                // same baseline envelope as final layout without remeasurement.
                line_ascent = std::max(line_ascent, metrics.baseline);
                line_descent = std::max(line_descent, std::max(0.f, metrics.height - metrics.baseline));
            }
            if (has_line_content) previous_collapsible_space = collapsible;
        }
    }
    finish_line(false);
    return total_height;
}

static float checked_intrinsic_width(float value) {
    if (!std::isfinite(value))
        throw std::overflow_error("Non-finite intrinsic width contribution");
    return value;
}

struct intrinsic_child_width_s {
    intrinsic_widths_s border_box;
    float margins;
    // Reuse an already measured raw minimum. A specified width does not provide
    // this suggestion, even if its generic contribution happens to be smaller.
    float raw_min_content = NAN;
    aether_thicknessf padding;
    aether_thicknessf border;
};

static intrinsic_child_width_s measure_child_intrinsic_width_parts(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* child, float maximum,
    aether_layout_result_s* result,
    intrinsic_width_request_s request,
    child_width_context_e context)
{
    auto child_style = get_style(styles, child);
    const bool table_box = child_style.display == AETHER_DISPLAY_TABLE ||
        child_style.display == AETHER_DISPLAY_TABLE_ROW ||
        child_style.display == AETHER_DISPLAY_TABLE_CELL;
    // A descendant's percentage margins/padding are cyclic while calculating
    // its intrinsic contribution. Resolve against zero, preserving absolute
    // calc terms and signed margins. Keep maximum for all other measurements.
    // Table boxes retain their existing local policy, not their descendants'.
    const float edge_reference = context == child_width_context_e::intrinsic_contribution && !table_box
        ? 0.f : maximum;
    auto child_margin = resolve_margin_edges(engine, child_style.margin, edge_reference, child_style);
    const auto measures = layout_measures(styles, child);
    // A percentage width is indefinite while this intrinsic contribution is
    // calculated. CSS Sizing therefore treats it as auto at this stage.
    const bool percentage_dependent_width =
        layout_measure_has_percentage(measures, "width", child_style.width);
    const auto keyword = [&](const char* name) { return measures ? measures->intrinsic_keyword(name) : aether::css::intrinsic_size_keyword::none; };
    const bool intrinsic_keyword = keyword("width") != aether::css::intrinsic_size_keyword::none || keyword("min-width") != aether::css::intrinsic_size_keyword::none || keyword("max-width") != aether::css::intrinsic_size_keyword::none;
    const float specified = percentage_dependent_width || keyword("width") != aether::css::intrinsic_size_keyword::none
        ? NAN
        : resolve(engine, child_style.width, maximum, child_style);
    auto child_padding = resolve_padding_edges(engine, child_style.padding, edge_reference, child_style,
        context == child_width_context_e::intrinsic_contribution && !table_box
            ? padding_resolution_phase_e::intrinsic_cyclic : padding_resolution_phase_e::used);
    auto child_border  = resolve_border_edges(engine, child_style, maximum);
    const float decorations = aether_thicknessf_horizontal(child_padding) +
                              aether_thicknessf_horizontal(child_border);
    const auto checked = checked_intrinsic_width;
    checked(decorations);

    // A constraint keyword needs independent raw sizes even with a definite
    // width. Never derive M/X from the already constrained contribution.
    intrinsic_widths_s raw;
    if (intrinsic_keyword) raw = measure_intrinsic_widths(engine, styles, child, child_style,
        child_padding, child_border, maximum, result, {true,true});
    const bool used_phase = context == child_width_context_e::inline_height;
    const auto phase_for = [&](bool minimum) { return used_phase ? intrinsic_keyword_phase_e::used : minimum ? intrinsic_keyword_phase_e::min_contribution : intrinsic_keyword_phase_e::max_contribution; };
    intrinsic_widths_s widths;
    float raw_min_content = NAN;
    if (std::isfinite(specified)) {
        const float width = (child_style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? specified + aether_thicknessf_horizontal(child_padding) + aether_thicknessf_horizontal(child_border)
            : specified;
        widths = {width, width};
    } else {
        widths = intrinsic_keyword ? raw : measure_intrinsic_widths(engine, styles, child, child_style,
                                         child_padding, child_border, maximum, result, request);
        if (keyword("width") != aether::css::intrinsic_size_keyword::none) {
            if (request.minimum) widths.min_content = resolve_intrinsic_keyword(keyword("width"), "width", raw, maximum, aether_thicknessf_horizontal(child_margin), used_phase, phase_for(true));
            if (request.maximum) widths.max_content = resolve_intrinsic_keyword(keyword("width"), "width", raw, maximum, aether_thicknessf_horizontal(child_margin), used_phase, phase_for(false));
        }
        if (request.minimum) raw_min_content = widths.min_content;
    }
    // Validate before clamping: a finite max must not disguise overflow in the
    // preferred/intrinsic size or its conversion to a border-box contribution.
    if (request.minimum) checked(widths.min_content);
    if (request.maximum) checked(widths.max_content);
    if (!table_box) {
        // This is a descendant's contribution, not the raw intrinsic size of
        // the flex root. Table boxes retain their existing sizing policy; the
        // exclusion is local, after recursively measuring their descendants.
        // Ordinary inline descendants are flattened by the inline collector;
        // inline elements reaching here as flex items are blockified for sizing.
        const auto border_limit = [&](float value) {
            return checked(value + (child_style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX
                ? decorations : 0.f));
        };
        // Intrinsic percentage bases are indefinite. A percentage maximum
        // remains unbounded, including calc(0% + Xpx) recorded in the metadata.
        // A percentage minimum uses zero, retaining calc's absolute component.
        const float max_width = keyword("max-width") != aether::css::intrinsic_size_keyword::none ||
            layout_measure_has_percentage(measures, "max-width", child_style.max_width)
            ? NAN : resolve(engine, child_style.max_width, 0.f, child_style);
        const float min_width = keyword("min-width") != aether::css::intrinsic_size_keyword::none
            ? NAN : resolve(engine, child_style.min_width, 0.f, child_style);
        const float max_border = std::isfinite(max_width) ? border_limit(max_width) : NAN;
        const float min_border = std::isfinite(min_width) ? border_limit(min_width) : NAN;
        const auto clamp = [&](float width, intrinsic_keyword_phase_e phase) {
            const float upper = keyword("max-width") != aether::css::intrinsic_size_keyword::none
                ? resolve_intrinsic_keyword(keyword("max-width"), "max-width", raw, maximum, aether_thicknessf_horizontal(child_margin), used_phase, phase) : max_border;
            const float lower = keyword("min-width") != aether::css::intrinsic_size_keyword::none
                ? resolve_intrinsic_keyword(keyword("min-width"), "min-width", raw, maximum, aether_thicknessf_horizontal(child_margin), used_phase, phase) : min_border;
            if (std::isfinite(upper)) width = std::min(width, upper);
            if (std::isfinite(lower)) width = std::max(width, lower);
            return std::max(decorations, width);
        };
        if (request.minimum) widths.min_content = clamp(widths.min_content, phase_for(true));
        if (request.maximum) widths.max_content = clamp(widths.max_content, phase_for(false));
    }
    return {widths, checked(aether_thicknessf_horizontal(child_margin)), raw_min_content,
            child_padding, child_border};
}

intrinsic_widths_s measure_child_natural_widths(
    const aether_layout_engine_s* engine, const StyleLookup* styles,
    aether_node_s* child, float maximum, aether_layout_result_s* result,
    intrinsic_width_request_s request, child_width_context_e context) {
    const auto natural = measure_child_intrinsic_width_parts(
        engine, styles, child, maximum, result, request, context);
    intrinsic_widths_s widths;
    // Signed margins contribute once, outside the size constraints.
    if (request.minimum) widths.min_content = checked_intrinsic_width(natural.border_box.min_content + natural.margins);
    if (request.maximum) widths.max_content = checked_intrinsic_width(natural.border_box.max_content + natural.margins);
    return widths;
}

float measure_child_natural_width(
    const aether_layout_engine_s* engine, const StyleLookup* styles,
    aether_node_s* child, float maximum, aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode, child_width_context_e context) {
    const bool minimum = mode == aether_intrinsic_size_mode_e::min_content;
    const auto widths = measure_child_natural_widths(engine, styles, child, maximum,
        result, {minimum, !minimum}, context);
    return minimum ? widths.min_content : widths.max_content;
}

static float select_intrinsic_flex_item_width(
    const aether_layout_engine_s* engine, const StyleLookup* styles,
    aether_node_s* child, float maximum, aether_layout_result_s* result,
    float contribution, intrinsic_child_width_s& natural, intrinsic_keyword_phase_e phase) {
    const auto child_style = get_style(styles, child);
    const bool table_box = child_style.display == AETHER_DISPLAY_TABLE ||
        child_style.display == AETHER_DISPLAY_TABLE_ROW || child_style.display == AETHER_DISPLAY_TABLE_CELL;
    // Tables retain their individual natural contribution. Signed margins are
    // added by the caller exactly once, after C/H selection.
    if (table_box) return contribution;

    const auto measures = layout_measures(styles, child);
    aether_css_length basis_length;
    const char* basis_property;
    if (child_style.flex_basis.kind == AETHER_FLEX_BASIS_LENGTH) {
        basis_length = child_style.flex_basis.length;
        basis_property = "flex-basis";
    } else if (child_style.flex_basis.kind == AETHER_FLEX_BASIS_AUTO) {
        basis_length = child_style.width;
        basis_property = "width";
    } else {
        return contribution;
    }
    const auto keyword = [&](const char* property) { return measures ? measures->intrinsic_keyword(property) : aether::css::intrinsic_size_keyword::none; };
    const bool keyword_basis = keyword(basis_property) != aether::css::intrinsic_size_keyword::none;
    // A probing maximum never makes a percentage basis definite, even when
    // calc simplification removed a zero-percent term from the scalar value.
    if ((!keyword_basis && basis_length.unit == AETHER_CSS_UNIT_AUTO) ||
        layout_measure_has_percentage(measures, basis_property, basis_length))
        return contribution;

    // Reuse the exact edges used by C: percentage padding is cyclic, while
    // borders retain the existing natural-contribution resolution policy.
    const auto padding = natural.padding;
    const auto border = natural.border;
    const float decoration = checked_intrinsic_width(
        aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border));
    const auto border_size = [&](aether_css_length length, const char* property = nullptr) {
        if (property && keyword(property) != aether::css::intrinsic_size_keyword::none)
            return measure_keyword_border_width(engine, styles, child, child_style, property, maximum, false, result, phase);
        float used;
        const auto tree = measures && property ? measures->contextual_tree(property) : aether::css::math_tree{};
        if (tree) {
            double value = 0.;
            if (!aether::css::resolve_contextual_math(tree, 0., value) ||
                !std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                throw std::overflow_error("Invalid intrinsic contextual flex basis");
            used = static_cast<float>(value);
        } else used = resolve(engine, length, 0.f, child_style);
        if (length.unit == AETHER_CSS_UNIT_AUTO) return NAN;
        return checked_intrinsic_width(std::max(0.f, used) +
            (child_style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX ? decoration : 0.f));
    };
    const float base = border_size(basis_length, basis_property);
    if (!aether_intrinsic_flex_needs_hypothetical(contribution, base, true,
            child_style.flex_grow, child_style.flex_shrink))
        return contribution;

    // Only a selected replacement needs H or a raw min-content walk. Do not
    // measure the generic contribution again: its min/max clamps are not the
    // automatic minimum's content-size suggestion.
    aether_flex_item_input_s item{};
    item.main_decoration_size = decoration;
    item.min_main_size = border_size(child_style.min_width, "min-width");
    item.max_main_size = layout_measure_has_percentage(measures, "max-width", child_style.max_width)
        ? NAN : border_size(child_style.max_width, "max-width");
    if (child_style.min_width.unit == AETHER_CSS_UNIT_AUTO && keyword("min-width") == aether::css::intrinsic_size_keyword::none) {
        item.automatic_minimum.is_scroll_container =
            flex_automatic_minimum_is_zero_for_overflow(child_style.overflow_x);
        if (!item.automatic_minimum.is_scroll_container) {
            if (!std::isfinite(natural.raw_min_content)) {
                natural.raw_min_content = checked_intrinsic_width(measure_intrinsic_width(
                    engine, styles, child, child_style, padding, border, maximum, result,
                    aether_intrinsic_size_mode_e::min_content));
            }
            item.automatic_minimum.content_size_suggestion = natural.raw_min_content;
            item.automatic_minimum.specified_size_suggestion =
                layout_measure_has_percentage(measures, "width", child_style.width)
                    ? NAN : border_size(child_style.width, "width");
            item.automatic_minimum.max_main_size = item.max_main_size;
        }
    }
    return aether_flex_resolve_hypothetical_size(base, item);
}

static intrinsic_widths_s measure_intrinsic_flex_widths(
    const aether_layout_engine_s* engine, const StyleLookup* styles,
    aether_node_s* element, const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border, float maximum,
    aether_layout_result_s* result, intrinsic_width_request_s request)
{
    const auto writing = layout_measures(styles, element);
    const bool column = !flex_axis(style.flex_direction, false, writing ? writing->writing_mode() : 0).horizontal;
    const bool wrap_row = !column && style.flex_wrap;
    // A wrapping maximum has an independent, natural min-content floor. Never
    // derive that floor from H, or run a second recursive walk at each level.
    if (wrap_row && request.maximum) request.minimum = true;
    const auto& children = collect_flex_items(styles, result, element, style);
    std::vector<float> minimums, maximums;
    if (request.minimum) minimums.reserve(children.size());
    if (request.maximum) maximums.reserve(children.size());
    for (auto* child : children) {
        auto natural = measure_child_intrinsic_width_parts(engine, styles, child, maximum,
            result, request, child_width_context_e::intrinsic_contribution);
        if (request.minimum) {
            const float width = !column && !wrap_row
                ? select_intrinsic_flex_item_width(engine, styles, child, maximum, result,
                    natural.border_box.min_content, natural, intrinsic_keyword_phase_e::min_contribution)
                : natural.border_box.min_content;
            minimums.push_back(checked_intrinsic_width(width + natural.margins));
        }
        if (request.maximum) {
            const float width = !column
                ? select_intrinsic_flex_item_width(engine, styles, child, maximum, result,
                    natural.border_box.max_content, natural, intrinsic_keyword_phase_e::max_contribution)
                : natural.border_box.max_content;
            maximums.push_back(checked_intrinsic_width(width + natural.margins));
        }
    }
    intrinsic_widths_s widths;
    const float gap = layout_used_gap(styles, element, style, style.flex_direction == AETHER_FLEX_DIRECTION_COLUMN || style.flex_direction == AETHER_FLEX_DIRECTION_COLUMN_REVERSE, 0.f, false);
    if (request.minimum) widths.min_content = aether_intrinsic_flex_main_contribution(
        minimums, column, style.flex_wrap, gap, aether_intrinsic_size_mode_e::min_content);
    if (request.maximum) widths.max_content = aether_intrinsic_flex_main_contribution(
        maximums, column, style.flex_wrap, gap, aether_intrinsic_size_mode_e::max_content,
        wrap_row ? widths.min_content : 0.f);
    // Decoration belongs to the root once, after the content minimum floor.
    // Validate before a caller's finite max-width could disguise overflow.
    const float decoration = checked_intrinsic_width(
        aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border));
    if (request.minimum) widths.min_content = checked_intrinsic_width(widths.min_content + decoration);
    if (request.maximum) widths.max_content = checked_intrinsic_width(widths.max_content + decoration);
    return widths;
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

static std::unordered_set<aether_node_s*> effective_fixed_containers(
    const StyleLookup* styles, const std::unordered_set<aether_node_s*>& active) {
    std::unordered_set<aether_node_s*> effective;
    const auto version = styles ? styles->selected_version() : aether::style::MapVersionRef{};
    if (version) for (const auto& entry : version->backing()->entries()) {
        const auto& record = entry.second;
        auto* node = record->node();
        const auto& identity = record->node_lifetime();
        if (identity && (identity->owner != node || !identity->alive || !identity->attached)) continue;
        const auto style = record->style_view();
        if (node && node->type == AETHER_NODE_ELEMENT && !node->anonymous_flex_owner &&
            (!style.transforms().empty() || !style.backdrop_filters().empty() || active.count(node)))
            effective.insert(node);
    }
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
    try {
        auto active = aether_active_transform_owners(runtime);
        const StyleLookup lookup(aether::style::pin_style_map(current_styles), nullptr, {});
        const auto effective = effective_fixed_containers(&lookup, active);
        *out_changed = !previous_layout || !previous_layout->effective_context_enabled ||
            effective != previous_layout->effective_fixed_container_nodes;
        engine->prepared_active_nodes = std::move(active);
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
    validate_contents_wrappers(styles);
    prepare_initial_canvas(engine,doc);
    if (version && version->contextual()) {
        for (const auto& entry : version->backing()->entries()) {
            if (!entry.second->measures()) continue;
            const auto& document = entry.second->measures()->document().values();
            if (document.viewport_width != viewport_width || document.viewport_height != viewport_height)
                throw std::runtime_error("Layout viewport differs from style document");
            break;
        }
    }
    const auto freeze_assets = [&]() {
        auto assets = std::make_shared<const aether::layout::LayoutAssets>(version, result->build_overlay->freeze(), defaults, document_identity, document_structure_epoch);
        result->assets = std::move(assets);
        result->build_overlay.reset();
    };
    result->effective_context_enabled = prepared || (version && version->contextual());
    if (result->effective_context_enabled) {
        try { result->effective_fixed_container_nodes = effective_fixed_containers(styles, active); }
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
static bool input_record_alive(const aether::style::FrozenNodeRecord* record, const aether_node_s* node) {
    if (!record || !record->node_lifetime()) return true;
    const auto& identity = record->node_lifetime();
    return identity->owner == node && identity->alive && identity->attached;
}
static aether_scroll_offset_t checked_input_offsets(aether_node_t node, void* data) {
    auto& context = *static_cast<input_callback_context_s*>(data);
    // Historical input guards use base provenance, even for an external paint override.
    const auto& base = context.lookup.base_version();
    const auto record = base ? base->backing()->find_owner(node) : aether::style::FrozenNodeRef{};
    if (!input_record_alive(record.get(), node)) throw std::invalid_argument("retired input node");
    const auto offset = context.callback(node, context.data);
    if (!context.valid() || !input_record_alive(record.get(), node))
        throw std::invalid_argument("input callback changed document");
    return offset;
}
static bool live_input_owner(const StyleLookup& lookup, aether_node_t node) {
    if (!node) return true;
    const auto& base = lookup.base_version();
    if (!base || !base->contextual()) return true;
    const auto* record = base->backing()->find(node);
    return record && record->node_lifetime() && input_record_alive(record, node);
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
