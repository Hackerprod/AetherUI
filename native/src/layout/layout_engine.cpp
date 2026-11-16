#include "document_word_fit.h"
#include "document_attempt_layout.h"
#include "initial_canvas.h"
/* ── Layout Engine — core box layout, flow, inline, and intrinsic sizing ─ */
#include "layout_internal.h"
#include "native_text_transaction.h"
#include "real_simple_text_policy.h"
#include "selected_layout_capture.h"
#include "document_text_context.h"
#include "document_root_layout.h"
using aether::layout::DocumentTextAccess;
using aether::layout::RealSimpleTextAccess;
using aether::layout::NativeTextAccess;
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
    if(engine->text_selection() && engine->text_selection()->mode==aether::rendering::text::MeasurementMode::native)
        throw std::invalid_argument("opaque controls outside native text contract");
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

static void place_normal_flow_block(aether_layout_box_s* box,float cursor_y,float collapsed_before) {
    const float border_y = cursor_y + collapsed_before;
    offset_box(box, 0, border_y - normal_flow_border_box(box).y);
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

static void admit_document_atomic_style(const aether_layout_engine_s*,aether_node_s*,const BoxStyleSnapshot&);
static document_atomic_widths_s document_auto_atomic_border_width(const aether_layout_engine_s*,const StyleLookup*,aether_node_s*,const BoxStyleSnapshot&,double);

static aether_layout_box_s* layout_element_body(
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
    if(DocumentTextAccess::drawing_mode(engine)&&
       (style.display==AETHER_DISPLAY_TABLE||style.display==AETHER_DISPLAY_TABLE_ROW||
        style.display==AETHER_DISPLAY_TABLE_CELL||
        style.position!=AETHER_POSITION_STATIC))
        throw std::invalid_argument("DOCUMENT_DRAWING_UNTRACKED_PATH");
    if(DocumentTextAccess::drawing_mode(engine)&&style.display==AETHER_DISPLAY_INLINE_BLOCK)admit_document_atomic_style(engine,element,style);
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
    std::optional<document_atomic_widths_s> document_atomic_widths;

    if (box->is_initial_canvas && !is_keyword("width")) {
        border_width = canvas.width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    } else if (has_forced_border_width) {
        border_width = forced_border_width;
    } else if (DocumentTextAccess::drawing_mode(engine)&&style.display==AETHER_DISPLAY_INLINE_BLOCK&&style.width.unit==AETHER_CSS_UNIT_AUTO) {
        document_atomic_widths=document_auto_atomic_border_width(engine,styles,element,style,containing_block.width);
        border_width=static_cast<float>(document_atomic_widths->border);
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
                                              style, result,document_atomic_widths?std::optional<double>(document_atomic_widths->content):std::nullopt);
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
    // Store only after child callbacks/completion. Logical capacity stayed on
    // this stack throughout fitting; this retained value serves outer atomic fit.
    box->document_atomic_widths=document_atomic_widths;
    return box;
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
 if(!DocumentTextAccess::drawing_mode(engine))return layout_element_body(engine, styles, element, containing_block, containing_height, normal_flow_y, force_available_width, forced_border_width, has_forced_border_width, forced_border_height, forced_height_kind, is_independent_layout_item, result, viewport_overflow_override, deferred_completion, intrinsic_measurement, intrinsic_available_width);
 auto* mutable_engine=const_cast<aether_layout_engine_s*>(engine);
 auto& state=DocumentTextAccess::drawing_attempts(engine);
 aether::layout::DocumentAttemptLayout::Frame attempt(state,mutable_engine);
 try{
  auto* box=layout_element_body(engine, styles, element, containing_block, containing_height, normal_flow_y, force_available_width, forced_border_width, has_forced_border_width, forced_border_height, forced_height_kind, is_independent_layout_item, result, viewport_overflow_override, deferred_completion, intrinsic_measurement, intrinsic_available_width);
  if(box->document_has_last_line)box->document_last_line_baseline-=double(box->content_box.y);
  box->document_layout_attempt=state.active_id(mutable_engine);
  attempt.close(box);return box;
 }catch(...){DocumentTextAccess::capture_failure(mutable_engine);throw;}
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
    marker.marker_kind=inline_item_s::MarkerKind::admission_only;
    marker.unit_barrier = barrier;
    output.push_back(std::move(marker));
}

static void collect_document_source(const aether_layout_engine_s* engine,aether_node_s* owner,
    aether_node_s* node,const BoxStyleSnapshot& style,std::vector<inline_item_s>& output) {
    auto source=DocumentTextAccess::source(engine,owner,node,style);
    auto item=inline_item_s::text_item(owner,style,source->decoded);
    item.is_line_break=source->kind==aether::layout::DocumentTextSource::Kind::line_break;
    item.document_source=std::move(source);output.push_back(std::move(item));
}

// One side-effect-free admission decision on the entire IFC, including empty
// inline ancestors. There is no prefix admission or fallback after measuring.
// ASCII excludes bidi/shaping/Unicode line-breaking from this initial contract.
static bool uses_source_unit_resolver(const aether_layout_engine_s* engine,const std::vector<inline_item_s>& items,
                                     const BoxStyleSnapshot& parent_style,
                                     bool intrinsic_width = false) {
    const auto mode = parent_style.white_space;
    if(DocumentTextAccess::active(engine)) {
        if(mode!=AETHER_WHITE_SPACE_NORMAL&&mode!=AETHER_WHITE_SPACE_NOWRAP&&mode!=AETHER_WHITE_SPACE_PRE&&!(DocumentTextAccess::drawing_mode(engine)&&mode==AETHER_WHITE_SPACE_PRE_WRAP))
            throw std::invalid_argument("DOCUMENT_TEXT_WHITE_SPACE_UNSUPPORTED");
        for(const auto& item:items){DocumentTextAccess::check(engine);
            if(item.unit_barrier||item.hanging_barrier||(item.is_atomic&&!DocumentTextAccess::drawing_mode(engine))||item.block_anchor_index!=static_cast<size_t>(-1)||(!item.is_atomic&&item.style.white_space!=mode))
                throw std::invalid_argument("DOCUMENT_TEXT_IFC_UNSUPPORTED");
        }
        return true;
    }
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
    const aether_layout_engine_s* engine,aether_node_s* element,
    const BoxStyleSnapshot& style,
    std::vector<inline_item_s>& output)
{
    if (!element->anonymous_flex_owner) return false;
    if(DocumentTextAccess::active(engine)){
        for(auto* child:element->children)collect_document_source(engine,element,child,style,output);
        return true;
    }
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
    aether_layout_result_s* result,std::optional<double> document_logical_width)
{
    if (RealSimpleTextAccess::owns(engine,parent)) {
        const auto measured=RealSimpleTextAccess::measure(engine,parent,parent_style);
        auto* fragment=result->create_box();
        fragment->kind=LAYOUT_BOX_TEXT; fragment->element=parent; fragment->style=parent_style;
        fragment->text=measured.run ? measured.run->source : std::string{};
        fragment->native_text={measured.run,measured.metrics.baseline};
        fragment->content_box={content.x,content.y,measured.metrics.width,measured.metrics.height};
        fragment->padding_box=fragment->border_box=fragment->margin_box=fragment->content_box;
        parent_box->add_child(fragment);
        if(result->records_boxes) result->paint_order.push_back(fragment);
        return measured.metrics.height;
    }
    if (NativeTextAccess::owns(engine,parent)) {
        const auto measured=NativeTextAccess::measure(engine,parent,parent_style);
        if(!measured.run) return 0.f;
        auto* fragment=result->create_box();
        fragment->kind=LAYOUT_BOX_TEXT; fragment->element=parent;
        fragment->style=parent_style; fragment->text=measured.run->source;
        fragment->native_text={measured.run,measured.metrics.baseline};
        fragment->content_box={content.x,content.y,measured.metrics.width,measured.metrics.height};
        fragment->padding_box=fragment->border_box=fragment->margin_box=fragment->content_box;
        parent_box->add_child(fragment);
        if(result->records_boxes) result->paint_order.push_back(fragment);
        return std::max(measured.metrics.height,layout_used_line_height(engine,parent,parent_style));
    }
    float cursor_y = content.y;
    std::vector<inline_item_s> inline_items;
    inline_capitalization_context_s context;
    if (collect_anonymous_flex_text_items(engine,parent, parent_style, inline_items)) {
        return layout_inline_items(engine, styles, parent_box, inline_items,
            content.x, content.y, content.width, content.height, containing_height, parent_style, result,document_logical_width);
    }
    bool has_pending_margin = false;
    float pending_margin = 0.f;
    aether_node_s* cursor_owner = nullptr;

    auto flush_inline = [&]() {
        if (!has_legacy_inline_content(inline_items) &&
            (!DocumentTextAccess::drawing_mode(engine) || inline_items.empty())) { inline_items.clear(); return; }
        // An eligible all-collapsed sequence has no line at all. In particular,
        // nested whitespace must not consume the margin separating two blocks.
        if (!DocumentTextAccess::active(engine) && uses_source_unit_resolver(engine,inline_items, parent_style) &&
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
                                               content.height, containing_height, parent_style, result,document_logical_width);
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
            if(DocumentTextAccess::drawing_mode(engine)&&parent_style.display==AETHER_DISPLAY_INLINE_BLOCK)throw std::invalid_argument("DOCUMENT_ATOMIC_BLOCK_CONTENT");
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
                place_normal_flow_block(child_box, cursor_y, collapsed_before);
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
    std::vector<std::shared_ptr<const aether::layout::DocumentTextSource>> sources;
    std::vector<aether::layout::DocumentTextRange> provenance;
    std::shared_ptr<const aether::layout::DocumentTextFragment> authenticated;
};
enum class inline_source_kind { word, space, forced, atomic };
struct inline_source_unit {
    inline_source_kind kind;
    std::vector<inline_source_fragment> fragments;
    std::size_t atomic_item=static_cast<std::size_t>(-1);
};

// Candidate extension of the existing source-unit collector. It emits the same
// units consumed by the existing intrinsic and line resolvers, not a new breaker.
namespace aether::layout {
class DocumentTextNormalizer final {
 using Source=std::shared_ptr<const DocumentTextSource>;
 struct Pending {Source source;DocumentTextRange range;};
 struct PassGuard {const aether_layout_engine_s* engine;bool complete=false;~PassGuard(){if(!complete)DocumentTextAccess::abort_pass(engine);}};
public:
 struct AtomicStamp {
  const aether_layout_box_s* key{};
  std::shared_ptr<node_identity_s> element_token,presentation_token,visual_token;
  aether_node_s* element{};
  aether_node_s* presentation_owner{};
  layout_box_kind_t kind{};
  BoxStyleSnapshot style{};
  std::vector<aether_layout_box_s*> children{};
  std::string text{};
  std::shared_ptr<const DocumentTextSource> document_source{};
  aether::rendering::text::Placement native_text{};
  std::shared_ptr<const DocumentTextInterval> document_interval;
  std::shared_ptr<const DocumentConstructionInterval> document_construction;
  std::optional<document_atomic_widths_s> document_atomic_widths;
  bool document_has_last_line{};
  double document_last_line_baseline{};
  std::uint64_t document_layout_attempt{};
  std::shared_ptr<const DocumentAtomicInlineEvent> document_atomic_inline;
  bool is_atomic_control{};
  bool is_initial_canvas{};
  std::string control_display_text{};
  bool control_shows_placeholder{};
  aether_scroll_metrics_t scroll{};
  bool visual_sample_valid{};
  bool visual_has_transform{};
  aether_transform_matrix visual_transform{};
  float visual_opacity{};
  int64_t visual_timestamp{};
  float collapsed_margin_before{};
  float collapsed_margin_after{};
  float own_relative_dx{};
  float own_relative_dy{};
  aether_thicknessf used_border{};
  aether_rectf margin_box{};
  aether_rectf border_box{};
  aether_rectf padding_box{};
  aether_rectf content_box{};
  layout_scroll_proxy_s scroll_proxy{};
  float scroll_flow_height{};
  float scroll_base_height{};
  uint8_t scroll_completion_flags{};
  std::uint64_t birth{};
  const aether_layout_box_s* primary{};
 };
 struct DrawingAtom {
  aether_layout_box_s* box{};
  std::vector<AtomicStamp> stamps;
  std::vector<aether_layout_box_s*> paint;
  double width{},height{},baseline{};
  std::shared_ptr<const DocumentAtomicInlineEvent> event;
 };
 static void authenticate_atomic_capture(const aether_layout_engine_s* engine,aether_layout_result_s* layout){
  auto& backend=DocumentTextAccess::drawing_backend(engine);
  (void)DocumentTextAccess::capture_construction_geometry(engine,layout,&backend);
 }
 static std::pair<float,float> physical_strut(const aether_layout_engine_s* engine,aether_node_s* owner,const BoxStyleSnapshot& style){return DocumentTextAccess::validate_admission_marker(engine,owner,style,false);}
 static double atomic_width(const aether_layout_engine_s* engine,const inline_item_s& item){
  if(!item.is_atomic||item.style.display!=AETHER_DISPLAY_INLINE_BLOCK||item.document_source||!item.text.empty()||item.is_line_break)throw std::invalid_argument("DOCUMENT_ATOMIC_KIND");
  (void)physical_strut(engine,item.element,item.style);const auto& s=item.style;
  const auto px=[](aether_css_length v){if(v.unit!=AETHER_CSS_UNIT_PX||!std::isfinite(v.value)||v.value<0||v.value>65536||v.linear_percent||v.linear_em||v.linear_rem||v.linear_vw||v.linear_vh)throw std::invalid_argument("DOCUMENT_ATOMIC_PX");return double(v.value);};
  if(s.position!=AETHER_POSITION_STATIC||s.overflow_x!=AETHER_OVERFLOW_VISIBLE||s.overflow_y!=AETHER_OVERFLOW_VISIBLE)throw std::invalid_argument("DOCUMENT_ATOMIC_OVERFLOW");
  const aether::style::MeasuresReadView m(*s.measures_owner());
  if(m.direction_rtl()||m.writing_mode()!=0||!s.measures_owner()->intrinsic_keywords().empty()||!s.measures_owner()->contextual_lengths().empty())throw std::invalid_argument("DOCUMENT_ATOMIC_CONTEXT");
  for(auto v:{s.min_width,s.min_height})if(v.unit!=AETHER_CSS_UNIT_AUTO&&px(v)!=0)throw std::invalid_argument("DOCUMENT_ATOMIC_MIN");
  for(auto v:{s.max_width,s.max_height})if(v.unit!=AETHER_CSS_UNIT_AUTO)throw std::invalid_argument("DOCUMENT_ATOMIC_MAX");
  const double width=s.width.unit==AETHER_CSS_UNIT_AUTO?0.:px(s.width);if(s.height.unit!=AETHER_CSS_UNIT_AUTO)(void)px(s.height);
  for(auto edges:{s.margin,s.padding,s.border_width})for(auto v:{edges.top,edges.right,edges.bottom,edges.left})(void)px(v);
  const auto border=resolve_border_edges(engine,s,0);
  const double edge=px(s.padding.left)+px(s.padding.right)+border.left+border.right;
  const double result=(s.box_sizing==AETHER_BOX_SIZING_BORDER_BOX?std::max(width,edge):width+edge)+px(s.margin.left)+px(s.margin.right);
  if(!std::isfinite(result)||result>65536)throw std::length_error("DOCUMENT_ATOMIC_WIDTH");return result;
 }
 static std::optional<std::pair<float,float>> admit_drawing_items(const aether_layout_engine_s* engine,const std::vector<inline_item_s>& items,int32_t mode){
  std::optional<std::pair<float,float>> common;
  for(const auto& item:items){
   if(item.unit_barrier||item.hanging_barrier||item.block_anchor_index!=static_cast<std::size_t>(-1)||(!item.is_atomic&&item.style.white_space!=mode))
    throw std::invalid_argument("DOCUMENT_DRAWING_IFC");
   if(item.is_atomic){(void)atomic_width(engine,item);continue;}
   if(item.marker_kind==inline_item_s::MarkerKind::admission_only){
    if(!item.legacy_ignored||item.document_source||item.is_line_break||!item.text.empty()||!transparent_unit_inline(item.style))throw std::invalid_argument("DOCUMENT_DRAWING_MARKER");
    const auto metrics=DocumentTextAccess::validate_admission_marker(engine,item.element,item.style);
    if(common&&*common!=metrics)throw std::invalid_argument("DOCUMENT_MARKER_STRUT_UNSUPPORTED");common=metrics;
   }else if(!item.document_source&&!item.is_line_break)throw std::invalid_argument("DOCUMENT_DRAWING_ATOMIC_UNSUPPORTED");
  }
  // Dropping a marker is allowed only for a homogeneous vertical IFC. Real
  // fragments on every nonempty line then represent the same strut; empty and
  // forced lines separately require the identical effective minimum height.
  if(common)for(const auto& item:items)if(item.document_source){
   const auto metrics=DocumentTextAccess::validate_admission_marker(engine,item.element,item.style,false);
   if(*common!=metrics)throw std::invalid_argument("DOCUMENT_MARKER_STRUT_UNSUPPORTED");
  }
  return common;
 }
 static void reserve_drawing_box(const aether_layout_engine_s* engine,aether_layout_result_s* result,
  aether_layout_box_s* parent,std::size_t bytes){
  auto& budget=DocumentTextAccess::drawing_budget(engine);budget.normalized(1);
  if(result->live_box_count>=65536)throw std::length_error("DOCUMENT_DRAWING_BOX_CAP");
  budget.bytes(1,sizeof(aether_layout_box_s));budget.bytes(bytes+1);
  const auto reserve=[&](auto& vector){if(vector.size()==vector.capacity()){
   const auto old=vector.capacity(),next=old?std::min(std::size_t{65536},old*2):std::size_t{1};
   if(next<=old)throw std::length_error("DOCUMENT_DRAWING_EDGE_CAP");
   budget.bytes(next,sizeof(aether_layout_box_s*));vector.reserve(next);
  }};
  reserve(parent->children);if(result->records_boxes)reserve(result->paint_order);
 }
 // Internal TU-only retention accounting; does not expose the budget reference.
 static std::uint64_t atomic_birth(const aether_layout_box_s* box){return box->registry_birth_;}
 static void work_drawing(const aether_layout_engine_s* engine,std::uint64_t n){DocumentTextAccess::drawing_budget(engine).normalized(n);}
 static void retain_drawing(const aether_layout_engine_s* engine,std::size_t count,std::size_t bytes){DocumentTextAccess::drawing_budget(engine).bytes(count,bytes);}
 static void reserve_atomic_cleanup(const aether_layout_engine_s* engine,std::size_t count){if(count>65536)throw std::length_error("DOCUMENT_ATOMIC_COUNT");DocumentTextAccess::normalization_budget(engine).normalized(std::uint64_t(count)*65536);}
 static void retain(const aether_layout_engine_s* engine,std::size_t count,std::size_t bytes){
  DocumentTextAccess::normalization_budget(engine).bytes(count,bytes);
 }
 static void discard_flex(const aether_layout_engine_s* engine,const std::vector<inline_item_s>& items){
  auto& budget=DocumentTextAccess::normalization_budget(engine);budget.bytes(items.size(),sizeof(Source));std::vector<Source> inputs;inputs.reserve(items.size());
  for(const auto& item:items){DocumentTextAccess::check(engine);budget.normalized(1);if(item.style.display!=AETHER_DISPLAY_FLEX||!item.document_source)throw std::invalid_argument("DOCUMENT_TEXT_FLEX_DISCARD");inputs.push_back(item.document_source);}
  DocumentTextAccess::begin_pass(engine,inputs);PassGuard pass_guard{engine};
  for(const auto& source:inputs){DocumentTextAccess::validate_source(engine,source);if(source->decoded.empty()){budget.bytes(1,sizeof(DocumentTextRange));DocumentTextAccess::dropped(engine,{{source->ordinal,0,0,0,0}});continue;}
   for(std::size_t i=0;i<source->decoded.size();++i){DocumentTextAccess::check(engine);budget.normalized(1);const auto c=source->decoded[i];if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n')throw std::invalid_argument("DOCUMENT_TEXT_FLEX_DISCARD_SCALAR");budget.bytes(1,sizeof(DocumentTextRange));DocumentTextAccess::dropped(engine,{{source->ordinal,i,1,0,0}});}
  }
  DocumentTextAccess::finish_pass(engine);pass_guard.complete=true;
  // Flex item collection is cached and may first run in an intrinsic probe.
  // Its whitespace-only normalization pass never creates a geometry obligation.
 }
 static std::vector<inline_source_unit> collect(const aether_layout_engine_s* engine,
     const std::vector<inline_item_s>& items,int32_t mode) {
  auto& budget=DocumentTextAccess::normalization_budget(engine);
  std::vector<inline_source_unit> units;std::vector<Pending> pending;
  budget.bytes(items.size(),sizeof(Source));std::vector<Source> inputs;inputs.reserve(items.size());
  for(const auto& item:items){DocumentTextAccess::check(engine);budget.normalized(1);if(item.document_source)inputs.push_back(item.document_source);}
  DocumentTextAccess::begin_pass(engine,inputs);PassGuard pass_guard{engine};
  bool line_content=false,previous_cr=false,have_order=false;std::size_t last_order=0,pending_owner=0;
  const bool pre=mode==AETHER_WHITE_SPACE_PRE||mode==AETHER_WHITE_SPACE_PRE_WRAP;
  auto check=[&](){DocumentTextAccess::check(engine);budget.normalized(1);};
  auto drop=[&](const Source& source,std::size_t begin,std::size_t length){
   budget.bytes(1,sizeof(DocumentTextRange));
   DocumentTextAccess::dropped(engine,{{source->ordinal,begin,length,0,0}});
  };
  auto append=[&](inline_source_kind kind,std::size_t owner,const std::string& bytes,
                  const std::vector<Pending>& input){
   if(units.empty()||units.back().kind!=kind){budget.bytes(1,sizeof(inline_source_unit));units.push_back({kind,{}});}
   auto& list=units.back().fragments;
   if(list.empty()||list.back().item_index!=owner){budget.bytes(1,sizeof(inline_source_fragment));list.push_back({owner,{}});}
   auto& fragment=list.back();const auto begin=fragment.text.size();
   budget.bytes(bytes.size());fragment.text+=bytes;
   for(const auto& p:input){check();budget.bytes(1,sizeof(DocumentTextRange));auto r=p.range;r.output_begin=begin;r.output_length=bytes.size();fragment.provenance.push_back(r);
    if(fragment.sources.empty()||fragment.sources.back()!=p.source){
     budget.bytes(1,sizeof(Source));fragment.sources.push_back(p.source);
    }
   }
  };
  auto flush=[&](bool emit){
   if(emit&&!pending.empty())append(inline_source_kind::space,pending_owner," ",pending);
   else for(const auto& p:pending){check();drop(p.source,p.range.input_begin,p.range.input_length);}
   pending.clear();
  };
  auto forced=[&](){flush(false);budget.bytes(1,sizeof(inline_source_unit));units.push_back({inline_source_kind::forced,{}});line_content=false;};
  for(std::size_t index=0;index<items.size();++index){
   check();const auto& item=items[index];const auto& source=item.document_source;
   if(item.is_atomic){flush(line_content);budget.bytes(1,sizeof(inline_source_unit));units.push_back({inline_source_kind::atomic,{},index});line_content=true;previous_cr=false;continue;}
   if(!source){if(!item.text.empty()||item.is_line_break)throw std::invalid_argument("DOCUMENT_TEXT_UNAUTHENTICATED_ITEM");continue;}
   DocumentTextAccess::validate_source(engine,source);
   if(have_order&&source->dom_ordinal<=last_order)throw std::invalid_argument("DOCUMENT_TEXT_PASS_ORDER");
   have_order=true;last_order=source->dom_ordinal;
   if(source->kind==DocumentTextSource::Kind::comment){drop(source,0,0);continue;}
   if(source->kind==DocumentTextSource::Kind::line_break){drop(source,0,0);forced();previous_cr=false;continue;}
   if(item.text!=source->decoded)throw std::invalid_argument("DOCUMENT_TEXT_PASS_BYTES");
   if(source->decoded.empty()){drop(source,0,0);continue;}
   // Complete consumption of each source in original UTF-8 scalar order is
   // structural here. No caller-provided slices can omit, repeat or reorder it.
   for(std::size_t offset=0;offset<source->decoded.size();){
    check();const auto begin=offset;const auto c=static_cast<unsigned char>(source->decoded[offset]);
    const auto length=c<128?1u:c<224?2u:c<240?3u:4u;
    if(length>source->decoded.size()-offset)throw std::invalid_argument("DOCUMENT_TEXT_PASS_UTF8");offset+=length;
    if(pre&&c=='\n'&&previous_cr){drop(source,begin,length);previous_cr=false;continue;}
    previous_cr=pre&&c=='\r';
    if(pre&&(c=='\r'||c=='\n')){drop(source,begin,length);forced();continue;}
    const bool space=length==1&&(c==' '||c=='\t'||c=='\r'||c=='\n');
    if(pre&&c=='\t')throw std::invalid_argument("TAB_PRESERVATION_UNSUPPORTED");
    if(space&&!pre){if(pending.empty())pending_owner=index;budget.bytes(1,sizeof(Pending));pending.push_back({source,{source->ordinal,begin,length,0,0}});continue;}
    flush(line_content);budget.bytes(length+sizeof(Pending));
    append(space?inline_source_kind::space:inline_source_kind::word,index,
       source->decoded.substr(begin,length),{{source,{source->ordinal,begin,length,0,0}}});
    line_content=true;
   }
  }
  flush(false);
  std::uint64_t unit_ordinal=0;
  for(auto& unit:units){for(auto& fragment:unit.fragments){check();const auto& item=items[fragment.item_index];
   fragment.authenticated=DocumentTextAccess::fragment(engine,fragment.sources,item.element,item.style,fragment.text,fragment.provenance);
   if(DocumentTextAccess::drawing_mode(engine))DocumentTextAccess::register_normalized_unit(engine,fragment.authenticated,unit_ordinal,unit.kind==inline_source_kind::space);
  }++unit_ordinal;}
  DocumentTextAccess::finish_pass(engine);pass_guard.complete=true;
  return units;
 }
 struct ExactIntrinsics {double min_content{},max_content{};};
 static ExactIntrinsics atomic_intrinsics(const aether_layout_engine_s* engine,const StyleLookup* styles,const inline_item_s& item,bool content_only=false){
  const double fixed=atomic_width(engine,item);
  if(item.style.width.unit!=AETHER_CSS_UNIT_AUTO)return {fixed,fixed};
  if(!styles)throw std::invalid_argument("DOCUMENT_ATOMIC_INTRINSIC_LOOKUP");
  // Count before callbacks; reserve the full collector capacity rather than
  // charging one logical append while vector growth allocates larger blocks.
  auto& budget=DocumentTextAccess::drawing_budget(engine);
  std::size_t nodes=0;
  const auto count=[&](auto&& self,const aether_node_s* node,std::size_t depth)->void{
   budget.normalized(1);if(!node||depth>64||++nodes>16384)throw std::length_error("DOCUMENT_ATOMIC_INTRINSIC_TREE");
   for(const auto* child:node->children)self(self,child,depth+1);
  };
  count(count,item.element,1);
  budget.bytes(nodes*3,sizeof(inline_item_s));
  std::vector<inline_item_s> children;children.reserve(nodes*3);inline_capitalization_context_s context;
  budget.bytes(item.element->children.size(),sizeof(aether_node_s*));
  const auto direct=item.element->children;
  for(auto* child:direct){
   const auto role=append_inline_node(engine,styles,item.element,item.style,child,children,context);
   if(role==inline_node_role_e::block_content)throw std::invalid_argument("DOCUMENT_ATOMIC_BLOCK_CONTENT");
  }
  for(const auto& child:children)if(child.is_atomic)throw std::invalid_argument("DOCUMENT_ATOMIC_NESTED_UNSUPPORTED");
  const auto raw=drawing_intrinsics_exact(engine,styles,children,item.style.white_space);
  if(content_only)return raw;
  const auto border=resolve_border_edges(engine,item.style,0);
  const double edges=double(item.style.padding.left.value)+item.style.padding.right.value+border.left+border.right+item.style.margin.left.value+item.style.margin.right.value;
  ExactIntrinsics out{raw.min_content+edges,raw.max_content+edges};
  if(!std::isfinite(out.min_content)||!std::isfinite(out.max_content)||out.min_content<0||out.max_content<out.min_content||out.max_content>65536)throw std::length_error("DOCUMENT_ATOMIC_INTRINSIC_LIMIT");return out;
 }
 static ExactIntrinsics drawing_intrinsics_exact(const aether_layout_engine_s* engine,const StyleLookup* styles,
  const std::vector<inline_item_s>& items,int32_t mode){
  const auto strut=admit_drawing_items(engine,items,mode);
  DocumentTextAccess::normalization_budget(engine).bytes(items.size(),sizeof(ExactIntrinsics));
  std::vector<ExactIntrinsics> atom_widths(items.size());
  for(std::size_t i=0;i<items.size();++i)if(items[i].is_atomic)atom_widths[i]=atomic_intrinsics(engine,styles,items[i]);
  const auto units=collect(engine,items,mode);
  auto& budget=DocumentTextAccess::normalization_budget(engine);auto& backend=DocumentTextAccess::drawing_backend(engine);
  std::size_t count=0;for(const auto& unit:units){if(unit.fragments.size()>65536-count)throw std::length_error("DOCUMENT_FIT_PARTS");count+=unit.fragments.size();}
  budget.bytes(count,sizeof(std::shared_ptr<const aether::rendering::text::ShapedRun>));
  std::vector<std::shared_ptr<const aether::rendering::text::ShapedRun>> runs;runs.reserve(count);
  for(const auto& unit:units)for(const auto& source:unit.fragments){const auto& item=items[source.item_index];
   if(!source.authenticated||item.style.white_space!=mode)throw std::invalid_argument("DOCUMENT_DRAWING_SOURCE_POLICY");
   runs.push_back(DocumentTextAccess::measure(engine,source.authenticated,item.style,item.element).run);
  }
  if(strut){
   if(runs.empty())throw std::invalid_argument("DOCUMENT_MARKER_EMPTY_IFC");
   for(const auto& run:runs)if(std::pair{run->metrics.height,run->metrics.baseline}!=*strut)throw std::invalid_argument("DOCUMENT_MARKER_STRUT_UNSUPPORTED");
  }
  std::shared_ptr<const DocumentFitCatalog> catalog;if(!runs.empty())catalog=backend.prepare_fit_catalog(engine,runs);
  const bool wrap=aether_white_space_allows_soft_wrap(mode);
  std::size_t cursor=0;double physical=0,maximum=0,minimum=0;
  for(const auto& unit:units){
   if(unit.kind==inline_source_kind::forced){maximum=std::max(maximum,physical);physical=0;continue;}
   if(unit.kind==inline_source_kind::atomic){const auto width=atom_widths.at(unit.atomic_item);physical+=width.max_content;if(wrap)minimum=std::max(minimum,width.min_content);if(!std::isfinite(physical)||physical>65536)throw std::length_error("DOCUMENT_INTRINSIC_WIDTH");continue;}
   double whole=0,indivisible=0;bool have=false;std::tuple<int32_t,bool,bool> policy;bool anywhere=false;
   for(const auto& source:unit.fragments){
    budget.normalized(1);const auto& item=items[source.item_index];const aether::style::MeasuresReadView m(*item.style.measures_owner());
    const auto wb=m.word_break(),ow=m.overflow_wrap();
    if((wb!=0&&wb!=3)||ow<0||ow>2)throw std::invalid_argument("DOCUMENT_BREAK_POLICY_UNSUPPORTED");
    const auto used=std::make_tuple(mode,wrap&&(wb==3||ow!=0),wrap&&(wb==3||ow==2));
    if(unit.kind==inline_source_kind::word&&have&&policy!=used)throw std::invalid_argument("DOCUMENT_MIXED_BREAK_POLICY");
    have=true;policy=used;anywhere=std::get<2>(used);
    const auto n=backend.fit_boundary(engine,catalog,cursor,0).count;
    if(n<2)throw std::invalid_argument("DOCUMENT_FIT_EMPTY");
    whole+=backend.fit_width(engine,catalog,cursor,0,n-1);
    if(unit.kind==inline_source_kind::word&&anywhere)for(std::size_t i=0;i+1<n;++i){
     budget.normalized(1);indivisible=std::max(indivisible,backend.fit_width(engine,catalog,cursor,i,i+1));
    }
    ++cursor;
   }
   physical+=whole;
   if(!std::isfinite(physical)||physical>65536.)throw std::length_error("DOCUMENT_INTRINSIC_WIDTH");
   // Conditional/hanging spaces never create a min-content opportunity of their
   // own. overflow-wrap:break-word leaves the whole word in min-content.
   if(wrap&&unit.kind==inline_source_kind::word)minimum=std::max(minimum,anywhere?indivisible:whole);
  }
  maximum=std::max(maximum,physical);if(!wrap)minimum=maximum;
  return {minimum,maximum};
 }
 struct DrawingFragment {
  std::size_t item_index{};
  std::string text;
  document_inline_line_metrics metrics;
  aether::rendering::text::Placement native_text;
  std::shared_ptr<const DocumentConstructionInterval> construction;
  DrawingAtom* atom=nullptr;
 };
 template<class Emit>
 static double drawing_lines(const aether_layout_engine_s* engine,
  const std::vector<inline_item_s>& items,int32_t mode,double available,double minimum,Emit emit,bool geometry=true,
  std::optional<std::pair<float,float>> physical_strut=std::nullopt,std::vector<DrawingAtom>* atoms=nullptr){
  std::shared_ptr<const DocumentLayoutSnapshot> atomic_snapshot;
  if(atoms){auto* layout=DocumentTextAccess::construction_layout(engine);atomic_snapshot=DocumentTextAccess::capture_construction_geometry(engine,layout,&DocumentTextAccess::drawing_backend(engine));}
  const auto strut=admit_drawing_items(engine,items,mode);
  if(strut&&minimum!=double(strut->first))throw std::invalid_argument("DOCUMENT_MARKER_MINIMUM_UNSUPPORTED");
  auto units=collect(engine,items,mode);
  auto& budget=DocumentTextAccess::normalization_budget(engine);
  auto& backend=DocumentTextAccess::drawing_backend(engine);
  if(geometry)DocumentTextAccess::bind_latest_construction_pass(engine);
  struct Part {std::size_t item{},count{};std::shared_ptr<const aether::rendering::text::ShapedRun> run;std::shared_ptr<const DocumentTextFragment> fragment;};
  std::size_t count=0;
  for(const auto& u:units){if(u.fragments.size()>65536-count)throw std::length_error("DOCUMENT_FIT_PARTS");count+=u.fragments.size();}
  budget.bytes(count,sizeof(Part)+sizeof(std::shared_ptr<const aether::rendering::text::ShapedRun>));
  std::vector<Part> parts;parts.reserve(count);
  std::vector<std::shared_ptr<const aether::rendering::text::ShapedRun>> runs;runs.reserve(count);
  for(const auto& unit:units)for(const auto& source:unit.fragments){
   const auto& item=items[source.item_index];
   if(!source.authenticated||item.style.white_space!=mode)throw std::invalid_argument("DOCUMENT_DRAWING_SOURCE_POLICY");
   auto measured=DocumentTextAccess::measure(engine,source.authenticated,item.style,item.element);
   parts.push_back({source.item_index,0,measured.run,source.authenticated});runs.push_back(std::move(measured.run));
  }
  if(strut){
   if(runs.empty())throw std::invalid_argument("DOCUMENT_MARKER_EMPTY_IFC");
   for(const auto& run:runs)if(std::pair{run->metrics.height,run->metrics.baseline}!=*strut)throw std::invalid_argument("DOCUMENT_MARKER_STRUT_UNSUPPORTED");
  }
  std::shared_ptr<const DocumentFitCatalog> catalog;
  if(!runs.empty())catalog=backend.prepare_fit_catalog(engine,runs);
  for(std::size_t i=0;i<parts.size();++i)parts[i].count=backend.fit_boundary(engine,catalog,i,0).count;
  if(atomic_snapshot)DocumentTextAccess::validate_final_geometry(engine,atomic_snapshot,&backend);
  if(atoms){auto* layout=DocumentTextAccess::construction_layout(engine);
   budget.bytes(layout->live_box_count,sizeof(aether_layout_box_s*)+4*sizeof(void*));std::unordered_set<const aether_layout_box_s*> members;members.reserve(layout->live_box_count);
   for(auto* box:layout->allocated_boxes){budget.normalized(1);members.insert(box);}
   const auto rect=[](aether_rectf a,aether_rectf b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;};
   const auto real=[](float a,float b){return a==b||(std::isnan(a)&&std::isnan(b));};
   const auto node_same=[](const aether_node_s* key,const std::shared_ptr<node_identity_s>& token){return !key?!token:token&&token->alive&&token->attached&&token->owner==key;};
   for(const auto& atom:*atoms)if(atom.box){
    if(atom.stamps.size()!=atom.paint.size()||atom.paint.empty()||atom.paint[0]!=atom.box)throw std::invalid_argument("DOCUMENT_ATOMIC_TREE_CHANGED");
    // Membership and birth precede every saved-box dereference, including root.
    for(std::size_t i=0;i<atom.stamps.size();++i){const auto& r=atom.stamps[i];
     budget.normalized(1+r.children.size()+r.text.size()+r.control_display_text.size());
     if(!members.contains(r.key)||r.key!=atom.paint[i]||r.key->registry_birth_!=r.birth)throw std::invalid_argument("DOCUMENT_ATOMIC_CHILD_CHANGED");
     const auto* box=r.key;
     budget.normalized(1);for(auto n=layout->primary_boxes.size();n;n>>=1)budget.normalized(2);
     if(layout->find_primary_box(r.element)!=r.primary)throw std::invalid_argument("DOCUMENT_ATOMIC_PRIMARY_CHANGED");
     if(box->owner!=layout||!box->block_static_anchors.empty()||!node_same(r.element,r.element_token)||!node_same(r.presentation_owner,r.presentation_token))throw std::invalid_argument("DOCUMENT_ATOMIC_LIFETIME");
     if((r.element&&r.element->identity!=r.element_token)||(r.presentation_owner&&r.presentation_owner->identity!=r.presentation_token))throw std::invalid_argument("DOCUMENT_ATOMIC_NODE_CHANGED");
     if(box->element!=r.element||box->presentation_owner!=r.presentation_owner||box->kind!=r.kind||box->children!=r.children||box->text!=r.text||box->document_source!=r.document_source||box->document_interval!=r.document_interval||box->document_construction!=r.document_construction||box->document_atomic_widths!=r.document_atomic_widths||box->document_has_last_line!=r.document_has_last_line||box->document_last_line_baseline!=r.document_last_line_baseline||box->document_layout_attempt!=r.document_layout_attempt||box->document_atomic_inline!=r.document_atomic_inline)throw std::invalid_argument("DOCUMENT_ATOMIC_CHANGED");
  if(box->is_atomic_control!=r.is_atomic_control)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->is_initial_canvas!=r.is_initial_canvas)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->control_display_text!=r.control_display_text)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->control_shows_placeholder!=r.control_shows_placeholder)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->visual_sample_valid!=r.visual_sample_valid)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->visual_has_transform!=r.visual_has_transform)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->visual_opacity!=r.visual_opacity)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->visual_timestamp!=r.visual_timestamp)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->collapsed_margin_before!=r.collapsed_margin_before)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->collapsed_margin_after!=r.collapsed_margin_after)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->own_relative_dx!=r.own_relative_dx)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->own_relative_dy!=r.own_relative_dy)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->scroll_flow_height!=r.scroll_flow_height)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->scroll_completion_flags!=r.scroll_completion_flags)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->style.style_owner()!=r.style.style_owner()||box->style.measures_owner()!=r.style.measures_owner()||!native_text_detail::scalars_equal(box->style.scalars(),r.style.scalars())||box->native_text.run!=r.native_text.run||box->native_text.baseline!=r.native_text.baseline||box->visual_identity.lock()!=r.visual_token)throw std::invalid_argument("DOCUMENT_GEOMETRY_STYLE");
  for(auto pair:{std::pair{box->margin_box,r.margin_box},std::pair{box->border_box,r.border_box},std::pair{box->padding_box,r.padding_box},std::pair{box->content_box,r.content_box}})if(!rect(pair.first,pair.second))throw std::invalid_argument("DOCUMENT_GEOMETRY_RECT");
  const auto& a=box->visual_transform;const auto& b=r.visual_transform;
  if(a.m11!=b.m11||a.m12!=b.m12||a.m21!=b.m21||a.m22!=b.m22||a.dx!=b.dx||a.dy!=b.dy||
     box->scroll.scrollable_overflow_x!=r.scroll.scrollable_overflow_x||box->scroll.scrollable_overflow_y!=r.scroll.scrollable_overflow_y||box->scroll.scroll_offset_x!=r.scroll.scroll_offset_x||box->scroll.scroll_offset_y!=r.scroll.scroll_offset_y||
     box->scroll_proxy.left!=r.scroll_proxy.left||box->scroll_proxy.right!=r.scroll_proxy.right||box->scroll_proxy.reference_content_x!=r.scroll_proxy.reference_content_x||box->scroll_proxy.valid!=r.scroll_proxy.valid||
     box->used_border.left!=r.used_border.left||box->used_border.top!=r.used_border.top||box->used_border.right!=r.used_border.right||box->used_border.bottom!=r.used_border.bottom||!real(box->scroll_base_height,r.scroll_base_height))throw std::invalid_argument("DOCUMENT_GEOMETRY_PROXY");
    }
   }
  }
  if(atoms){std::uint64_t ordinal=0;for(const auto& unit:units){if(unit.kind==inline_source_kind::atomic){auto& atom=atoms->at(unit.atomic_item);if(!atom.box)throw std::invalid_argument("DOCUMENT_ATOMIC_MISSING");
    if(geometry)atom.event=DocumentTextAccess::bind_atomic_unit(engine,atom.box,ordinal,atom.width,atom.height,atom.baseline);
   }++ordinal;}}
  document_inline_line_resolver resolver(available,minimum);
  if(physical_strut)resolver.set_physical_strut(physical_strut->first,physical_strut->second);
  std::vector<DrawingFragment> line;
  double total=0;std::uint64_t line_number=0;bool represented=false;bool after_atom=false;
  const bool wrap=aether_white_space_allows_soft_wrap(mode);
  const bool preserved=mode==AETHER_WHITE_SPACE_PRE_WRAP;
  auto close=[&](inline_line_end end){
   if(!resolver.empty()||end==inline_line_end::forced){
    const auto summary=resolver.close(end);emit(line,summary,total,end);
    total+=summary.height;if(!std::isfinite(total)||total>65536.)throw std::length_error("DOCUMENT_FIT_HEIGHT");
    ++line_number;
   }
   resolver.reset();line.clear();
  };
  auto append=[&](DocumentWordPiece piece,bool whitespace){
   const auto& part=parts[piece.table];
   const auto begin=backend.fit_boundary(engine,catalog,piece.table,piece.begin);
   const auto end=backend.fit_boundary(engine,catalog,piece.table,piece.end);
   if(end.byte<begin.byte||end.byte>part.run->source.size())throw std::invalid_argument("DOCUMENT_FIT_BYTES");
   budget.retained();
   if(line.size()>=65536)throw std::length_error("DOCUMENT_LINE_CAP");
   if(line.size()==line.capacity()){const auto capacity=std::min(std::size_t{65536},std::max(std::size_t{1},line.capacity()*2));budget.bytes(capacity,sizeof(DrawingFragment));line.reserve(capacity);}
   if(line.size()==resolver.retained_capacity()){const auto capacity=std::min(std::size_t{65536},std::max(std::size_t{1},resolver.retained_capacity()*2));budget.bytes(capacity,sizeof(document_inline_line_metrics));resolver.reserve_retained(capacity);}
   budget.bytes(end.byte-begin.byte+1);
   auto transport=geometry?backend.fit_construction(engine,catalog,piece.table,piece.begin,piece.end):std::shared_ptr<const DocumentConstructionInterval>{};
   DrawingFragment f{part.item,part.run->source.substr(begin.byte,end.byte-begin.byte),
    {piece.width,piece.height,piece.baseline,true,whitespace,mode},
    {part.run,part.run->metrics.baseline},std::move(transport)};
   resolver.append(f.metrics);line.push_back(std::move(f));represented=true;
  };
  auto unit_width=[&](std::size_t begin,std::size_t end){double width=0;
   for(auto i=begin;i<end;++i){budget.normalized(1);width+=backend.fit_width(engine,catalog,i,0,parts[i].count-1);}
   if(!std::isfinite(width)||width>65536.)throw std::length_error("DOCUMENT_FIT_WIDTH");return width;
  };
  auto append_whole=[&](std::size_t begin,std::size_t end,bool whitespace){
   for(auto i=begin;i<end;++i){const auto& p=parts[i];append({i,0,p.count-1,backend.fit_width(engine,catalog,i,0,p.count-1),p.run->metrics.height,p.run->metrics.baseline},whitespace);}
  };
  std::size_t cursor=0,pending_begin=0,pending_end=0;bool opportunity=false;
  auto seal_pending=[&](std::size_t first,std::size_t last,DrawingAtom* following){
   budget.bytes(1,sizeof(DocumentSoftWrapPlan)+2*sizeof(void*));
   budget.bytes(line.size(),sizeof(std::shared_ptr<const DocumentConstructionInterval>)+sizeof(std::shared_ptr<const DocumentAtomicInlineEvent>)+1);
   budget.bytes(pending_end-pending_begin+last-first,sizeof(std::shared_ptr<const DocumentConstructionInterval>));
   auto plan=std::shared_ptr<DocumentSoftWrapPlan>(new DocumentSoftWrapPlan);plan->available_=available;plan->line_number_=line_number;
   plan->line_.reserve(line.size());plan->line_atoms_.reserve(line.size());plan->line_order_.reserve(line.size());plan->pending_.reserve(pending_end-pending_begin);plan->next_.reserve(last-first);
   for(const auto& f:line){plan->line_order_.push_back(f.atom?1:0);if(f.atom)plan->line_atoms_.push_back(f.atom->event);else plan->line_.push_back(f.construction);}
   for(auto i=pending_begin;i<pending_end;++i)plan->pending_.push_back(backend.fit_construction(engine,catalog,i,0,parts[i].count-1));
   if(following)plan->next_atom_=following->event;else for(auto i=first;i<last;++i)plan->next_.push_back(backend.fit_construction(engine,catalog,i,0,parts[i].count-1));
   DocumentTextAccess::seal_soft_wrap_plan(engine,plan);
   for(auto i=pending_begin;i<pending_end;++i)DocumentTextAccess::suppress_normal_separator(engine,parts[i].fragment,plan);
  };
  auto append_atom=[&](std::size_t item){
   if(!atoms||item>=atoms->size())throw std::invalid_argument("DOCUMENT_ATOMIC_UNPREPARED");auto& atom=atoms->at(item);
   if(pending_begin!=pending_end){const auto pending=unit_width(pending_begin,pending_end);
    if(resolver.needs_break(pending+atom.width,wrap)){if(geometry)seal_pending(0,0,&atom);close(inline_line_end::soft);}
    else append_whole(pending_begin,pending_end,true);pending_begin=pending_end=0;
   }
   if(resolver.needs_break(atom.width,wrap))close(inline_line_end::soft);
   budget.retained();if(line.size()>=65536)throw std::length_error("DOCUMENT_LINE_CAP");
   if(line.size()==line.capacity()){const auto n=std::min(std::size_t{65536},std::max(std::size_t{1},line.capacity()*2));budget.bytes(n,sizeof(DrawingFragment));line.reserve(n);}
   if(line.size()==resolver.retained_capacity()){const auto n=std::min(std::size_t{65536},std::max(std::size_t{1},resolver.retained_capacity()*2));budget.bytes(n,sizeof(document_inline_line_metrics));resolver.reserve_retained(n);}
   DrawingFragment f;f.item_index=item;f.metrics={atom.width,atom.height,atom.baseline,false,false,mode,true};f.atom=&atom;
   resolver.append(f.metrics);line.push_back(std::move(f));represented=true;opportunity=true;after_atom=true;
  };
  for(const auto& unit:units){
   if(unit.kind==inline_source_kind::forced){close(inline_line_end::forced);opportunity=false;continue;}
   if(unit.kind==inline_source_kind::atomic){append_atom(unit.atomic_item);continue;}
   const auto first=cursor;cursor+=unit.fragments.size();
   if(unit.kind==inline_source_kind::space){
    if(preserved){append_whole(first,cursor,true);opportunity=true;}
    else if(wrap){pending_begin=first;pending_end=cursor;}
    else append_whole(first,cursor,true);
    continue;
   }
   bool emergency=false;bool have_policy=false;std::tuple<int32_t,bool,bool> policy;
   for(auto i=first;i<cursor;++i){
    const auto& item=items[parts[i].item];const aether::style::MeasuresReadView m(*item.style.measures_owner());
    const auto wb=m.word_break(),ow=m.overflow_wrap();
    if((wb!=0&&wb!=3)||ow<0||ow>2)throw std::invalid_argument("DOCUMENT_BREAK_POLICY_UNSUPPORTED");
    const bool split=wrap&&(wb==3||ow!=0),min_split=wrap&&(wb==3||ow==2);
    const auto used=std::make_tuple(mode,split,min_split);
    if(have_policy&&policy!=used)throw std::invalid_argument("DOCUMENT_MIXED_BREAK_POLICY");
    have_policy=true;policy=used;emergency=split;
   }
   if(pending_begin!=pending_end){
    const auto pending_width=unit_width(pending_begin,pending_end);
    if(resolver.needs_break(pending_width+unit_width(first,cursor),true)){
     // Source disposition is emitted at this actual decision, never later to
     // repair a gap in final coverage. Original source/pass remain untouched.
     if(geometry)seal_pending(first,cursor,nullptr);
     close(inline_line_end::soft);
    }else append_whole(pending_begin,pending_end,true);
    pending_begin=pending_end=0;
   }
   budget.bytes(cursor-first,sizeof(DocumentWordPart));std::vector<DocumentWordPart> word;word.reserve(cursor-first);
   for(auto i=first;i<cursor;++i)word.push_back({i,parts[i].count,parts[i].run->metrics.height,parts[i].run->metrics.baseline});
   DocumentWordFit::append(word,resolver,available,wrap&&((preserved&&opportunity)||after_atom),emergency,budget,
    [&](auto table,auto begin,auto end){return backend.fit_width(engine,catalog,table,begin,end);},
    [&](DocumentWordPiece piece){append(piece,false);},[&]{close(inline_line_end::soft);});
   opportunity=false;after_atom=false;
  }
  if(pending_begin!=pending_end)throw std::invalid_argument("DOCUMENT_UNEXPECTED_TRAILING_SEPARATOR");
  if(strut&&!represented)throw std::invalid_argument("DOCUMENT_MARKER_EMPTY_IFC");
  close(inline_line_end::paragraph_end);return total;
 }
};
}

void document_text_discard_flex_sources(const aether_layout_engine_s* engine,aether_node_s* parent,const BoxStyleSnapshot& style,const std::vector<aether_node_s*>& nodes){
 std::vector<inline_item_s> items;
 for(auto* node:nodes)collect_document_source(engine,parent,node,style,items);
 aether::layout::DocumentTextNormalizer::discard_flex(engine,items);
}

static std::vector<inline_source_unit> collect_source_units(
    const aether_layout_engine_s* engine,const std::vector<inline_item_s>& items, int32_t mode)
{
    if(DocumentTextAccess::active(engine))return aether::layout::DocumentTextNormalizer::collect(engine,items,mode);
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
        const auto measured = fragment.authenticated ? DocumentTextAccess::measure(engine,fragment.authenticated,item.style,item.element) : engine_measure_text_transport(engine, fragment.text.c_str(), &item.style, 0.f, item.element);
        const auto metrics = measured.metrics;
        result.width = checked_inline_sum(result.width, metrics.width);
        if (fragment.authenticated && item.style.white_space == AETHER_WHITE_SPACE_PRE_WRAP) {
            aether::layout::DocumentTextNormalizer::retain(engine, 2, sizeof(measured_inline_fragment));
            aether::layout::DocumentTextNormalizer::retain(engine, 1, fragment.text.size() + 1);
        }
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
    const auto units = collect_source_units(engine,items, mode);
    const bool wrap = aether_white_space_allows_soft_wrap(mode);
    inline_line_resolver resolver(available_width, minimum_line_height);
    std::vector<measured_inline_fragment> line;
    measured_source_unit pending;
    const bool preserved_wrap = mode == AETHER_WHITE_SPACE_PRE_WRAP;
    bool preserved_opportunity = false;
    float total_height = 0.f;
    auto close = [&](inline_line_end end) {
        if (!resolver.empty() || end == inline_line_end::forced) {
            const auto summary = resolver.close(end);
            emit_line(line, summary, total_height, end);
            total_height = checked_inline_sum(total_height, summary.height);
        }
        resolver.reset();
        line.clear();
        preserved_opportunity = false;
    };
    auto accept = [&](measured_source_unit& unit) {
        for (auto& fragment : unit.fragments) {
            if (preserved_wrap && DocumentTextAccess::active(engine)) {
                aether::layout::DocumentTextNormalizer::retain(engine, 2,
                    sizeof(inline_line_metrics) + sizeof(measured_inline_fragment));
            }
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
        if (preserved_wrap) {
            // A preserved group remains physically on this line even if it
            // exceeds available width. Its following word may trigger a soft
            // break; close() computes hanging/alignment from retained prefixes.
            // collect_source_units joins adjacent spaces across inline owners.
            if (source.kind == inline_source_kind::space) {
                accept(unit);
                preserved_opportunity = true;
            } else {
                if (resolver.needs_break(unit.width, preserved_opportunity))
                    close(inline_line_end::soft);
                accept(unit);
                preserved_opportunity = false;
            }
            continue;
        }
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
    const aether_layout_engine_s* engine, const std::vector<inline_item_s>& items, int32_t mode,const StyleLookup* styles)
{
    if(DocumentTextAccess::drawing_mode(engine)){const auto exact=aether::layout::DocumentTextNormalizer::drawing_intrinsics_exact(engine,styles,items,mode);return {float(exact.min_content),float(exact.max_content)};}
    const auto units = collect_source_units(engine,items, mode);
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
        // Preserved spaces hang at min-content opportunities (including a
        // space-only paragraph), while max-content retains every advance.
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

static document_atomic_widths_s document_auto_atomic_border_width(const aether_layout_engine_s* engine,const StyleLookup* styles,aether_node_s* node,const BoxStyleSnapshot& style,double containing){
 using N=aether::layout::DocumentTextNormalizer;
 if(!std::isfinite(containing)||containing<0||containing>65536)throw std::invalid_argument("DOCUMENT_ATOMIC_CONTAINING_WIDTH");
 const auto sizes=N::atomic_intrinsics(engine,styles,inline_item_s::atomic(node,style),true);
 const auto border=resolve_border_edges(engine,style,0);
 const double e=double(style.padding.left.value)+style.padding.right.value+border.left+border.right;
 const double m=double(style.margin.left.value)+style.margin.right.value;
 const double minimum=sizes.min_content,preferred=sizes.max_content;
 const double available=std::max(0.,containing-e-m);
 const double content=std::min(std::max(minimum,available),preferred),out=content+e;
 if(!std::isfinite(out)||out<0||out+m>65536)throw std::length_error("DOCUMENT_ATOMIC_USED_WIDTH");return {content,out,out+m};
}
static void admit_document_atomic_style(const aether_layout_engine_s* engine,aether_node_s* node,const BoxStyleSnapshot& style){
 auto item=inline_item_s::atomic(node,style);(void)aether::layout::DocumentTextNormalizer::atomic_width(engine,item);
}

// Owns all provisional children until they are actually attached to the parent.
// Destruction revokes each child ticket before deleting its genuine subtree.
struct document_prepared_atoms {
 const aether_layout_engine_s* engine; aether_layout_result_s* result;
 std::vector<aether::layout::DocumentTextNormalizer::DrawingAtom> values;
 int initial_exceptions=std::uncaught_exceptions();
 ~document_prepared_atoms()noexcept{
  if(std::uncaught_exceptions()>initial_exceptions){
   // A rejected callback may have corrupted child edges. The genuine engine
   // failure path owns the registry and severs all edges before reclamation.
   // Revoke custody now, but never recursively delete an untrusted subtree.
   for(auto& atom:values)if(atom.box)aether::layout::DocumentTextAccess::discard_drawing_geometry(engine,atom.box);
   return;
  }
  for(auto& atom:values)if(atom.box){bool member=false;for(auto* b:result->allocated_boxes)if(b==atom.box)member=true;
   aether::layout::DocumentTextAccess::discard_drawing_geometry(engine,atom.box);if(member)delete atom.box;
  }
 }
};
static void prepare_document_atoms(document_prepared_atoms& prepared,const StyleLookup* styles,
 const std::vector<inline_item_s>& items,float available_width,float available_height,percentage_height_s containing_height,const BoxStyleSnapshot& parent_style){
 using N=aether::layout::DocumentTextNormalizer;
 N::reserve_atomic_cleanup(prepared.engine,std::count_if(items.begin(),items.end(),[](const auto& item){return item.is_atomic;}));
 N::retain(prepared.engine,items.size(),sizeof(N::DrawingAtom));prepared.values.resize(items.size());
 for(std::size_t i=0;i<items.size();++i)if(items[i].is_atomic){
  if(parent_style.display==AETHER_DISPLAY_INLINE_BLOCK)throw std::invalid_argument("DOCUMENT_ATOMIC_NESTED_UNSUPPORTED");
  (void)N::atomic_width(prepared.engine,items[i]);
  N::retain_drawing(prepared.engine,1,sizeof(aether_layout_box_s)+sizeof(decltype(prepared.result->primary_boxes)::value_type)+4*sizeof(void*));
  if(prepared.result->live_box_count>=65536)throw std::length_error("DOCUMENT_ATOMIC_BOX_CAP");
  if(prepared.result->records_boxes&&prepared.result->paint_order.size()==prepared.result->paint_order.capacity()){
   const auto n=std::min(std::size_t{65536},std::max(std::size_t{1},prepared.result->paint_order.capacity()*2));
   N::retain_drawing(prepared.engine,n,sizeof(aether_layout_box_s*));prepared.result->paint_order.reserve(n);
  }
  const auto first=prepared.result->paint_order.size();
  auto* box=layout_element(prepared.engine,styles,items[i].element,{0,0,available_width,available_height},containing_height,0,false,0,false,0,forced_height_kind_e::none,true,prepared.result);
  auto& atom=prepared.values[i];atom.box=box;
  if(!box->document_has_last_line)throw std::invalid_argument("DOCUMENT_ATOMIC_NO_LINE");
  atom.width=box->document_atomic_widths?box->document_atomic_widths->margin:double(box->margin_box.width);atom.height=box->margin_box.height;
  atom.baseline=double(box->content_box.y)-box->margin_box.y+box->document_last_line_baseline;
  if(!std::isfinite(atom.baseline)||atom.baseline<0||atom.baseline>65536)throw std::invalid_argument("DOCUMENT_ATOMIC_BASELINE");
  const auto count=box->children.size()+1;
  if(count>65536||prepared.result->paint_order.size()<first)throw std::invalid_argument("DOCUMENT_ATOMIC_PAINT_COUNT");
  N::retain_drawing(prepared.engine,prepared.result->live_box_count,sizeof(aether_layout_box_s*)+4*sizeof(void*));
  std::unordered_set<const aether_layout_box_s*> members;members.reserve(prepared.result->live_box_count);
  N::work_drawing(prepared.engine,prepared.result->live_box_count+count);
  for(auto* key:prepared.result->allocated_boxes)members.insert(key);
  // Callback-free full authentication before dereferencing node identities for
  // owned stamps. Subsequent siblings may allocate, but may not mutate these boxes.
  N::authenticate_atomic_capture(prepared.engine,prepared.result);
  N::retain_drawing(prepared.engine,count,sizeof(aether_layout_box_s*)+sizeof(N::AtomicStamp));atom.paint.reserve(count);atom.stamps.reserve(count);
  for(std::size_t j=0;j<count;++j){auto* retained=j?box->children[j-1]:box;
   if(!members.contains(retained)||(j&&retained->kind!=LAYOUT_BOX_TEXT))throw std::invalid_argument("DOCUMENT_ATOMIC_CHILD_KIND");
   if(prepared.result->records_boxes&&(prepared.result->paint_order.size()-first!=count||prepared.result->paint_order[first+j]!=retained))throw std::invalid_argument("DOCUMENT_ATOMIC_PAINT_ORDER");
   N::retain_drawing(prepared.engine,retained->text.size()+retained->control_display_text.size()+2,1);
   N::retain_drawing(prepared.engine,retained->children.size(),sizeof(aether_layout_box_s*));
   atom.paint.push_back(retained);const auto* box=retained;N::AtomicStamp r;
   N::work_drawing(prepared.engine,1);for(auto n=prepared.result->primary_boxes.size();n;n>>=1)N::work_drawing(prepared.engine,2);
   r.primary=prepared.result->find_primary_box(box->element);
   r.key=box;r.birth=N::atomic_birth(box);r.element_token=box->element?box->element->identity:nullptr;
   r.presentation_token=box->presentation_owner?box->presentation_owner->identity:nullptr;r.visual_token=box->visual_identity.lock();
  r.element=box->element;
  r.presentation_owner=box->presentation_owner;
  r.kind=box->kind;
  r.style=box->style;
  r.children=box->children;
  r.text=box->text;
  r.document_source=box->document_source;
  r.native_text=box->native_text;
  r.document_interval=box->document_interval;r.document_construction=box->document_construction;
  r.document_atomic_widths=box->document_atomic_widths;
  r.document_has_last_line=box->document_has_last_line;r.document_last_line_baseline=box->document_last_line_baseline;
  r.document_layout_attempt=box->document_layout_attempt;r.document_atomic_inline=box->document_atomic_inline;
  r.is_atomic_control=box->is_atomic_control;
  r.is_initial_canvas=box->is_initial_canvas;
  r.control_display_text=box->control_display_text;
  r.control_shows_placeholder=box->control_shows_placeholder;
  r.scroll=box->scroll;
  r.visual_sample_valid=box->visual_sample_valid;
  r.visual_has_transform=box->visual_has_transform;
  r.visual_transform=box->visual_transform;
  r.visual_opacity=box->visual_opacity;
  r.visual_timestamp=box->visual_timestamp;
  r.collapsed_margin_before=box->collapsed_margin_before;
  r.collapsed_margin_after=box->collapsed_margin_after;
  r.own_relative_dx=box->own_relative_dx;
  r.own_relative_dy=box->own_relative_dy;
  r.used_border=box->used_border;
  r.margin_box=box->margin_box;
  r.border_box=box->border_box;
  r.padding_box=box->padding_box;
  r.content_box=box->content_box;
  r.scroll_proxy=box->scroll_proxy;
  r.scroll_flow_height=box->scroll_flow_height;
  r.scroll_base_height=box->scroll_base_height;
  r.scroll_completion_flags=box->scroll_completion_flags;
   atom.stamps.push_back(std::move(r));
  }
  prepared.result->paint_order.resize(first);
 }
}

static float layout_drawing_inline_items(const aether_layout_engine_s* engine,const StyleLookup* styles,
 aether_layout_box_s* parent_box,const std::vector<inline_item_s>& items,
 float start_x,float start_y,float available_width,float available_height,percentage_height_s containing_height,const BoxStyleSnapshot& parent_style,aether_layout_result_s* result,std::optional<double> document_logical_width){
 using namespace aether::layout;
 const double logical_width=document_logical_width.value_or(double(available_width));
 if(parent_style.text_align==AETHER_TEXT_ALIGN_JUSTIFY)
  throw std::invalid_argument("DOCUMENT_DRAWING_JUSTIFY_UNSUPPORTED");
 const double minimum=std::max(double(layout_used_line_height(engine,parent_box->element,parent_style)),1.);
 const bool has_atoms=std::any_of(items.begin(),items.end(),[](const auto& item){return item.is_atomic;});
 document_prepared_atoms prepared{engine,result,{}};
 std::optional<std::pair<float,float>> physical;
 if(has_atoms||parent_style.display==AETHER_DISPLAY_INLINE_BLOCK)physical=DocumentTextNormalizer::physical_strut(engine,parent_box->element,parent_style);
 if(has_atoms)prepare_document_atoms(prepared,styles,items,available_width,available_height,containing_height,parent_style);
 auto emit=[&](const auto& line,const document_inline_line_summary& summary,double y,inline_line_end){
  parent_box->document_has_last_line=true;
  parent_box->document_last_line_baseline=double(start_y)+y+summary.ascent;
  double x=start_x;
  if(parent_style.text_align==AETHER_TEXT_ALIGN_CENTER)x+=std::max(0.,(logical_width-summary.alignment_width)/2.);
  else if(parent_style.text_align==AETHER_TEXT_ALIGN_RIGHT||parent_style.text_align==AETHER_TEXT_ALIGN_END)
   x+=std::max(0.,logical_width-summary.alignment_width);
  const double left=x,right=x+summary.alignment_width;
  aether_check_scroll_coordinate(left);aether_check_scroll_coordinate(right);
  for(const auto& fragment:line){
   const auto& item=items[fragment.item_index];
   const double top=double(start_y)+y+std::max(0.,summary.ascent-fragment.metrics.baseline);
   for(double value:{x,top,fragment.metrics.width,fragment.metrics.height})
    if(!std::isfinite(value)||std::abs(value)>65536.)throw std::length_error("DOCUMENT_DRAWING_GEOMETRY");
   if(fragment.atom){auto& atom=*fragment.atom;auto* box=atom.box;
    if(!box||!atom.event||box->document_atomic_inline!=atom.event)throw std::invalid_argument("DOCUMENT_ATOMIC_EMIT");
    DocumentTextNormalizer::retain_drawing(engine,1,sizeof(aether_layout_box_s*));
    if(parent_box->children.size()==parent_box->children.capacity()){const auto n=std::max(std::size_t{1},parent_box->children.capacity()*2);DocumentTextNormalizer::retain_drawing(engine,n,sizeof(aether_layout_box_s*));parent_box->children.reserve(n);}
    const auto needed=result->paint_order.size()+atom.paint.size();
    if(needed>65536)throw std::length_error("DOCUMENT_ATOMIC_PAINT_CAP");
    if(needed>result->paint_order.capacity()){DocumentTextNormalizer::retain_drawing(engine,needed,sizeof(aether_layout_box_s*));result->paint_order.reserve(needed);}
    const auto normal=normal_flow_margin_box(box);offset_box(box,float(x)-normal.x,float(top)-normal.y);
    parent_box->add_child(box);if(result->records_boxes)result->paint_order.insert(result->paint_order.end(),atom.paint.begin(),atom.paint.end());atom.box=nullptr;
    x+=fragment.metrics.width;continue;
   }
   DocumentTextNormalizer::reserve_drawing_box(engine,result,parent_box,fragment.text.size());
   auto* box=result->create_box();box->kind=LAYOUT_BOX_TEXT;box->element=item.element;box->style=item.style;
   box->presentation_owner=item.element?item.element->anonymous_flex_owner:nullptr;
   box->text=fragment.text;box->native_text=fragment.native_text;
   box->document_construction=fragment.construction;
   if(!box->document_construction)throw std::invalid_argument("DOCUMENT_DRAWING_MISSING_CONSTRUCTION");
   box->content_box={float(x),float(top),box->document_construction->projected_width(),float(fragment.metrics.height)};
   box->padding_box=box->border_box=box->margin_box=box->content_box;
   if(summary.hanging_width>0.){
    const auto& rect=box->content_box;
    box->scroll_proxy={std::clamp(double(rect.x),left,right),
     std::clamp(double(rect.x)+rect.width,left,right),rect.x,true};
    (void)aether_scroll_own_bounds(box);
   }
   if(result->records_boxes)result->paint_order.push_back(box);
   parent_box->add_child(box);x+=fragment.metrics.width;
  }
 };
 return float(DocumentTextNormalizer::drawing_lines(engine,items,parent_style.white_space,
  logical_width,minimum,emit,true,physical,has_atoms?&prepared.values:nullptr));
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
    aether_layout_result_s* result,std::optional<double> document_logical_width)
{
    if(DocumentTextAccess::drawing_mode(engine)){
        if(!uses_source_unit_resolver(engine,items,parent_style))throw std::invalid_argument("DOCUMENT_DRAWING_IFC");
        aether::layout::DocumentTextNormalizer::admit_drawing_items(engine,items,parent_style.white_space);
        return layout_drawing_inline_items(engine,styles,parent_box,items,start_x,start_y,available_width,available_height,containing_height,parent_style,result,document_logical_width);
    }
    if (uses_source_unit_resolver(engine,items, parent_style))
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
            if(DocumentTextAccess::active(engine))collect_document_source(engine,element,node,style,output);
            else collect_text_items(element, style, node->text, output, context);
        } else if(node->type==AETHER_NODE_COMMENT&&DocumentTextAccess::active(engine)){collect_document_source(engine,element,node,style,output);
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
                if(DocumentTextAccess::active(engine))collect_document_source(engine,node,node,child_style,output);
                else output.push_back(inline_item_s::line_break(node, child_style));
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
    if(DocumentTextAccess::active(engine)&&(node->type==AETHER_NODE_TEXT||node->type==AETHER_NODE_COMMENT)){
        collect_document_source(engine,parent,node,parent_style,output);return inline_node_role_e::inline_content;
    }
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
        if(DocumentTextAccess::active(engine))collect_document_source(engine,node,node,child_style,output);
                else output.push_back(inline_item_s::line_break(node, child_style));
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
    if (uses_source_unit_resolver(engine,items, parent_style, true)) {
        const auto sizes = measure_source_intrinsic_sizes(engine, items, parent_style.white_space,styles);
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
            const float width = engine_measure_text_transport(
                engine, token.text.c_str(), &item.style, 0.f, item.element).metrics.width;
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
    if(RealSimpleTextAccess::owns(engine,element)) {const float w=RealSimpleTextAccess::measure(engine,element,style).metrics.width+decoration;return {w,w};}
    if(NativeTextAccess::owns(engine,element)) {
        const float width=NativeTextAccess::measure(engine,element,style).metrics.width+decoration;
        return {width,width};
    }
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

    if (!collect_anonymous_flex_text_items(engine,element, style, inline_items)) {
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
    if(RealSimpleTextAccess::owns(engine,element)) return RealSimpleTextAccess::measure(engine,element,style).metrics.height+aether_thicknessf_vertical(padding)+aether_thicknessf_vertical(border);
    if(NativeTextAccess::owns(engine,element)) {
        const auto m=NativeTextAccess::measure(engine,element,style);
        return (m.run ? std::max(m.metrics.height,layout_used_line_height(engine,element,style)) : 0.f)
            +aether_thicknessf_vertical(padding)+aether_thicknessf_vertical(border);
    }
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
        const auto& children = collect_flex_items(engine,styles, result, element, style);
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
        if (!collect_anonymous_flex_text_items(engine,element, style, inline_items)) {
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
    const bool source_units = uses_source_unit_resolver(engine,items, parent_style);
    const float line_minimum = std::max(1.f, layout_used_line_height(engine, owner, parent_style));
    if (source_units) {
        auto emit = [](const auto&, const inline_line_summary&, float, inline_line_end) {};
        if(DocumentTextAccess::drawing_mode(engine)){
            const bool has_atoms=std::any_of(items.begin(),items.end(),[](const auto& item){return item.is_atomic;});
            document_prepared_atoms prepared{engine,result,{}};
            if(has_atoms)prepare_document_atoms(prepared,styles,items,available_width,0,{},parent_style);
            std::optional<std::pair<float,float>> physical;
            if(has_atoms||parent_style.display==AETHER_DISPLAY_INLINE_BLOCK)physical=aether::layout::DocumentTextNormalizer::physical_strut(engine,owner,parent_style);
            return float(aether::layout::DocumentTextNormalizer::drawing_lines(engine,items,parent_style.white_space,
                available_width,line_minimum,[](const auto&,const auto&,double,inline_line_end){},false,physical,has_atoms?&prepared.values:nullptr));
        }
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
    const auto& children = collect_flex_items(engine,styles, result, element, style);
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

} // extern "C"

// Prove the omitted HTML containing block has origin (0,0) and the
// viewport's available width. Unsupported geometry is rejected, not ignored.
static bool document_root_parent(const StyleLookup* styles,aether_doc_s* doc,
                                 aether_node_s* body) {
    auto* html=aether_doc_get_document_element(doc);
    if(html==body) return false; // Standalone BODY has no parent-collapse phase.
    if(!html||html->tag_name!="html"||!body||body->tag_name!="body"||body->parent!=html)
        throw aether::layout::DocumentRootGeometryUnsupported{};
    const auto record=styles->lookup_record_owner(html);
    if(!record||!record->measures())throw aether::layout::DocumentRootGeometryUnsupported{};
    const auto s=get_style(styles,html);const auto view=s.view();
    const aether::style::MeasuresReadView measures(*record->measures());
    const auto zero=[](aether_css_length v){return v.unit==AETHER_CSS_UNIT_PX&&v.value==0&&
        v.linear_percent==0&&v.linear_em==0&&v.linear_rem==0&&v.linear_vw==0&&v.linear_vh==0;};
    const auto automatic=[](aether_css_length v){return v.unit==AETHER_CSS_UNIT_AUTO&&v.value==0&&
        v.linear_percent==0&&v.linear_em==0&&v.linear_rem==0&&v.linear_vw==0&&v.linear_vh==0;};
    if(s.display!=AETHER_DISPLAY_BLOCK||s.position!=AETHER_POSITION_STATIC||
       s.box_sizing!=AETHER_BOX_SIZING_CONTENT_BOX||!s.visible||s.opacity!=1||
       (s.overflow_x!=AETHER_OVERFLOW_VISIBLE&&s.overflow_x!=AETHER_OVERFLOW_AUTO)||
       (s.overflow_y!=AETHER_OVERFLOW_VISIBLE&&s.overflow_y!=AETHER_OVERFLOW_AUTO)||
       measures.direction_rtl()||measures.writing_mode()!=0||!view.transforms().empty()||
       !automatic(s.width)||!automatic(s.height)||!automatic(s.max_width)||!automatic(s.max_height)||
       !(automatic(s.min_width)||zero(s.min_width))||!(automatic(s.min_height)||zero(s.min_height)))
        throw aether::layout::DocumentRootGeometryUnsupported{};
    for(const auto edges:{s.margin,s.padding,s.border_width})
        for(const auto v:{edges.top,edges.right,edges.bottom,edges.left})
            if(!zero(v))throw aether::layout::DocumentRootGeometryUnsupported{};
    for(const auto v:{s.inset.top,s.inset.right,s.inset.bottom,s.inset.left})
        if(!automatic(v))throw aether::layout::DocumentRootGeometryUnsupported{};
    for(const std::string name:{"width","height","min-width","min-height","max-width","max-height"})
        if(measures.contextual_tree(name)||measures.intrinsic_keyword(name)!=aether::css::intrinsic_size_keyword::none)throw aether::layout::DocumentRootGeometryUnsupported{};
    return true;
}

static aether_status_t layout_impl(
    aether_layout_engine_t engine, aether_doc_t doc, aether_style_map_t source_styles,
    float viewport_width, float viewport_height,
    aether_scroll_offset_fn scroll_fn, void* scroll_data,
    aether_layout_result_t* out_result,
    const aether::layout::SelectedLayoutAuthority* selection, bool document_root=false)
{
    if (document_root && (selection || scroll_fn)) return AETHER_ERROR_INVALID_STATE;
    if (selection && !selection->accepts(doc,source_styles)) return AETHER_ERROR_INVALID_STATE;
    if (!engine) return AETHER_ERROR_NULL_ARG;
    if ((selection || document_root) && (engine->initial_canvas_mode || scroll_fn)) return AETHER_ERROR_INVALID_STATE;
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
    struct measure_scope_s { aether_layout_engine_s* e; ~measure_scope_s() { NativeTextAccess::clear(e); RealSimpleTextAccess::clear(e); e->measure_styles = nullptr; e->measure_document.reset(); } } measure_scope{engine};
    try {
    const auto text_selection = engine->text_selection();
    if(text_selection && ((text_selection->mode==aether::rendering::text::MeasurementMode::native &&
        (!text_selection->provider || !text_selection->font || engine->text_measurer || engine->contextual_text_measurer)) ||
        (text_selection->mode==aether::rendering::text::MeasurementMode::legacy &&
        (text_selection->provider || text_selection->font))))
        throw std::invalid_argument("invalid text measurement selection");
    aether::style::FrozenStyleRef defaults;
    const auto default_flat = aether_computed_style_default();
    const auto default_status = aether::style::freeze_style(default_flat, defaults);
    if (default_status == AETHER_ERROR_OUT_OF_MEMORY) throw std::bad_alloc();
    if (default_status != AETHER_OK) throw std::runtime_error("Invalid default style");
    result->build_overlay = std::make_unique<aether::layout::MutableLayoutOverlay>();
    result->assets = std::make_shared<const aether::layout::LayoutAssets>(version, nullptr, defaults, document_identity, document_structure_epoch);
    const StyleLookup build_lookup(version, result->build_overlay.get(), defaults);
    // Declared after lookup: cleanup clears its borrowed pointer first, even on
    // early returns/throws. Overlay nodes remain owned by result/assets until then.
    struct document_measure_scope_s {aether_layout_engine_s* engine;~document_measure_scope_s(){DocumentTextAccess::finish(engine);}} document_measure_scope{engine};
    const auto* styles = &build_lookup;
    engine->measure_styles = styles;
    engine->measure_document = document_identity;
    RealSimpleTextAccess::begin(engine,doc,*styles);
    NativeTextAccess::begin(engine,doc,*styles);
    DocumentTextAccess::begin(engine,doc,*styles,result);
    validate_contents_wrappers(styles, selection ? &selection->scope() : nullptr);
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
    const auto freeze_assets = [&](bool retain_overlay=false) {
        auto assets = std::make_shared<const aether::layout::LayoutAssets>(version, result->build_overlay->freeze(), defaults, document_identity, document_structure_epoch);
        result->assets = std::move(assets);
        if(!retain_overlay)result->build_overlay.reset();
    };
    const bool final_geometry=DocumentTextAccess::final_geometry_mode(engine);
    if(final_geometry&&!document_root)throw std::invalid_argument("DOCUMENT_FINAL_LAYOUT_MODE");
    if(DocumentTextAccess::drawing_mode(engine))DocumentTextAccess::begin_drawing_attempts(engine);
    // The helper owns its snapshot/pin only during prepare; all such owners are
    // destroyed before it returns/throws, hence before any caller delete result.
    const auto complete_final_geometry = [&]() -> aether_status_t {
        if(DocumentTextAccess::drawing_mode(engine))DocumentTextAccess::drawing_attempts(engine).select_root(engine,result->root);
        freeze_assets(true); // immutable owner copies; live overlay becomes read-only
        if(version&&version->contextual()) {
            const auto status=aether_contextual_transition_finalize_layout(result,nullptr);
            if(status!=AETHER_OK)return status;
        }
        validate_used_geometry(result);
        result->build_hit_order_cache(); // optional cache; allocations precede audit
        if(DocumentTextAccess::drawing_mode(engine))DocumentTextAccess::promote_drawing_geometry(engine,result);
        auto pending=DocumentTextAccess::prepare_final_layout(engine,result);
        // No callbacks, allocation or throwing work from prepare return to commit.
        if(!DocumentTextAccess::commit_final_layout(engine,result,pending))return AETHER_ERROR_INVALID_STATE;
        result->build_overlay.reset(); // finish in commit has already cleared borrow
        return AETHER_OK;
    };
    result->effective_context_enabled = prepared || (version && version->contextual());
    if (result->effective_context_enabled) {
        try { result->effective_fixed_container_nodes = effective_fixed_containers(styles, active); }
        catch (const std::bad_alloc&) { DocumentTextAccess::capture_failure(engine); DocumentTextAccess::finish(engine); delete result; return AETHER_ERROR_OUT_OF_MEMORY; }
    }

    // Find root element (using C API from document_node.cpp)
    aether_node_s* root_el = selection ? selection->target() : aether_doc_get_body(doc);
    if (!root_el) root_el = aether_doc_get_document_element(doc);
    if(!root_el&&final_geometry) {
        const auto status=complete_final_geometry();
        if(status!=AETHER_OK){DocumentTextAccess::finish(engine);delete result;return status;}
        result->abandoned=false;*out_result=result;return AETHER_OK;
    }
    if (!root_el) { DocumentTextAccess::finish(engine); freeze_assets(); DocumentTextAccess::complete_layout(engine,result); result->abandoned = false; *out_result = result; return AETHER_OK; } // empty doc

    if(document_root && (!version||!version->contextual()||version->document_lifetime()!=document_identity))
        throw aether::layout::DocumentRootGeometryUnsupported{};
    const bool place_document_body=document_root&&document_root_parent(styles,doc,root_el);
    auto root_style = get_style(styles, root_el);
    engine->root_font_size = get_style(styles, aether_doc_get_document_element(doc)).font_size;
    result->root_font_size = engine->root_font_size;

    if (!selection) {
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

    }
    const int32_t viewport_overflow[2] = {result->viewport_overflow_x, result->viewport_overflow_y};
    auto* root_box = layout_element(engine, styles, root_el,
                                    {0, 0, viewport_width, viewport_height}, viewport_height,
                                    0, selection == nullptr,
                                    0, false, 0, forced_height_kind_e::none, false, result,
                                    selection ? nullptr : viewport_overflow);
    result->root = root_box;
    if(place_document_body&&root_box) {
        const float before=root_box->collapsed_margin_before;
        const float delta=before-normal_flow_border_box(root_box).y;
        if(!std::isfinite(before)||!std::isfinite(delta)||std::abs(before)>65536||std::abs(delta)>65536)
            throw aether::layout::DocumentRootGeometryUnsupported{};
        validate_used_geometry(result);
        // Exactly the parent normal-flow placement, before visual/scroll
        // finalization and immutable assets; never a paint-only correction.
        place_normal_flow_block(root_box,0,before);
    }
    layout_finalize_fixed_containers(engine, styles, result);
    if (!aether::style::structure_valid(version.get()) || !operation_guard.valid() ||
        (selection && !selection->accepts(doc,source_styles))) {
        DocumentTextAccess::finish(engine); delete result; return AETHER_ERROR_INVALID_STATE;
    }
    RealSimpleTextAccess::finish(engine);
    NativeTextAccess::finish(engine);
    NativeTextAccess::clear(engine);
    if(final_geometry) {
        const auto status=complete_final_geometry();
        if(status!=AETHER_OK){DocumentTextAccess::finish(engine);delete result;return status;}
        result->abandoned=false;*out_result=result;return AETHER_OK;
    }
    DocumentTextAccess::finish(engine);
    freeze_assets();
    if (version && version->contextual()) {
        const auto status = aether_contextual_transition_finalize_layout(result, nullptr);
        if (status != AETHER_OK) { delete result; return status; }
    }
    validate_used_geometry(result);
    result->build_hit_order_cache();
    DocumentTextAccess::complete_layout(engine,result);
    result->abandoned = false;
    *out_result = result;
    return AETHER_OK;
    } catch (const std::bad_alloc&) {
        DocumentTextAccess::capture_failure(engine);
        result->abandoned = true; delete result; return AETHER_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        DocumentTextAccess::capture_failure(engine);
        result->abandoned = true; delete result; return AETHER_ERROR_INVALID_STATE;
    }
}

namespace aether::layout {
aether_status_t DocumentRootLayoutAccess::layout(aether_layout_engine_t engine,aether_doc_t doc,
    aether_style_map_t styles,float width,float height,aether_layout_result_t* out) noexcept {
    return layout_impl(engine,doc,styles,width,height,nullptr,nullptr,out,nullptr,true);
}
aether_status_t layout_selected_capture(aether_layout_engine_t engine,aether_doc_t doc,
    aether_style_map_t styles,const SelectedLayoutAuthority& authority,
    aether_layout_result_t* out_result) noexcept {
    return layout_impl(engine,doc,styles,authority.viewport_width(),authority.viewport_height(),
                       nullptr,nullptr,out_result,&authority);
}
}

extern "C" {
AETHER_API aether_status_t aether_layout_layout(
    aether_layout_engine_t engine,aether_doc_t doc,aether_style_map_t styles,
    float viewport_width,float viewport_height,aether_scroll_offset_fn scroll_fn,
    void* scroll_data,aether_layout_result_t* out_result) {
    return layout_impl(engine,doc,styles,viewport_width,viewport_height,
                       scroll_fn,scroll_data,out_result,nullptr);
}

AETHER_API void aether_layout_set_contextual_text_measurer(aether_layout_engine_t engine,
    aether_text_measurer_contextual_fn callback, void* data) {
    if (!engine || RealSimpleTextAccess::active(engine) || DocumentTextAccess::active(engine)) return;
    if (engine->layout_in_progress && engine->text_selection() &&
        engine->text_selection()->mode==aether::rendering::text::MeasurementMode::native) return;
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
