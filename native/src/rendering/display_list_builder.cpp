/* ── DisplayListBuilder — faithful port from DisplayListBuilder.cs ── */
#include "display_list_types.h"
#include "../layout/layout_types.h"
#include "../abi/aether_abi.h"
#include "../dom/dom_types.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

/* ─────────────────────────── helpers ────────────────────────────── */

static bool rect_is_empty(aether_rectf r) {
    return r.width <= 0 || r.height <= 0;
}

static float resolve_border_radius(aether_css_length radius, aether_rectf border_box, float font_size) {
    float ref = std::min(border_box.width, border_box.height);
    float r = aether_css_length_resolve(radius, ref, font_size, font_size, 0, 0);
    return std::max(0.f, r);
}

static aether_color_rgba apply_opacity(aether_color_rgba color, float opacity) {
    color.a = std::clamp(color.a * std::clamp(opacity, 0.f, 1.f), 0.f, 1.f);
    return color;
}

static float resolve_uniform_border(aether_css_edges edges, float reference, float font_size) {
    auto resolve_one = [&](aether_css_length value) -> float {
        float r = aether_css_length_resolve(value, reference, font_size, font_size, reference, reference);
        return std::isfinite(r) ? std::max(0.f, r) : 0.f;
    };
    return std::max({ resolve_one(edges.left), resolve_one(edges.top),
                      resolve_one(edges.right), resolve_one(edges.bottom) });
}

static std::string normalize_whitespace(const std::string& value) {
    std::string result;
    bool prev_space = true;
    for (char c : value) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!prev_space) { result += ' '; prev_space = true; }
        } else {
            result += c;
            prev_space = false;
        }
    }
    if (!result.empty() && result.back() == ' ') result.pop_back();
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

/* ── gradient/shadow copy helpers ─────────────────────────────────── */

static aether_linear_gradient copy_gradient(const aether_linear_gradient& src) {
    aether_linear_gradient g = src;
    if (src.stop_count > 0 && src.stops) {
        g.stops = static_cast<aether_gradient_stop*>(std::malloc(sizeof(aether_gradient_stop) * src.stop_count));
        if (g.stops) std::memcpy(g.stops, src.stops, sizeof(aether_gradient_stop) * src.stop_count);
    }
    return g;
}

/* ── command emitters ─────────────────────────────────────────────── */

static void emit_fill_rounded_rect(std::vector<aether_paint_command_t>& cmds,
                                    aether_rectf rect, float radius, aether_color_rgba color) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_FILL_ROUNDED_RECT;
    cmd.data.fill_rounded_rect = {rect, radius, color};
    cmds.push_back(cmd);
}

static void emit_fill_rounded_rect_gradient(std::vector<aether_paint_command_t>& cmds,
                                             aether_rectf rect, float radius,
                                             const aether_linear_gradient& gradient, float opacity) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT;
    cmd.data.fill_rounded_rect_gradient = {rect, radius, copy_gradient(gradient), opacity};
    cmds.push_back(cmd);
}

static void emit_draw_box_shadow(std::vector<aether_paint_command_t>& cmds,
                                  aether_rectf rect, float radius, aether_box_shadow shadow) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_DRAW_BOX_SHADOW;
    cmd.data.draw_box_shadow = {rect, radius, shadow};
    cmds.push_back(cmd);
}

static void emit_stroke_rounded_rect(std::vector<aether_paint_command_t>& cmds,
                                      aether_rectf rect, float radius, float stroke_width, aether_color_rgba color) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_STROKE_ROUNDED_RECT;
    cmd.data.stroke_rounded_rect = {rect, radius, stroke_width, color};
    cmds.push_back(cmd);
}

static void emit_draw_text(std::vector<aether_paint_command_t>& cmds,
                            const char* text, aether_rectf rect,
                            const char* font_family, float font_size, int32_t font_weight,
                            int32_t text_align, aether_color_rgba color, bool wrap) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_DRAW_TEXT;
    cmd.data.draw_text.text = aether::strdup(text);
    cmd.data.draw_text.rect = rect;
    strncpy(cmd.data.draw_text.font_family, font_family, sizeof(cmd.data.draw_text.font_family) - 1);
    cmd.data.draw_text.font_size = font_size;
    cmd.data.draw_text.font_weight = font_weight;
    cmd.data.draw_text.text_align = text_align;
    cmd.data.draw_text.color = color;
    cmd.data.draw_text.wrap = wrap ? AETHER_TRUE : AETHER_FALSE;
    cmds.push_back(cmd);
}

static void emit_draw_text_gradient(std::vector<aether_paint_command_t>& cmds,
                                     const char* text, aether_rectf rect,
                                     const char* font_family, float font_size, int32_t font_weight,
                                     int32_t text_align, const aether_linear_gradient& gradient, float opacity) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_DRAW_TEXT_GRADIENT;
    cmd.data.draw_text_gradient.text = aether::strdup(text);
    cmd.data.draw_text_gradient.rect = rect;
    strncpy(cmd.data.draw_text_gradient.font_family, font_family, sizeof(cmd.data.draw_text_gradient.font_family) - 1);
    cmd.data.draw_text_gradient.font_size = font_size;
    cmd.data.draw_text_gradient.font_weight = font_weight;
    cmd.data.draw_text_gradient.text_align = text_align;
    cmd.data.draw_text_gradient.gradient = copy_gradient(gradient);
    cmd.data.draw_text_gradient.opacity = opacity;
    cmds.push_back(cmd);
}

static void emit_push_clip(std::vector<aether_paint_command_t>& cmds, aether_rectf rect, float radius) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_PUSH_CLIP;
    cmd.data.push_clip = {rect, radius};
    cmds.push_back(cmd);
}

static void emit_pop_clip(std::vector<aether_paint_command_t>& cmds) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_POP_CLIP;
    cmds.push_back(cmd);
}

/* ══════════════════════════════════════════════════════════════════════
   DisplayListBuilder — walks LayoutBox tree, emits paint commands
   ══════════════════════════════════════════════════════════════════════ */

static void paint_box(aether_display_list_builder_s* builder,
                      aether_layout_box_s* box,
                      aether_node_s* focused_element,
                      std::vector<aether_paint_command_t>& cmds);

static void paint_box_shadows(aether_layout_box_s* box,
                               std::vector<aether_paint_command_t>& cmds);

static void paint_background_and_border(aether_layout_box_s* box,
                                         std::vector<aether_paint_command_t>& cmds);

static void paint_scrollbar(aether_layout_box_s* box,
                             std::vector<aether_paint_command_t>& cmds);

static void paint_control(aether_display_list_builder_s* builder,
                           aether_layout_box_s* box,
                           aether_node_s* focused_element,
                           std::vector<aether_paint_command_t>& cmds);

/* ── paint_box (core recursive) ───────────────────────────────────── */

static void paint_box(aether_display_list_builder_s* builder,
                      aether_layout_box_s* box,
                      aether_node_s* focused_element,
                      std::vector<aether_paint_command_t>& cmds)
{
    auto& style = box->style;
    if (!style.visible || style.display == AETHER_DISPLAY_NONE || rect_is_empty(box->border_box)) {
        return;
    }

    // Text box → emit text command and return
    if (box->kind == LAYOUT_BOX_TEXT) {
        if (!box->text.empty()) {
            if (style.has_text_gradient) {
                emit_draw_text_gradient(cmds, box->text.c_str(), box->content_box,
                                        style.font_family, style.font_size, style.font_weight,
                                        AETHER_TEXT_ALIGN_START, style.text_gradient, style.opacity);
            } else {
                emit_draw_text(cmds, box->text.c_str(), box->content_box,
                               style.font_family, style.font_size, style.font_weight,
                               AETHER_TEXT_ALIGN_START,
                               apply_opacity(style.color, style.opacity), false);
            }
        }
        return;
    }

    // Box shadows (paint behind background)
    paint_box_shadows(box, cmds);

    // Background fill + border stroke
    paint_background_and_border(box, cmds);

    // Clip for overflow:hidden/scroll
    bool is_scroll_container = (style.overflow == AETHER_OVERFLOW_AUTO || style.overflow == AETHER_OVERFLOW_SCROLL);
    bool clips = (style.overflow == AETHER_OVERFLOW_HIDDEN) || is_scroll_container;
    if (clips) {
        float clip_radius = resolve_border_radius(style.border_radius, box->border_box, style.font_size);
        emit_push_clip(cmds, box->padding_box, clip_radius);
    }

    // Children (or atomic control paint)
    if (box->is_atomic_control) {
        paint_control(builder, box, focused_element, cmds);
    } else {
        for (auto* child : box->children) {
            paint_box(builder, child, focused_element, cmds);
        }
    }

    // Scrollbar
    if (is_scroll_container && box->scrollable_overflow_y > 0) {
        paint_scrollbar(box, cmds);
    }

    if (clips) {
        emit_pop_clip(cmds);
    }

    // Focus ring
    if (box->element == focused_element) {
        float focus_radius = resolve_border_radius(style.border_radius, box->border_box, style.font_size);
        aether_rectf inflated = aether_rectf_inflate(box->border_box, 2);
        emit_stroke_rounded_rect(cmds, inflated, focus_radius + 2, 1,
                                 {0.145f, 0.388f, 0.922f, 0.75f});
    }
}

/* ── paint_box_shadows ────────────────────────────────────────────── */

static void paint_box_shadows(aether_layout_box_s* box,
                               std::vector<aether_paint_command_t>& cmds)
{
    // Native computed_style doesn't have a box shadow list yet (single shadow only).
    if (!box->style.has_box_shadow) return;

    float radius = resolve_border_radius(box->style.border_radius, box->border_box, box->style.font_size);
    emit_draw_box_shadow(cmds, box->border_box, radius, box->style.box_shadow);
}

/* ── paint_background_and_border ──────────────────────────────────── */

static void paint_background_and_border(aether_layout_box_s* box,
                                         std::vector<aether_paint_command_t>& cmds)
{
    auto& style = box->style;
    float radius = resolve_border_radius(style.border_radius, box->border_box, style.font_size);

    if (style.has_background_gradient) {
        emit_fill_rounded_rect_gradient(cmds, box->border_box, radius,
                                         style.background_gradient, style.opacity);
    } else {
        auto bg = apply_opacity(style.background_color, style.opacity);
        if (bg.a > 0) {
            emit_fill_rounded_rect(cmds, box->border_box, radius, bg);
        }
    }

    float bw = resolve_uniform_border(style.border_width, box->border_box.width, style.font_size);
    auto bc = apply_opacity(style.border_color, style.opacity);
    if (bw > 0 && bc.a > 0) {
        float inset = bw / 2;
        aether_rectf rect = {
            box->border_box.x + inset,
            box->border_box.y + inset,
            std::max(0.f, box->border_box.width - bw),
            std::max(0.f, box->border_box.height - bw)
        };
        emit_stroke_rounded_rect(cmds, rect, std::max(0.f, radius - inset), bw, bc);
    }
}

/* ── paint_scrollbar ──────────────────────────────────────────────── */

static void paint_scrollbar(aether_layout_box_s* box,
                             std::vector<aether_paint_command_t>& cmds)
{
    const float thumb_width = 4.f;
    const float margin = 2.f;
    auto track = box->padding_box;
    float track_h = track.height;
    float content_extent = track_h + box->scrollable_overflow_y;
    float thumb_h = std::max(24.f, track_h * (track_h / content_extent));
    float available = std::max(0.f, track_h - thumb_h);
    float progress = box->scrollable_overflow_y > 0
        ? box->scroll_offset_y / box->scrollable_overflow_y : 0.f;
    float thumb_y = track.y + available * progress;
    aether_rectf thumb_rect = {
        track.x + track.width - thumb_width - margin,
        thumb_y, thumb_width, thumb_h
    };
    emit_fill_rounded_rect(cmds, thumb_rect, thumb_width / 2, {0.5f, 0.5f, 0.5f, 0.55f});
}

/* ── paint_control (button text / input text + caret) ─────────────── */

static void paint_control(aether_display_list_builder_s* builder,
                           aether_layout_box_s* box,
                           aether_node_s* focused_element,
                           std::vector<aether_paint_command_t>& cmds)
{
    auto* element = box->element;
    auto& style = box->style;

    if (element->tag_name == "button") {
        std::string text = normalize_whitespace(text_content(element));
        emit_draw_text(cmds, text.c_str(), box->content_box,
                       style.font_family, style.font_size, style.font_weight,
                       AETHER_TEXT_ALIGN_CENTER,
                       apply_opacity(style.color, style.opacity), false);
        return;
    }

    // Input / textarea
    auto val_it = element->attributes.find("value");
    std::string value = (val_it != element->attributes.end()) ? val_it->second : std::string();
    auto ph_it = element->attributes.find("placeholder");
    std::string placeholder = (ph_it != element->attributes.end()) ? ph_it->second : std::string();
    std::string displayed = !value.empty() ? value : placeholder;
    aether_color_rgba color = !value.empty()
        ? style.color
        : aether_color_rgba{0.42f, 0.45f, 0.5f, 1.0f};

    emit_draw_text(cmds, displayed.c_str(), box->content_box,
                   style.font_family, style.font_size, style.font_weight,
                   AETHER_TEXT_ALIGN_START,
                   apply_opacity(color, style.opacity), false);

    // Caret for focused input
    if (element == focused_element && builder->text_measurer) {
        auto metrics = builder->text_measurer(value.c_str(), &style,
                                             box->content_box.width,
                                             builder->text_measurer_data);
        float caret_x = std::min(box->content_box.x + box->content_box.width - 1,
                                 box->content_box.x + metrics.width + 1);
        float caret_top = box->content_box.y
            + std::max(1.f, (box->content_box.height - style.font_size * 1.15f) / 2);
        float caret_h = std::min(box->content_box.height - 2, style.font_size * 1.15f);
        emit_fill_rounded_rect(cmds,
                               {caret_x, caret_top, 1, std::max(1.f, caret_h)},
                               0, apply_opacity(style.color, style.opacity));
    }
}

/* ══════════════════════════════════════════════════════════════════════
   C ABI implementation
   ══════════════════════════════════════════════════════════════════════ */

extern "C" {

AETHER_API aether_status_t aether_display_list_builder_create(
    aether_text_measurer_fn text_measurer, void* text_measurer_data,
    aether_display_list_builder_t* out_builder)
{
    if (!out_builder) return AETHER_ERROR_NULL_ARG;
    auto* b = new(std::nothrow) aether_display_list_builder_s();
    if (!b) return AETHER_ERROR_OUT_OF_MEMORY;
    b->text_measurer = text_measurer;
    b->text_measurer_data = text_measurer_data;
    *out_builder = b;
    return AETHER_OK;
}

AETHER_API void aether_display_list_builder_destroy(aether_display_list_builder_t builder) {
    delete builder;
}

AETHER_API aether_status_t aether_display_list_build(
    aether_display_list_builder_t builder, aether_layout_result_t layout,
    aether_node_t focused_element, aether_display_list_t* out_list)
{
    if (!builder || !layout || !out_list) return AETHER_ERROR_NULL_ARG;
    if (!layout->root) return AETHER_ERROR_INVALID_STATE;

    auto* list = new(std::nothrow) aether_display_list_s();
    if (!list) return AETHER_ERROR_OUT_OF_MEMORY;

    paint_box(builder, layout->root, focused_element, list->commands);
    *out_list = list;
    return AETHER_OK;
}

AETHER_API void aether_display_list_destroy(aether_display_list_t list) {
    delete list;
}

AETHER_API int32_t aether_display_list_get_count(aether_display_list_t list) {
    if (!list) return 0;
    return static_cast<int32_t>(list->commands.size());
}

AETHER_API aether_status_t aether_display_list_get_command(
    aether_display_list_t list, int32_t index, aether_paint_command_t* out_cmd)
{
    if (!list || !out_cmd) return AETHER_ERROR_NULL_ARG;
    std::memset(out_cmd, 0, sizeof(*out_cmd));
    if (index < 0 || index >= static_cast<int32_t>(list->commands.size())) {
        return AETHER_ERROR_INVALID_STATE;
    }

    auto& src = list->commands[index];
    out_cmd->tag = src.tag;

    switch (src.tag) {
        case AETHER_PAINT_FILL_ROUNDED_RECT:
            out_cmd->data.fill_rounded_rect = src.data.fill_rounded_rect;
            break;
        case AETHER_PAINT_DRAW_BOX_SHADOW:
            out_cmd->data.draw_box_shadow = src.data.draw_box_shadow;
            break;
        case AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT:
            out_cmd->data.fill_rounded_rect_gradient.rect = src.data.fill_rounded_rect_gradient.rect;
            out_cmd->data.fill_rounded_rect_gradient.radius = src.data.fill_rounded_rect_gradient.radius;
            out_cmd->data.fill_rounded_rect_gradient.opacity = src.data.fill_rounded_rect_gradient.opacity;
            out_cmd->data.fill_rounded_rect_gradient.gradient = copy_gradient(src.data.fill_rounded_rect_gradient.gradient);
            break;
        case AETHER_PAINT_STROKE_ROUNDED_RECT:
            out_cmd->data.stroke_rounded_rect = src.data.stroke_rounded_rect;
            break;
        case AETHER_PAINT_DRAW_TEXT:
            out_cmd->data.draw_text.text = aether::strdup(src.data.draw_text.text);
            out_cmd->data.draw_text.rect = src.data.draw_text.rect;
            strncpy(out_cmd->data.draw_text.font_family, src.data.draw_text.font_family,
                    sizeof(out_cmd->data.draw_text.font_family) - 1);
            out_cmd->data.draw_text.font_size = src.data.draw_text.font_size;
            out_cmd->data.draw_text.font_weight = src.data.draw_text.font_weight;
            out_cmd->data.draw_text.text_align = src.data.draw_text.text_align;
            out_cmd->data.draw_text.color = src.data.draw_text.color;
            out_cmd->data.draw_text.wrap = src.data.draw_text.wrap;
            break;
        case AETHER_PAINT_DRAW_TEXT_GRADIENT:
            out_cmd->data.draw_text_gradient.text = aether::strdup(src.data.draw_text_gradient.text);
            out_cmd->data.draw_text_gradient.rect = src.data.draw_text_gradient.rect;
            strncpy(out_cmd->data.draw_text_gradient.font_family, src.data.draw_text_gradient.font_family,
                    sizeof(out_cmd->data.draw_text_gradient.font_family) - 1);
            out_cmd->data.draw_text_gradient.font_size = src.data.draw_text_gradient.font_size;
            out_cmd->data.draw_text_gradient.font_weight = src.data.draw_text_gradient.font_weight;
            out_cmd->data.draw_text_gradient.text_align = src.data.draw_text_gradient.text_align;
            out_cmd->data.draw_text_gradient.gradient = copy_gradient(src.data.draw_text_gradient.gradient);
            out_cmd->data.draw_text_gradient.opacity = src.data.draw_text_gradient.opacity;
            break;
        case AETHER_PAINT_PUSH_CLIP:
            out_cmd->data.push_clip = src.data.push_clip;
            break;
        case AETHER_PAINT_POP_CLIP:
            break;
    }
    return AETHER_OK;
}

AETHER_API void aether_paint_command_free(aether_paint_command_t* cmd) {
    if (!cmd) return;
    switch (cmd->tag) {
        case AETHER_PAINT_DRAW_TEXT:
            free(cmd->data.draw_text.text);
            cmd->data.draw_text.text = nullptr;
            break;
        case AETHER_PAINT_DRAW_TEXT_GRADIENT:
            free(cmd->data.draw_text_gradient.text);
            cmd->data.draw_text_gradient.text = nullptr;
            aether_linear_gradient_free(&cmd->data.draw_text_gradient.gradient);
            break;
        case AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT:
            aether_linear_gradient_free(&cmd->data.fill_rounded_rect_gradient.gradient);
            break;
        default: break;
    }
}

} // extern "C"
