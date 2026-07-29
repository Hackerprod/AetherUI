/* ── Layout Engine — faithful port from LayoutEngine.cs (868 lines) ─ */
#include "layout_types.h"
#include "../abi/aether_abi.h"
#include "../dom/dom_types.h"
#include "../css/css_types.h"
#include "../css/border_utils.h"
#include "../css/text_transform.h"
#include "box_model.h"
#include "form_controls.h"
#include "inline_text.h"
#include "flex_resolver.h"
#include "intrinsic_sizing.h"
#include "margin_adjacency.h"
#include "text_justify.h"
#include "table_layout.h"

#include <algorithm>
#include <cctype>
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

static opaque_control_size_s measure_opaque_single_line_control(
    const aether_layout_engine_s* engine,
    aether_node_s* element,
    const aether_computed_style_t& style)
{
    const std::string text = aether_opaque_control_display_text(element, style);
    if (text.empty()) return {0.f, std::max(style.font_size * style.line_height, style.font_size * 1.1f)};
    const auto metrics = engine_measure_text(engine, text.c_str(), &style, 0.f);
    return {metrics.width, std::max(metrics.height, style.font_size * 1.1f)};
}


static float resolve_length(aether_css_length l, float reference,
                             float font_size, float root_font_size,
                             float vp_w, float vp_h) {
    return aether_css_length_resolve(l, reference, font_size, root_font_size, vp_w, vp_h);
}

static float resolve(const aether_layout_engine_s* e,
                     aether_css_length value, float reference,
                     const aether_computed_style_t& style) {
    return resolve_length(value, reference, style.font_size,
                          e ? e->root_font_size : 16.f,
                          e ? e->viewport_width : 0.f,
                          e ? e->viewport_height : 0.f);
}

static aether_thicknessf resolve_padding_edges(const aether_layout_engine_s* e,
                                               aether_css_edges edges, float reference,
                                               const aether_computed_style_t& style) {
    return aether_resolve_box_edges(edges, reference, style.font_size, e ? e->root_font_size : 16.f,
                                    e ? e->viewport_width : 0.f, e ? e->viewport_height : 0.f, false);
}

static aether_thicknessf resolve_margin_edges(const aether_layout_engine_s* e,
                                              aether_css_edges edges, float reference,
                                              const aether_computed_style_t& style) {
    return aether_resolve_box_edges(edges, reference, style.font_size, e ? e->root_font_size : 16.f,
                                    e ? e->viewport_width : 0.f, e ? e->viewport_height : 0.f, true);
}

static aether_thicknessf resolve_border_edges(const aether_layout_engine_s* e,
                                              const aether_computed_style_t& style,
                                              float reference) {
    return aether_resolve_border_edges(style, reference, e ? e->root_font_size : 16.f,
                                       e ? e->viewport_width : 0.f, e ? e->viewport_height : 0.f);
}

static aether_computed_style_t get_style(aether_style_map_s* styles, aether_node_s* el) {
    if (!styles || !el) return aether_computed_style_default();
    auto it = styles->styles.find(el);
    return (it != styles->styles.end()) ? it->second : aether_computed_style_default();
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
    (void)aether_shadow_list_copy(&parent_style.text_shadows, &anonymous_style.text_shadows);
    anonymous_style.display = AETHER_DISPLAY_BLOCK;

    styles->styles.emplace(anonymous, anonymous_style);
    result->layout_only_style_map = styles;
    result->layout_only_nodes.push_back(anonymous);
    result->layout_only_style_nodes.push_back(anonymous);
    return anonymous;
}

static const std::vector<aether_node_s*>& collect_flex_items(
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
        if (child_style.display != AETHER_DISPLAY_NONE && child_style.visible && !out_of_flow) {
            items.push_back(node);
        }
    }
    flush_direct_text();
    return items;
}

static bool is_out_of_flow(const aether_computed_style_t& style) {
    return style.position == AETHER_POSITION_ABSOLUTE ||
           style.position == AETHER_POSITION_FIXED;
}

static void offset_box(aether_layout_box_s* box, float dx, float dy);

static void offset_rect(aether_rectf& r, float dx, float dy) {
    r.x += dx;
    r.y += dy;
}

static void offset_box(aether_layout_box_s* box, float dx, float dy) {
    offset_rect(box->margin_box, dx, dy);
    offset_rect(box->border_box, dx, dy);
    offset_rect(box->padding_box, dx, dy);
    offset_rect(box->content_box, dx, dy);
    for (auto* child : box->children) {
        offset_box(child, dx, dy);
    }
}

static bool is_auto_margin(aether_css_length value) {
    return value.unit == AETHER_CSS_UNIT_AUTO;
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

static aether_layout_box_s* layout_element(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    aether_rectf containing_block,
    float normal_flow_y,
    bool force_available_width,
    float forced_border_width,
    bool has_forced_border_width,
    float forced_border_height,
    bool has_forced_border_height,
    bool is_independent_layout_item,
    aether_layout_result_s* result);

static void layout_positioned_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf positioning_block,
    aether_layout_result_s* result);

static float layout_atomic_control(
    const aether_layout_engine_s* engine,
    aether_node_s* element,
    const aether_computed_style_t& style,
    float content_width,
    float explicit_height, bool has_explicit_height,
    aether_thicknessf padding,
    aether_thicknessf border);

static float layout_flow_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf content,
    const aether_computed_style_t& parent_style,
    aether_layout_result_s* result);

static float layout_inline_items(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    const std::vector<inline_item_s>& items,
    float start_x, float start_y, float available_width,
    const aether_computed_style_t& parent_style,
    aether_layout_result_s* result);

static float layout_flex_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf content,
    const aether_computed_style_t& style,
    bool has_definite_height,
    aether_layout_result_s* result);

struct table_row_s {
    aether_node_s* element = nullptr;
    std::vector<aether_node_s*> cells;
};

static float layout_table_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* table,
    aether_rectf content,
    aether_layout_result_s* result);

static float measure_intrinsic_table_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* table,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode);

static void collect_inline_element(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    std::vector<inline_item_s>& output);

static void collect_text_items(aether_node_s* owner,
                               const aether_computed_style_t& style,
                               const std::string& text,
                               std::vector<inline_item_s>& output);

enum class inline_node_role_e { ignored, inline_content, block_content };

static inline_node_role_e append_inline_node(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* parent,
    const aether_computed_style_t& parent_style,
    aether_node_s* node,
    std::vector<inline_item_s>& output);

static float measure_intrinsic_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode = aether_intrinsic_size_mode_e::max_content);

static float measure_intrinsic_height(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    aether_thicknessf padding, aether_thicknessf border,
    float available_width);

static float measure_intrinsic_flex_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode);

static float measure_child_natural_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* child, float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode);

static float measure_inline_items_intrinsic_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    const std::vector<inline_item_s>& items,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode);

static float measure_inline_items_height(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    const std::vector<inline_item_s>& items,
    float available_width,
    const aether_computed_style_t& parent_style,
    aether_layout_result_s* result);

/* ── Register ──────────────────────────────────────────────────────── */

static void register_box(aether_layout_result_s* result, aether_layout_box_s* box) {
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

class detached_layout_scope_s {
public:
    explicit detached_layout_scope_s(aether_layout_result_s* result)
        : result_(result), previous_(result->records_boxes) {
        result_->records_boxes = false;
    }

    ~detached_layout_scope_s() { result_->records_boxes = previous_; }

    detached_layout_scope_s(const detached_layout_scope_s&) = delete;
    detached_layout_scope_s& operator=(const detached_layout_scope_s&) = delete;

private:
    aether_layout_result_s* result_;
    bool previous_;
};

/* ── LayoutElement ─────────────────────────────────────────────────── */

static aether_layout_box_s* layout_element(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    aether_rectf containing_block,
    float normal_flow_y,
    bool force_available_width,
    float forced_border_width, bool has_forced_border_width,
    float forced_border_height, bool has_forced_border_height,
    bool is_independent_layout_item,
    aether_layout_result_s* result)
{
    auto style = get_style(styles, element);
    auto* box = result->create_box();
    box->kind = LAYOUT_BOX_ELEMENT;
    box->element = element;
    box->style = style;
    register_box(result, box);

    if (style.display == AETHER_DISPLAY_NONE || !style.visible) {
        return box;
    }

    auto margin     = resolve_margin_edges(engine, style.margin, containing_block.width, style);
    auto padding    = resolve_padding_edges(engine, style.padding, containing_block.width, style);
    auto border     = resolve_border_edges(engine, style, containing_block.width);
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

    float min_width = resolve(engine, style.min_width, containing_block.width, style);
    if (std::isfinite(min_width)) {
        float min_border = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? min_width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border)
            : min_width;
        border_width = std::max(border_width, min_border);
    }

    float max_width = resolve(engine, style.max_width, containing_block.width, style);
    if (std::isfinite(max_width)) {
        float max_border = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? max_width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border)
            : max_width;
        border_width = std::min(border_width, max_border);
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
    if (has_forced_border_height) {
        explicit_height = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? std::max(0.f, forced_border_height - aether_thicknessf_vertical(padding) - aether_thicknessf_vertical(border))
            : forced_border_height;
        has_explicit_height = true;
    } else {
        explicit_height = resolve(engine, style.height, containing_block.height, style);
        has_explicit_height = std::isfinite(explicit_height);
    }

    bool intrinsic_control = is_opaque_single_line_control(element);
    box->is_atomic_control = intrinsic_control;

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

    float content_height;
    if (intrinsic_control) {
        content_height = layout_atomic_control(engine, element, style,
                                               content_width,
                                               explicit_height, has_explicit_height,
                                               padding, border);
    } else if (style.display == AETHER_DISPLAY_FLEX) {
        content_height = layout_flex_children(engine, styles, box, element,
                                              {content_x, content_y, content_width, child_containing_height},
                                              style,
                                              std::isfinite(definite_content_height),
                                              result);
    } else if (style.display == AETHER_DISPLAY_TABLE) {
        content_height = layout_table_children(engine, styles, box, element,
                                               {content_x, content_y, content_width, child_containing_height},
                                               result);
    } else {
        content_height = layout_flow_children(engine, styles, box, element,
                                              {content_x, content_y, content_width, child_containing_height},
                                              style, result);
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
        const float specified_min_height = resolve(engine, style.min_height, containing_block.height, style);

        aether_margin_adjacency_facts start_facts{};
        start_facts.has_border_or_padding_before = aether_box_has_before_decoration(padding, border);
        start_facts.has_in_flow_content_before = !first_is_block;
        start_facts.establishes_block_formatting_context = has_bfc;
        if (first_is_block && aether_parent_start_margin_adjoins_child(start_facts)) {
            box->collapsed_margin_before = aether_collapse_adjoining_margins(margin.top, first->collapsed_margin_before);
            offset_box(first, 0, content_y - first->border_box.y);
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

    // Absolute and fixed descendants do not take part in normal-flow sizing. Lay them out only
    // after the in-flow content has established this element's padding box, which is their
    // containing block for position: absolute.
    const float positioning_height = std::isfinite(definite_content_height)
        ? definite_content_height + aether_thicknessf_vertical(padding)
        : content_height + aether_thicknessf_vertical(padding);
    layout_positioned_children(engine, styles, box, element,
                               {border_x + border.left, border_y + border.top,
                                std::max(0.f, border_width - aether_thicknessf_horizontal(border)),
                                std::max(0.f, positioning_height)},
                               result);

    // Scroll container
    if ((style.overflow_y == AETHER_OVERFLOW_AUTO || style.overflow_y == AETHER_OVERFLOW_SCROLL)
        && std::isfinite(definite_content_height))
    {
        float scrollable = std::max(0.f, content_height - definite_content_height);
        box->scrollable_overflow_y = scrollable;
        if (scrollable > 0) {
            float scroll_offset_y = 0.f;
            if (engine->scroll_offset_fn) {
                scroll_offset_y = engine->scroll_offset_fn(element, engine->scroll_offset_data);
            }
            scroll_offset_y = std::clamp(scroll_offset_y, 0.f, scrollable);
            box->scroll_offset_y = scroll_offset_y;
            if (scroll_offset_y != 0.f) {
                for (auto* child : box->children) {
                    offset_box(child, 0, -scroll_offset_y);
                }
            }
        }
    }

    float border_height = has_explicit_height
        ? (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX
           ? explicit_height + aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border)
           : explicit_height)
        : content_height + aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border);

    float min_height = resolve(engine, style.min_height, containing_block.height, style);
    if (std::isfinite(min_height)) {
        float min_border = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? min_height + aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border)
            : min_height;
        border_height = std::max(border_height, min_border);
    }

    float max_height = resolve(engine, style.max_height, containing_block.height, style);
    if (std::isfinite(max_height)) {
        float max_border = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? max_height + aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border)
            : max_height;
        border_height = std::min(border_height, max_border);
    }

    border_height = std::max(aether_thicknessf_vertical(border) + aether_thicknessf_vertical(padding), border_height);

    box->border_box = { border_x, border_y, border_width, border_height };
    box->padding_box = aether_rectf_deflate(box->border_box, border);
    box->content_box = aether_rectf_deflate(box->padding_box, padding);
    box->margin_box  = { containing_block.x, border_y - box->collapsed_margin_before,
                         border_width + aether_thicknessf_horizontal(margin),
                         border_height + box->collapsed_margin_before + box->collapsed_margin_after };

    // Relative positioning preserves the element's space in normal flow while moving its visual
    // box (and its descendants) by the requested inset. A specified left/top wins over right/bottom.
    if (style.position == AETHER_POSITION_RELATIVE) {
        const float left = resolve(engine, style.inset.left, containing_block.width, style);
        const float right = resolve(engine, style.inset.right, containing_block.width, style);
        const float top = resolve(engine, style.inset.top, containing_block.height, style);
        const float bottom = resolve(engine, style.inset.bottom, containing_block.height, style);
        const float dx = std::isfinite(left) ? left : (std::isfinite(right) ? -right : 0.f);
        const float dy = std::isfinite(top) ? top : (std::isfinite(bottom) ? -bottom : 0.f);
        if (dx != 0.f || dy != 0.f) offset_box(box, dx, dy);
    }
    return box;
}

/* ── Out-of-flow positioned descendants ───────────────────────────── */

static void layout_positioned_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf positioning_block,
    aether_layout_result_s* result)
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
                return aether_rectf{first->margin_box.x, first->margin_box.y, 0.f, 0.f};
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
                        return aether_rectf{
                            candidate->margin_box.x + candidate->margin_box.width,
                            candidate->margin_box.y,
                            0.f, 0.f,
                        };
                    }
                }
            }
        }
        return aether_rectf{positioning_block.x, positioning_block.y, 0.f, 0.f};
    };

    for (auto* node : parent->children) {
        if (node->type != AETHER_NODE_ELEMENT) continue;
        const auto style = get_style(styles, node);
        if (!is_out_of_flow(style) || style.display == AETHER_DISPLAY_NONE || !style.visible) continue;

        // Fixed-position descendants use the viewport; absolute descendants use the nearest
        // positioned ancestor supplied by their parent layout call.
        const aether_rectf containing = style.position == AETHER_POSITION_FIXED
            ? aether_rectf{0.f, 0.f, result->viewport.width, result->viewport.height}
            : positioning_block;
        const auto margin = resolve_margin_edges(engine, style.margin, containing.width, style);
        const float left = resolve(engine, style.inset.left, containing.width, style);
        const float right = resolve(engine, style.inset.right, containing.width, style);
        const float top = resolve(engine, style.inset.top, containing.height, style);
        const float bottom = resolve(engine, style.inset.bottom, containing.height, style);

        const float specified_width = resolve(engine, style.width, containing.width, style);
        const float specified_height = resolve(engine, style.height, containing.height, style);
        const bool constrain_width = !std::isfinite(specified_width) &&
            std::isfinite(left) && std::isfinite(right);
        const bool constrain_height = !std::isfinite(specified_height) &&
            std::isfinite(top) && std::isfinite(bottom);
        const float forced_width = std::max(0.f, containing.width - left - right -
                                             aether_thicknessf_horizontal(margin));
        const float forced_height = std::max(0.f, containing.height - top - bottom -
                                              aether_thicknessf_vertical(margin));

        auto* child_box = layout_element(engine, styles, node, containing, containing.y,
                                         false, forced_width, constrain_width,
                                         forced_height, constrain_height, true, result);

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
        offset_box(child_box, x - child_box->margin_box.x, y - child_box->margin_box.y);
        parent_box->add_child(child_box);
    }
}

/* ── LayoutAtomicControl ───────────────────────────────────────────── */

static float layout_atomic_control(
    const aether_layout_engine_s* engine,
    aether_node_s* element,
    const aether_computed_style_t& style,
    float content_width,
    float explicit_height, bool has_explicit_height,
    aether_thicknessf padding,
    aether_thicknessf border)
{
    (void)content_width;
    float natural_height;
    if (has_explicit_height) {
        natural_height = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? explicit_height
            : std::max(0.f, explicit_height - aether_thicknessf_vertical(padding) - aether_thicknessf_vertical(border));
    } else {
        natural_height = measure_opaque_single_line_control(engine, element, style).content_height;
    }
    return std::max(natural_height, style.font_size * 1.1f);
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

static int32_t flex_cross_alignment(const aether_computed_style_t& container,
                                    const aether_computed_style_t& item) {
    return item.align_self == AETHER_ALIGN_SELF_AUTO
        ? container.align_items
        : item.align_self;
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

/* ── LayoutFlowChildren ────────────────────────────────────────────── */

static float layout_flow_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf content,
    const aether_computed_style_t& parent_style,
    aether_layout_result_s* result)
{
    float cursor_y = content.y;
    std::vector<inline_item_s> inline_items;
    bool has_pending_margin = false;
    float pending_margin = 0.f;

    auto flush_inline = [&]() {
        if (inline_items.empty()) return;
        if (has_pending_margin) {
            cursor_y += pending_margin;
            has_pending_margin = false;
            pending_margin = 0.f;
        }
        const float used = layout_inline_items(engine, styles, parent_box, inline_items,
                                               content.x, cursor_y, content.width,
                                               parent_style, result);
        cursor_y += used;
        // A non-empty inline line separates adjacent block margins. Whitespace
        // alone has zero height and must not prevent the normal CSS collapse.
        if (used > 0.f) {
            has_pending_margin = false;
            pending_margin = 0.f;
        }
        inline_items.clear();
    };

    for (auto* node : parent->children) {
        const auto role = append_inline_node(engine, styles, parent, parent_style, node, inline_items);
        if (role == inline_node_role_e::block_content) {
            const auto child_style = get_style(styles, node);
                flush_inline();
                auto* child_box = layout_element(engine, styles, node,
                                                 {content.x, cursor_y, content.width, content.height},
                                                 cursor_y, true,
                                                 0, false, 0, false, false, result);
                // A pending margin exists only when no in-flow separator has been
                // laid out since the preceding block. Inline content flushes it, so
                // the remaining siblings are adjoining at this point.
                const float collapsed_before = has_pending_margin
                    ? aether_collapse_adjoining_margins(pending_margin, child_box->collapsed_margin_before)
                    : child_box->collapsed_margin_before;
                const float border_y = cursor_y + collapsed_before;
                offset_box(child_box, 0, border_y - child_box->border_box.y);
                parent_box->add_child(child_box);
                aether_margin_adjacency_facts empty_facts{};
                empty_facts.has_in_flow_content_before = !child_box->children.empty();
                empty_facts.has_in_flow_content_after = !child_box->children.empty();
                const auto child_padding = resolve_padding_edges(engine, child_style.padding, content.width, child_style);
                const auto child_border = resolve_border_edges(engine, child_style, content.width);
                empty_facts.has_border_or_padding_before = aether_box_has_before_decoration(child_padding, child_border);
                empty_facts.has_border_or_padding_after = aether_box_has_after_decoration(child_padding, child_border);
                empty_facts.has_definite_height = child_style.height.unit != AETHER_CSS_UNIT_AUTO;
                const float child_min_height = resolve(engine, child_style.min_height, content.height, child_style);
                empty_facts.has_positive_min_height = std::isfinite(child_min_height) && child_min_height > 0;
                if (child_box->border_box.height <= .001f && aether_empty_block_margins_adjoin(empty_facts)) {
                    pending_margin = aether_collapse_adjoining_margins(collapsed_before, child_box->collapsed_margin_after);
                } else {
                    cursor_y = child_box->border_box.y + child_box->border_box.height;
                    pending_margin = child_box->collapsed_margin_after;
                }
                has_pending_margin = true;
        }
    }

    flush_inline();
    return std::max(0.f, cursor_y - content.y + (has_pending_margin ? pending_margin : 0.f));
}

/* ── LayoutInlineItems ─────────────────────────────────────────────── */

static float layout_inline_items(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    const std::vector<inline_item_s>& items,
    float start_x, float start_y, float available_width,
    const aether_computed_style_t& parent_style,
    aether_layout_result_s* result)
{
    float cursor_x = start_x;
    float cursor_y = start_y;
    const float minimum_line_height = std::max(parent_style.font_size * parent_style.line_height, 1.f);
    float line_height = minimum_line_height;
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
                const auto metrics = engine_measure_text(engine, child->text.c_str(), &child->style, 0.f);
                ascent = std::max(ascent, metrics.baseline);
                descent = std::max(descent, std::max(0.f, metrics.height - metrics.baseline));
            }
            line_height = std::max(line_height, ascent + descent);
            if (ascent > 0.f) {
                for (int index = line_start_index; index < end; ++index) {
                    auto* child = parent_box->children[index];
                    if (child->kind != LAYOUT_BOX_TEXT) continue;
                    const auto metrics = engine_measure_text(engine, child->text.c_str(), &child->style, 0.f);
                    offset_box(child, 0.f, std::max(0.f, ascent - metrics.baseline));
                }
            }
            line_widths.push_back({line_start_index, end, width, force_advance});
            cursor_y += line_height;
        }
        cursor_x = start_x;
        line_height = minimum_line_height;
        line_start_index = end;
    };

    bool previous_collapsible_space = false;
    for (auto& item : items) {
        if (item.is_line_break) {
            finish_line(true);
            previous_collapsible_space = false;
            continue;
        }

        if (item.is_atomic) {
            auto* provisional = layout_element(engine, styles, item.element,
                                              {start_x, cursor_y, available_width, result->viewport.height},
                                              cursor_y, false,
                                              0, false, 0, false, true, result);
            float w = provisional->margin_box.width;
            if (cursor_x > start_x && cursor_x + w > start_x + available_width) {
                float old_y = provisional->margin_box.y;
                finish_line(false);
                offset_box(provisional, 0, cursor_y - old_y);
            }
            float dx = cursor_x - provisional->margin_box.x;
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
            const auto metrics = engine_measure_text(engine, token.text.c_str(), &item.style, 0.f);
            if (allow_wrap && cursor_x > start_x && cursor_x + metrics.width > start_x + available_width &&
                (!whitespace || token.kind == aether_inline_text_token_kind_e::preserved_space)) {
                finish_line(false);
            }

            auto* fragment = result->create_box();
            fragment->kind = LAYOUT_BOX_TEXT;
            fragment->element = item.element;
            fragment->style = item.style;
            fragment->text = token.text;
            fragment->content_box = { cursor_x, cursor_y, metrics.width, metrics.height };
            fragment->padding_box = fragment->content_box;
            fragment->border_box  = fragment->content_box;
            fragment->margin_box  = fragment->content_box;
            parent_box->add_child(fragment);
            if (result->records_boxes) result->paint_order.push_back(fragment);
            cursor_x += metrics.width;
            line_height = std::max(line_height, metrics.height);
            previous_collapsible_space = collapsible;
        }
    }

    if (static_cast<int>(parent_box->children.size()) > line_start_index) {
        finish_line(false);
    }

    apply_line_alignment(parent_box, line_widths, available_width, parent_style.text_align);
    return line_widths.empty() ? 0.f : std::max(0.f, cursor_y - start_y);
}

/* ── LayoutFlexChildren ────────────────────────────────────────────── */

static float layout_flex_children(
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
                offset_box(child_box, x - child_box->margin_box.x, cursor_y - child_box->margin_box.y);
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
            offset_box(child_box, cursor_x - child_box->margin_box.x, y - child_box->margin_box.y);
            parent_box->add_child(child_box);
            cursor_x += child_box->margin_box.width + spacing;
        }
        total_cross_size += cross_extent;
        cross_cursor += cross_extent + gap;
    }
    total_cross_size += gap * static_cast<float>(resolved.resolution.lines.empty() ? 0 : resolved.resolution.lines.size() - 1);
    return has_definite_height ? std::max(total_cross_size, content.height) : total_cross_size;
}

/* ── CollectInlineElement ──────────────────────────────────────────── */

static void collect_inline_element(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    std::vector<inline_item_s>& output)
{
    for (auto* node : element->children) {
        if (node->type == AETHER_NODE_TEXT) {
            collect_text_items(element, style, node->text, output);
        } else if (node->type == AETHER_NODE_ELEMENT) {
            auto child_style = get_style(styles, node);
            if (child_style.display == AETHER_DISPLAY_LINE_BREAK) {
                output.push_back(inline_item_s::line_break(node, child_style));
            } else if (is_opaque_single_line_control(node) || child_style.display == AETHER_DISPLAY_INLINE_BLOCK) {
                output.push_back(inline_item_s::atomic(node, child_style));
            } else if (child_style.display != AETHER_DISPLAY_NONE && child_style.visible) {
                collect_inline_element(engine, styles, node, child_style, output);
            }
        }
    }
}

/* ── CollectTextItems ──────────────────────────────────────────────── */

static void collect_text_items(aether_node_s* owner,
                               const aether_computed_style_t& style,
                               const std::string& text,
                               std::vector<inline_item_s>& output)
{
    if (!text.empty()) {
        auto* style_origin = owner ? owner->style_origin : nullptr;
        output.push_back(inline_item_s::text_item(
            style_origin ? style_origin : owner, style,
            aether_apply_text_transform(text, style.text_transform)));
    }
}

static inline_node_role_e append_inline_node(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* parent,
    const aether_computed_style_t& parent_style,
    aether_node_s* node,
    std::vector<inline_item_s>& output)
{
    if (node->type == AETHER_NODE_TEXT) {
        if (!aether_inline_text_has_visible_content(node->text, parent_style.white_space)) {
            return inline_node_role_e::ignored;
        }
        collect_text_items(parent, parent_style, node->text, output);
        return inline_node_role_e::inline_content;
    }
    if (node->type != AETHER_NODE_ELEMENT) return inline_node_role_e::ignored;

    const auto child_style = get_style(styles, node);
    if (child_style.display == AETHER_DISPLAY_NONE || !child_style.visible || is_out_of_flow(child_style)) {
        return inline_node_role_e::ignored;
    }
    if (child_style.display == AETHER_DISPLAY_BLOCK || child_style.display == AETHER_DISPLAY_FLEX ||
        child_style.display == AETHER_DISPLAY_TABLE) {
        return inline_node_role_e::block_content;
    }
    if (child_style.display == AETHER_DISPLAY_LINE_BREAK) {
        output.push_back(inline_item_s::line_break(node, child_style));
    } else if (is_opaque_single_line_control(node) || child_style.display == AETHER_DISPLAY_INLINE_BLOCK) {
        output.push_back(inline_item_s::atomic(node, child_style));
    } else {
        collect_inline_element(engine, styles, node, child_style, output);
    }
    return inline_node_role_e::inline_content;
}

static float measure_inline_items_intrinsic_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
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
                engine_measure_text(engine, token.text.c_str(), &item.style, 0.f).width});
        }
    }
    const auto sizes = aether_measure_inline_intrinsic_sizes(fragments);
    return mode == aether_intrinsic_size_mode_e::min_content ? sizes.min_content : sizes.max_content;
}

/* ── MeasureIntrinsicWidth ─────────────────────────────────────────── */

static float measure_intrinsic_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
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
    auto flush_inline = [&]() {
        if (inline_items.empty()) return;
        content_width = std::max(content_width,
            measure_inline_items_intrinsic_width(engine, styles, inline_items, maximum, result, mode));
        inline_items.clear();
    };

    for (auto* child : element->children) {
        const auto role = append_inline_node(engine, styles, element, style, child, inline_items);
        if (role != inline_node_role_e::block_content) continue;
        flush_inline();
        content_width = std::max(content_width,
            measure_child_natural_width(engine, styles, child, maximum, result, mode));
    }
    flush_inline();
    return content_width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
}

static float measure_intrinsic_height(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    aether_thicknessf padding, aether_thicknessf border,
    float available_width)
{
    const float content_width = std::max(0.f, available_width -
        aether_thicknessf_horizontal(padding) - aether_thicknessf_horizontal(border));
    if (is_opaque_single_line_control(element)) {
        return layout_atomic_control(engine, element, style, content_width, NAN, false, padding, border) +
            aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border);
    }

    float content_height = 0.f;
    int block_child_count = 0;
    std::vector<inline_item_s> inline_items;
    auto flush_inline = [&]() {
        if (inline_items.empty()) return;
        const float inline_height = measure_inline_items_height(
            engine, styles, inline_items, content_width, style, nullptr);
        if (style.display == AETHER_DISPLAY_FLEX && style.flex_direction == AETHER_FLEX_DIRECTION_ROW) {
            content_height = std::max(content_height, inline_height);
        } else {
            content_height += inline_height;
        }
        inline_items.clear();
    };

    for (auto* child : element->children) {
        const auto role = append_inline_node(engine, styles, element, style, child, inline_items);
        if (role != inline_node_role_e::block_content) continue;
        flush_inline();
        ++block_child_count;
        const auto child_style = get_style(styles, child);
        const auto child_padding = resolve_padding_edges(engine, child_style.padding, content_width, child_style);
        const auto child_border = resolve_border_edges(engine, child_style, content_width);
        const auto child_margin = resolve_margin_edges(engine, child_style.margin, content_width, child_style);
        const bool percentage_height = child_style.height.unit == AETHER_CSS_UNIT_PERCENT ||
            (child_style.height.unit == AETHER_CSS_UNIT_LINEAR &&
             std::fabs(child_style.height.linear_percent) > .0001f);
        float child_height = percentage_height ? NAN : resolve(engine, child_style.height, 0.f, child_style);
        if (std::isfinite(child_height)) {
            if (child_style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX) {
                child_height += aether_thicknessf_vertical(child_padding) + aether_thicknessf_vertical(child_border);
            }
        } else {
            child_height = measure_intrinsic_height(engine, styles, child, child_style,
                                                    child_padding, child_border, content_width);
        }
        child_height += aether_thicknessf_vertical(child_margin);
        if (style.display == AETHER_DISPLAY_FLEX && style.flex_direction == AETHER_FLEX_DIRECTION_ROW) {
            content_height = std::max(content_height, child_height);
        } else {
            content_height += child_height;
        }
    }
    flush_inline();
    if (style.display == AETHER_DISPLAY_FLEX &&
        style.flex_direction == AETHER_FLEX_DIRECTION_COLUMN) {
        content_height += std::max(0, block_child_count - 1) * std::max(0.f, style.gap);
    }
    return content_height + aether_thicknessf_vertical(padding) + aether_thicknessf_vertical(border);
}

static float measure_inline_items_height(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    const std::vector<inline_item_s>& items,
    float available_width,
    const aether_computed_style_t& parent_style,
    aether_layout_result_s* result)
{
    const float line_minimum = std::max(1.f, parent_style.font_size * parent_style.line_height);
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
                engine, styles, item.element, style, padding, border, available_width);
            append_fragment(width, height, false);
            continue;
        }
        for (const auto& token : aether_tokenize_inline_text(item.text, item.style.white_space)) {
            if (token.kind == aether_inline_text_token_kind_e::forced_break) {
                finish_line(true);
                continue;
            }
            const bool whitespace = token.kind != aether_inline_text_token_kind_e::word;
            const auto metrics = engine_measure_text(engine, token.text.c_str(), &item.style, 0.f);
            append_fragment(metrics.width, metrics.height, whitespace);
        }
    }
    finish_line(false);
    return total_height;
}

static float measure_intrinsic_flex_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
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
        std::vector<inline_item_s> inline_items;
        collect_inline_element(engine, styles, element, style, inline_items);
        return measure_inline_items_intrinsic_width(engine, styles, inline_items, maximum, result, mode) +
            aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    }

    std::vector<float> child_widths;
    for (auto* child : children) {
        child_widths.push_back(measure_child_natural_width(engine, styles, child, maximum, result, mode));
    }
    const float content = aether_intrinsic_flex_main_contribution(
        child_widths, style.flex_direction == AETHER_FLEX_DIRECTION_COLUMN,
        style.flex_wrap, style.gap, mode);
    return content + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
}

static float measure_child_natural_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* child, float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode)
{
    auto child_style = get_style(styles, child);
    auto child_margin = resolve_margin_edges(engine, child_style.margin, maximum, child_style);
    // A percentage width is indefinite while this intrinsic contribution is
    // calculated. CSS Sizing therefore treats it as auto at this stage.
    const bool percentage_dependent_width =
        child_style.width.unit == AETHER_CSS_UNIT_PERCENT ||
        (child_style.width.unit == AETHER_CSS_UNIT_LINEAR &&
         std::fabs(child_style.width.linear_percent) > .0001f);
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

/* ── Table layout (phase 1: unspanned automatic columns) ────────────────── */

static void collect_table_rows(aether_style_map_s* styles, aether_node_s* container,
                               std::vector<table_row_s>& rows)
{
    for (auto* child : container->children) {
        if (child->type != AETHER_NODE_ELEMENT) continue;
        const auto child_style = get_style(styles, child);
        if (child_style.display == AETHER_DISPLAY_NONE || !child_style.visible) continue;
        if (child_style.display == AETHER_DISPLAY_TABLE_ROW) {
            table_row_s row{};
            row.element = child;
            for (auto* candidate : child->children) {
                if (candidate->type != AETHER_NODE_ELEMENT) continue;
                const auto cell_style = get_style(styles, candidate);
                if (cell_style.display == AETHER_DISPLAY_TABLE_CELL && cell_style.visible) {
                    row.cells.push_back(candidate);
                }
            }
            if (!row.cells.empty()) rows.push_back(std::move(row));
            continue;
        }

        // Row groups are transparent in phase 1. This admits ordinary
        // table/thead/tbody/tfoot markup without claiming row-group sizing.
        if (child_style.display != AETHER_DISPLAY_TABLE_CELL) {
            collect_table_rows(styles, child, rows);
        }
    }
}

static std::vector<aether_table_column_contribution_s> measure_table_columns(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    const std::vector<table_row_s>& rows,
    float maximum,
    aether_layout_result_s* result)
{
    size_t column_count = 0;
    for (const auto& row : rows) column_count = std::max(column_count, row.cells.size());
    std::vector<aether_table_column_contribution_s> columns(column_count);
    for (const auto& row : rows) {
        for (size_t column = 0; column < row.cells.size(); ++column) {
            const float minimum = measure_child_natural_width(
                engine, styles, row.cells[column], maximum, result,
                aether_intrinsic_size_mode_e::min_content);
            const float preferred = measure_child_natural_width(
                engine, styles, row.cells[column], maximum, result,
                aether_intrinsic_size_mode_e::max_content);
            columns[column].min_content = std::max(columns[column].min_content, minimum);
            columns[column].max_content = std::max(columns[column].max_content, preferred);
        }
    }
    return columns;
}

static float measure_intrinsic_table_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* table,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode)
{
    std::vector<table_row_s> rows;
    collect_table_rows(styles, table, rows);
    const auto columns = measure_table_columns(engine, styles, rows, maximum, result);
    float contribution = 0.f;
    for (const auto& column : columns) {
        contribution += mode == aether_intrinsic_size_mode_e::min_content
            ? column.min_content : column.max_content;
    }
    return contribution;
}

static float layout_table_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* table,
    aether_rectf content,
    aether_layout_result_s* result)
{
    std::vector<table_row_s> rows;
    collect_table_rows(styles, table, rows);
    if (rows.empty()) return 0.f;

    const auto contributions = measure_table_columns(engine, styles, rows, content.width, result);
    const auto column_widths = aether_resolve_auto_table_columns(contributions, content.width);
    float cursor_y = content.y;

    for (const auto& row : rows) {
        const auto row_style = get_style(styles, row.element);
        auto* row_box = result->create_box();
        row_box->kind = LAYOUT_BOX_ELEMENT;
        row_box->element = row.element;
        row_box->style = row_style;
        register_box(result, row_box);

        float cursor_x = content.x;
        float row_height = 0.f;
        for (size_t column = 0; column < row.cells.size(); ++column) {
            const float column_width = column_widths[column];
            auto* cell_box = layout_element(engine, styles, row.cells[column],
                                            {cursor_x, cursor_y, column_width, content.height},
                                            cursor_y, true,
                                            0.f, false, 0.f, false, true, result);
            row_height = std::max(row_height, cell_box->margin_box.height);
            row_box->add_child(cell_box);
            cursor_x += column_width;
        }

        row_box->content_box = {content.x, cursor_y, content.width, row_height};
        row_box->padding_box = row_box->content_box;
        row_box->border_box = row_box->content_box;
        row_box->margin_box = row_box->content_box;
        parent_box->add_child(row_box);
        cursor_y += row_height;
    }
    return std::max(0.f, cursor_y - content.y);
}

/* ══════════════════════════════════════════════════════════════════════
   C ABI implementation
   ══════════════════════════════════════════════════════════════════════ */

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

AETHER_API aether_status_t aether_layout_layout(
    aether_layout_engine_t engine, aether_doc_t doc, aether_style_map_t styles,
    float viewport_width, float viewport_height,
    aether_scroll_offset_fn scroll_fn, void* scroll_data,
    aether_layout_result_t* out_result)
{
    if (!engine || !out_result) return AETHER_ERROR_NULL_ARG;
    if (!doc) return AETHER_ERROR_NULL_ARG;
    if (viewport_width <= 0 || viewport_height <= 0) return AETHER_ERROR_INVALID_STATE;

    engine->scroll_offset_fn = scroll_fn;
    engine->scroll_offset_data = scroll_data;
    engine->viewport_width = viewport_width;
    engine->viewport_height = viewport_height;

    auto* result = new(std::nothrow) aether_layout_result_s();
    if (!result) return AETHER_ERROR_OUT_OF_MEMORY;
    result->viewport = {viewport_width, viewport_height};
    result->style_map = styles;

    // Find root element (using C API from document_node.cpp)
    aether_node_s* root_el = aether_doc_get_body(doc);
    if (!root_el) root_el = aether_doc_get_document_element(doc);
    if (!root_el) { *out_result = result; return AETHER_OK; } // empty doc

    auto root_style = get_style(styles, root_el);
    engine->root_font_size = root_style.font_size;

    auto* root_box = layout_element(engine, styles, root_el,
                                    {0, 0, viewport_width, viewport_height},
                                    0, true,
                                    0, false, 0, false, false, result);
    result->root = root_box;
    *out_result = result;
    return AETHER_OK;
}

AETHER_API void aether_layout_result_destroy(aether_layout_result_t result) {
    delete result;
}

AETHER_API aether_layout_box_t aether_layout_result_get_root(aether_layout_result_t result) {
    if (!result) return nullptr;
    return result->root;
}

AETHER_API aether_layout_box_t aether_layout_result_find_primary_box(aether_layout_result_t result, aether_node_t element) {
    if (!result || !element) return nullptr;
    return result->find_primary_box(element);
}

AETHER_API aether_node_t aether_layout_result_hit_test(aether_layout_result_t result, float x, float y) {
    if (!result) return nullptr;
    return result->hit_test(x, y);
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

AETHER_API float aether_layout_box_get_scrollable_overflow_y(aether_layout_box_t box) {
    return box ? box->scrollable_overflow_y : 0;
}
AETHER_API float aether_layout_box_get_scroll_offset_y(aether_layout_box_t box) {
    return box ? box->scroll_offset_y : 0;
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
