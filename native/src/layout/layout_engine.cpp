/* ── Layout Engine — faithful port from LayoutEngine.cs (868 lines) ─ */
#include "layout_types.h"
#include "../abi/aether_abi.h"
#include "../dom/dom_types.h"
#include "../css/css_types.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

/* ─────────────────────────── helpers ────────────────────────────── */

static bool is_atomic_control(aether_node_s* el) {
    return el->tag_name == "input" || el->tag_name == "button";
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

static void append_text_content(const aether_node_s* node, std::string& output) {
    if (!node) return;
    for (auto* child : node->children) {
        if (child->type == AETHER_NODE_TEXT) {
            output += child->text;
        } else if (child->type == AETHER_NODE_ELEMENT) {
            append_text_content(child, output);
        }
    }
}

static std::string text_content(const aether_node_s* element) {
    std::string result;
    append_text_content(element, result);
    return result;
}

static std::string get_atomic_control_text(aether_node_s* element) {
    if (element->tag_name == "button") {
        return normalize_whitespace(text_content(element));
    }
    // RuntimeValue is a managed-side concept; fall back to 'value' attribute
    auto it = element->attributes.find("value");
    if (it != element->attributes.end() && !it->second.empty()) {
        return it->second;
    }
    auto ph = element->attributes.find("placeholder");
    return (ph != element->attributes.end()) ? ph->second : std::string();
}

static std::vector<std::string> tokenize_inline_text(const std::string& text) {
    std::vector<std::string> tokens;
    std::string buf;
    bool whitespace = false;
    for (char c : text) {
        bool cur_ws = std::isspace(static_cast<unsigned char>(c)) != 0;
        if (!buf.empty() && cur_ws != whitespace) {
            tokens.push_back(buf);
            buf.clear();
        }
        whitespace = cur_ws;
        buf += c;
    }
    if (!buf.empty()) {
        tokens.push_back(buf);
    }
    return tokens;
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

static float resolve_or_zero(const aether_layout_engine_s* e,
                             aether_css_length value, float reference,
                             const aether_computed_style_t& style) {
    float r = resolve(e, value, reference, style);
    return std::isfinite(r) ? std::max(0.f, r) : 0.f;
}

static float resolve_margin_value(const aether_layout_engine_s* e,
                                  aether_css_length value, float reference,
                                  const aether_computed_style_t& style) {
    float r = resolve(e, value, reference, style);
    return std::isfinite(r) ? r : 0.f;
}

static aether_thicknessf resolve_edges(const aether_layout_engine_s* e,
                                       aether_css_edges edges, float reference,
                                       const aether_computed_style_t& style) {
    return {
        resolve_or_zero(e, edges.left, reference, style),
        resolve_or_zero(e, edges.top, reference, style),
        resolve_or_zero(e, edges.right, reference, style),
        resolve_or_zero(e, edges.bottom, reference, style),
    };
}

static aether_thicknessf resolve_margin_edges(const aether_layout_engine_s* e,
                                              aether_css_edges edges, float reference,
                                              const aether_computed_style_t& style) {
    return {
        resolve_margin_value(e, edges.left, reference, style),
        resolve_margin_value(e, edges.top, reference, style),
        resolve_margin_value(e, edges.right, reference, style),
        resolve_margin_value(e, edges.bottom, reference, style),
    };
}

static aether_computed_style_t get_style(aether_style_map_s* styles, aether_node_s* el) {
    if (!styles || !el) return aether_computed_style_default();
    auto it = styles->styles.find(el);
    return (it != styles->styles.end()) ? it->second : aether_computed_style_default();
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

/* ── apply line alignment ─────────────────────────────────────────── */

struct line_range { int start; int end; float width; };

static void apply_line_alignment(aether_layout_box_s* parent,
                                 const std::vector<line_range>& lines,
                                 float available_width,
                                 int32_t align) {
    if (align == AETHER_TEXT_ALIGN_START) return;
    for (auto& line : lines) {
        float offset = (align == AETHER_TEXT_ALIGN_CENTER)
            ? (available_width - line.width) / 2
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

static float measure_intrinsic_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result);

static float measure_intrinsic_flex_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result);

static float measure_child_natural_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* child, float maximum,
    aether_layout_result_s* result);

/* ── Register ──────────────────────────────────────────────────────── */

static void register_box(aether_layout_result_s* result, aether_layout_box_s* box) {
    result->primary_boxes.try_emplace(box->element, box);
    result->paint_order.push_back(box);
}

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
    aether_layout_result_s* result)
{
    auto style = get_style(styles, element);
    auto* box = new aether_layout_box_s();
    box->kind = LAYOUT_BOX_ELEMENT;
    box->element = element;
    box->style = style;
    register_box(result, box);

    if (style.display == AETHER_DISPLAY_NONE || !style.visible) {
        return box;
    }

    auto margin     = resolve_margin_edges(engine, style.margin, containing_block.width, style);
    auto padding    = resolve_edges(engine, style.padding, containing_block.width, style);
    auto border     = resolve_edges(engine, style.border_width, containing_block.width, style);

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

    border_width = std::max(aether_thicknessf_horizontal(border) + aether_thicknessf_horizontal(padding),
                            std::min(border_width, available_border_width));

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

    bool intrinsic_control = is_atomic_control(element);
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
    } else {
        content_height = layout_flow_children(engine, styles, box, element,
                                              {content_x, content_y, content_width, child_containing_height},
                                              style, result);
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
    if ((style.overflow == AETHER_OVERFLOW_AUTO || style.overflow == AETHER_OVERFLOW_SCROLL)
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
    box->margin_box  = { containing_block.x, normal_flow_y,
                         border_width + aether_thicknessf_horizontal(margin),
                         border_height + aether_thicknessf_vertical(margin) };

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
                                         forced_height, constrain_height, result);

        const auto static_anchor = static_position(node);
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
    float natural_height;
    if (has_explicit_height) {
        natural_height = (style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? explicit_height
            : std::max(0.f, explicit_height - aether_thicknessf_vertical(padding) - aether_thicknessf_vertical(border));
    } else {
        std::string displayed_text = get_atomic_control_text(element);
        if (displayed_text.empty()) {
            natural_height = style.font_size * style.line_height;
        } else {
            auto metrics = engine_measure_text(engine, displayed_text.c_str(), &style, content_width);
            natural_height = metrics.height;
        }
    }
    return std::max(natural_height, style.font_size * 1.1f);
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

    auto flush_inline = [&]() {
        if (inline_items.empty()) return;
        cursor_y += layout_inline_items(engine, styles, parent_box, inline_items,
                                        content.x, cursor_y, content.width,
                                        parent_style, result);
        inline_items.clear();
    };

    for (auto* node : parent->children) {
        if (node->type == AETHER_NODE_TEXT) {
            collect_text_items(parent, parent_style, node->text, inline_items);
        } else if (node->type == AETHER_NODE_ELEMENT) {
            auto child_style = get_style(styles, node);
            if (child_style.display == AETHER_DISPLAY_NONE || !child_style.visible) continue;
            if (is_out_of_flow(child_style)) continue;

            if (child_style.display == AETHER_DISPLAY_BLOCK || child_style.display == AETHER_DISPLAY_FLEX) {
                flush_inline();
                auto* child_box = layout_element(engine, styles, node,
                                                 {content.x, cursor_y, content.width, content.height},
                                                 cursor_y, true,
                                                 0, false, 0, false, result);
                parent_box->add_child(child_box);
                cursor_y += child_box->margin_box.height;
            } else if (child_style.display == AETHER_DISPLAY_LINE_BREAK) {
                inline_items.push_back(inline_item_s::line_break(node, child_style));
            } else if (is_atomic_control(node) || child_style.display == AETHER_DISPLAY_INLINE_BLOCK) {
                inline_items.push_back(inline_item_s::atomic(node, child_style));
            } else {
                collect_inline_element(engine, styles, node, child_style, inline_items);
            }
        }
        // comments ignored
    }

    flush_inline();
    return std::max(0.f, cursor_y - content.y);
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
    float line_height = std::max(parent_style.font_size * parent_style.line_height, 1.f);
    int line_start_index = static_cast<int>(parent_box->children.size());
    std::vector<line_range> line_widths;

    auto finish_line = [&](bool force_advance) {
        int end = static_cast<int>(parent_box->children.size());
        float width = std::max(0.f, cursor_x - start_x);
        if (end > line_start_index || force_advance) {
            line_widths.push_back({line_start_index, end, width});
            cursor_y += line_height;
        }
        cursor_x = start_x;
        line_height = std::max(parent_style.font_size * parent_style.line_height, 1.f);
        line_start_index = end;
    };

    for (auto& item : items) {
        if (item.is_line_break) {
            finish_line(true);
            continue;
        }

        if (item.is_atomic) {
            auto* provisional = layout_element(engine, styles, item.element,
                                              {start_x, cursor_y, available_width, result->viewport.height},
                                              cursor_y, false,
                                              0, false, 0, false, result);
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
            continue;
        }

        // text item
        auto tokens = tokenize_inline_text(item.text);
        for (auto& token : tokens) {
            bool is_ws = std::all_of(token.begin(), token.end(),
                                     [](char c){ return std::isspace(static_cast<unsigned char>(c)); });
            std::string normalized = is_ws ? " " : token;
            if (is_ws && cursor_x <= start_x) continue;

            auto metrics = engine_measure_text(engine, normalized.c_str(), &item.style, 0.f);

            if (!is_ws && cursor_x > start_x && cursor_x + metrics.width > start_x + available_width) {
                finish_line(false);
            }

            auto* fragment = new aether_layout_box_s();
            fragment->kind = LAYOUT_BOX_TEXT;
            fragment->element = item.element;
            fragment->style = item.style;
            fragment->text = normalized;
            fragment->content_box = { cursor_x, cursor_y, metrics.width, metrics.height };
            fragment->padding_box = fragment->content_box;
            fragment->border_box  = fragment->content_box;
            fragment->margin_box  = fragment->content_box;
            parent_box->add_child(fragment);
            result->paint_order.push_back(fragment);
            cursor_x += metrics.width;
            line_height = std::max(line_height, metrics.height);
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
    // Collect visible element children
    std::vector<aether_node_s*> children;
    for (auto* node : parent->children) {
        if (node->type != AETHER_NODE_ELEMENT) continue;
        auto cs = get_style(styles, node);
        if (cs.display != AETHER_DISPLAY_NONE && cs.visible && !is_out_of_flow(cs)) {
            children.push_back(node);
        }
    }

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

    // ── flex-direction: column ──
    if (style.flex_direction == AETHER_FLEX_DIRECTION_COLUMN) {
        std::vector<aether_layout_box_s*> boxes;
        float total_height = 0.f;
        for (auto* child : children) {
            auto* child_box = layout_element(engine, styles, child,
                                             {content.x, content.y, content.width, content.height},
                                             content.y, style.align_items == AETHER_ALIGN_STRETCH,
                                             0, false, 0, false, result);
            boxes.push_back(child_box);
            total_height += child_box->margin_box.height;
        }
        total_height += style.gap * std::max(0, static_cast<int>(boxes.size()) - 1);
        float column_free = has_definite_height ? std::max(0.f, content.height - total_height) : 0.f;
        float column_spacing = style.gap;
        float cursor_y = content.y;
        if (style.justify_content == AETHER_JUSTIFY_CENTER) cursor_y = content.y + column_free / 2;
        else if (style.justify_content == AETHER_JUSTIFY_END) cursor_y = content.y + column_free;
        if (style.justify_content == AETHER_JUSTIFY_SPACE_BETWEEN && boxes.size() > 1) {
            column_spacing += column_free / static_cast<float>(boxes.size() - 1);
        }
        for (auto* child_box : boxes) {
            float x = content.x;
            if (style.align_items == AETHER_ALIGN_CENTER) x = content.x + (content.width - child_box->margin_box.width) / 2;
            else if (style.align_items == AETHER_ALIGN_END) x = content.x + content.width - child_box->margin_box.width;
            offset_box(child_box, x - child_box->margin_box.x, cursor_y - child_box->margin_box.y);
            parent_box->add_child(child_box);
            cursor_y += child_box->margin_box.height + column_spacing;
        }
        return total_height;
    }

    // ── flex-direction: row (with optional wrap) ──
    bool stretch_row = !style.flex_wrap && has_definite_height && style.align_items == AETHER_ALIGN_STRETCH;

    // Base pass: measure every child at natural (pre-grow) size
    std::vector<aether_layout_box_s*> base_boxes;
    for (auto* child : children) {
        auto cs = get_style(styles, child);
        float stretch_h = (stretch_row && cs.height.unit == AETHER_CSS_UNIT_AUTO) ? content.height : NAN;
        auto* b = layout_element(engine, styles, child,
                                 {content.x, content.y, content.width, content.height},
                                 content.y, false, 0, false,
                                 stretch_h, std::isfinite(stretch_h), result);
        base_boxes.push_back(b);
    }

    // Split into lines (wrap)
    std::vector<std::vector<int>> lines;
    if (style.flex_wrap) {
        std::vector<int> current;
        float running_width = 0.f;
        for (int i = 0; i < static_cast<int>(children.size()); i++) {
            float item_w = base_boxes[i]->margin_box.width;
            if (!current.empty() && running_width + style.gap + item_w > content.width) {
                lines.push_back(current);
                current.clear();
                running_width = 0.f;
            }
            current.push_back(i);
            running_width += (current.size() > 1 ? style.gap : 0) + item_w;
        }
        if (!current.empty()) lines.push_back(current);
    } else {
        std::vector<int> all;
        for (int i = 0; i < static_cast<int>(children.size()); i++) all.push_back(i);
        lines.push_back(all);
    }

    float row_cursor_y = content.y;
    float row_total_height = 0.f;

    for (auto& line : lines) {
        std::vector<aether_layout_box_s*> line_boxes;
        float line_width = 0.f;
        float line_max_height = 0.f;
        for (int i : line) {
            line_boxes.push_back(base_boxes[i]);
            line_width += base_boxes[i]->margin_box.width;
            line_max_height = std::max(line_max_height, base_boxes[i]->margin_box.height);
        }
        line_width += style.gap * std::max(0, static_cast<int>(line.size()) - 1);

        // flex-grow pass
        float grow_total = 0.f;
        for (int i : line) {
            auto cs = get_style(styles, children[i]);
            if (cs.flex_grow > 0 && cs.width.unit == AETHER_CSS_UNIT_AUTO) {
                grow_total += cs.flex_grow;
            }
        }
        if (grow_total > 0) {
            float growable = std::max(0.f, content.width - line_width);
            if (growable > 0) {
                for (int li = 0; li < static_cast<int>(line.size()); li++) {
                    int i = line[li];
                    auto cs = get_style(styles, children[i]);
                    if (cs.flex_grow <= 0 || cs.width.unit != AETHER_CSS_UNIT_AUTO) continue;
                    float extra = growable * (cs.flex_grow / grow_total);
                    float stretch_h = (stretch_row && cs.height.unit == AETHER_CSS_UNIT_AUTO) ? content.height : NAN;
                    auto* grown = layout_element(engine, styles, children[i],
                                                 {content.x, content.y, content.width, content.height},
                                                 content.y, false,
                                                 line_boxes[li]->border_box.width + extra, true,
                                                 stretch_h, std::isfinite(stretch_h), result);
                    line_boxes[li] = grown;
                    line_max_height = std::max(line_max_height, grown->margin_box.height);
                }
                line_width = content.width;
            }
        }

        float free = std::max(0.f, content.width - line_width);
        float spacing = style.gap;
        float cursor_x = content.x;
        if (style.justify_content == AETHER_JUSTIFY_CENTER) cursor_x = content.x + free / 2;
        else if (style.justify_content == AETHER_JUSTIFY_END) cursor_x = content.x + free;
        if (style.justify_content == AETHER_JUSTIFY_SPACE_BETWEEN && line_boxes.size() > 1) {
            spacing += free / static_cast<float>(line_boxes.size() - 1);
        }

        float cross_extent = (lines.size() == 1 && has_definite_height) ? content.height : line_max_height;
        for (auto* child_box : line_boxes) {
            float y = row_cursor_y;
            if (style.align_items == AETHER_ALIGN_CENTER) y = row_cursor_y + (cross_extent - child_box->margin_box.height) / 2;
            else if (style.align_items == AETHER_ALIGN_END) y = row_cursor_y + cross_extent - child_box->margin_box.height;
            offset_box(child_box, cursor_x - child_box->margin_box.x, y - child_box->margin_box.y);
            parent_box->add_child(child_box);
            cursor_x += child_box->margin_box.width + spacing;
        }

        row_cursor_y += line_max_height + style.gap;
        row_total_height += line_max_height;
    }

    row_total_height += style.gap * std::max(0, static_cast<int>(lines.size()) - 1);
    return has_definite_height ? std::max(row_total_height, content.height) : row_total_height;
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
            } else if (is_atomic_control(node) || child_style.display == AETHER_DISPLAY_INLINE_BLOCK) {
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
        output.push_back(inline_item_s::text_item(owner, style, text));
    }
}

/* ── MeasureIntrinsicWidth ─────────────────────────────────────────── */

static float measure_intrinsic_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result)
{
    if (element->tag_name == "input") {
        return std::min(maximum, 180.f + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border));
    }
    if (style.display == AETHER_DISPLAY_FLEX) {
        return measure_intrinsic_flex_width(engine, styles, element, style, padding, border, maximum, result);
    }

    // A block item's intrinsic width is the widest visible child. Returning just its
    // padding here makes a flex item with nested markup measure as zero-width, which
    // collapses headings and labels next to a sibling button.
    float child_content_width = 0.f;
    bool has_element_children = false;
    for (auto* c : element->children) {
        if (c->type != AETHER_NODE_ELEMENT) continue;
        auto child_style = get_style(styles, c);
        if (child_style.display == AETHER_DISPLAY_NONE || !child_style.visible) continue;
        has_element_children = true;
        child_content_width = std::max(child_content_width,
            measure_child_natural_width(engine, styles, c, maximum, result));
    }

    if (has_element_children) {
        return child_content_width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    }

    std::string text = normalize_whitespace(text_content(element));
    auto metrics = engine_measure_text(engine, text.c_str(), &style, 0.f);
    return metrics.width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
}

static float measure_intrinsic_flex_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result)
{
    std::vector<aether_node_s*> children;
    for (auto* c : element->children) {
        if (c->type != AETHER_NODE_ELEMENT) continue;
        auto cs = get_style(styles, c);
        if (cs.display != AETHER_DISPLAY_NONE && cs.visible) children.push_back(c);
    }
    if (children.empty()) {
        std::string text = normalize_whitespace(text_content(element));
        auto metrics = engine_measure_text(engine, text.c_str(), &style, 0.f);
        return metrics.width + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
    }

    std::vector<float> child_widths;
    for (auto* child : children) {
        child_widths.push_back(measure_child_natural_width(engine, styles, child, maximum, result));
    }
    float content = 0.f;
    if (style.flex_direction == AETHER_FLEX_DIRECTION_COLUMN) {
        content = *std::max_element(child_widths.begin(), child_widths.end());
    } else {
        content = 0.f;
        for (float w : child_widths) content += w;
        content += style.gap * std::max(0, static_cast<int>(children.size()) - 1);
    }
    return content + aether_thicknessf_horizontal(padding) + aether_thicknessf_horizontal(border);
}

static float measure_child_natural_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* child, float maximum,
    aether_layout_result_s* result)
{
    auto child_style = get_style(styles, child);
    auto child_margin = resolve_margin_edges(engine, child_style.margin, maximum, child_style);
    float specified = resolve(engine, child_style.width, maximum, child_style);
    auto child_padding = resolve_edges(engine, child_style.padding, maximum, child_style);
    auto child_border  = resolve_edges(engine, child_style.border_width, maximum, child_style);

    float width;
    if (std::isfinite(specified)) {
        width = (child_style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? specified + aether_thicknessf_horizontal(child_padding) + aether_thicknessf_horizontal(child_border)
            : specified;
    } else {
        width = measure_intrinsic_width(engine, styles, child, child_style,
                                        child_padding, child_border, maximum, result);
    }
    return width + aether_thicknessf_horizontal(child_margin);
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

    // Find root element (using C API from document_node.cpp)
    aether_node_s* root_el = aether_doc_get_body(doc);
    if (!root_el) root_el = aether_doc_get_document_element(doc);
    if (!root_el) { *out_result = result; return AETHER_OK; } // empty doc

    auto root_style = get_style(styles, root_el);
    engine->root_font_size = root_style.font_size;

    auto* root_box = layout_element(engine, styles, root_el,
                                    {0, 0, viewport_width, viewport_height},
                                    0, true,
                                    0, false, 0, false, result);
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
