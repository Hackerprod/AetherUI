/* ── DisplayListBuilder — faithful port from DisplayListBuilder.cs ── */
#include "display_list_types.h"
#include "../layout/layout_types.h"
#include "../layout/form_controls.h"
#include "../abi/aether_abi.h"
#include "../dom/dom_types.h"
#include "../css/border_radii.h"
#include "border_painter.h"
#include "background_image_geometry.h"
#include "transform_matrix.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

/* ─────────────────────────── helpers ────────────────────────────── */

static bool rect_is_empty(aether_rectf r) {
    return r.width <= 0 || r.height <= 0;
}

static void emit_fill_rounded_rect(std::vector<aether_paint_command_t>& cmds,
                                   aether_rectf rect, aether_corner_radii radii, aether_color_rgba color);

static aether_color_rgba apply_opacity(aether_color_rgba color, float opacity) {
    color.a = std::clamp(color.a * std::clamp(opacity, 0.f, 1.f), 0.f, 1.f);
    return color;
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
                                    aether_rectf rect, aether_corner_radii radii, aether_color_rgba color) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_FILL_ROUNDED_RECT;
    cmd.data.fill_rounded_rect = {rect, radii, color};
    cmds.push_back(cmd);
}

static void emit_fill_rounded_rect_gradient(std::vector<aether_paint_command_t>& cmds,
                                             aether_rectf rect, aether_corner_radii radii,
                                             const aether_linear_gradient& gradient, float opacity) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT;
    cmd.data.fill_rounded_rect_gradient = {rect, radii, copy_gradient(gradient), opacity};
    cmds.push_back(cmd);
}

static void emit_draw_background_image(std::vector<aether_paint_command_t>& cmds, aether_rectf rect,
                                       const aether_background_layer& layer, float opacity,
                                       aether_rectf source_rect = {}, aether_rectf destination_rect = {},
                                       aether_bool_t has_resolved_geometry = AETHER_FALSE) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_DRAW_BACKGROUND_IMAGE;
    cmd.data.draw_background_image.source = aether::strdup(layer.image_source);
    cmd.data.draw_background_image.paint_rect = rect;
    cmd.data.draw_background_image.position_x = layer.position_x;
    cmd.data.draw_background_image.position_y = layer.position_y;
    cmd.data.draw_background_image.size_kind = layer.size_kind;
    cmd.data.draw_background_image.size_x = layer.size_x;
    cmd.data.draw_background_image.size_y = layer.size_y;
    cmd.data.draw_background_image.repeat_x = layer.repeat_x;
    cmd.data.draw_background_image.repeat_y = layer.repeat_y;
    cmd.data.draw_background_image.opacity = opacity;
    cmd.data.draw_background_image.source_rect = source_rect;
    cmd.data.draw_background_image.destination_rect = destination_rect;
    cmd.data.draw_background_image.has_resolved_geometry = has_resolved_geometry;
    cmds.push_back(cmd);
}

static void emit_apply_backdrop_filter(std::vector<aether_paint_command_t>& cmds,
                                       aether_rectf rect, aether_corner_radii radii,
                                       const aether_backdrop_filter_list& filters) {
    if (filters.count <= 0 || !filters.items) return;
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_APPLY_BACKDROP_FILTER;
    cmd.data.apply_backdrop_filter.rect = rect;
    cmd.data.apply_backdrop_filter.radii = radii;
    if (aether_backdrop_filter_list_copy(&filters, &cmd.data.apply_backdrop_filter.filters) != AETHER_TRUE) return;
    cmds.push_back(cmd);
}

static void emit_draw_box_shadow(std::vector<aether_paint_command_t>& cmds,
                                  aether_rectf rect, aether_corner_radii radii, aether_shadow shadow) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_DRAW_BOX_SHADOW;
    cmd.data.draw_box_shadow = {rect, radii, shadow};
    cmds.push_back(cmd);
}

static void emit_stroke_rounded_rect(std::vector<aether_paint_command_t>& cmds,
                                      aether_rectf rect, aether_corner_radii radii, float stroke_width, aether_color_rgba color) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_STROKE_ROUNDED_RECT;
    cmd.data.stroke_rounded_rect = {rect, radii, stroke_width, color};
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

static void emit_draw_text_shadow(std::vector<aether_paint_command_t>& cmds,
                                  const char* text, aether_rectf rect,
                                  const char* font_family, float font_size, int32_t font_weight,
                                  int32_t text_align, aether_shadow shadow, bool wrap) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_DRAW_TEXT_SHADOW;
    cmd.data.draw_text_shadow.text = aether::strdup(text);
    cmd.data.draw_text_shadow.rect = rect;
    strncpy(cmd.data.draw_text_shadow.font_family, font_family, sizeof(cmd.data.draw_text_shadow.font_family) - 1);
    cmd.data.draw_text_shadow.font_size = font_size;
    cmd.data.draw_text_shadow.font_weight = font_weight;
    cmd.data.draw_text_shadow.text_align = text_align;
    cmd.data.draw_text_shadow.shadow = shadow;
    cmd.data.draw_text_shadow.wrap = wrap ? AETHER_TRUE : AETHER_FALSE;
    cmds.push_back(cmd);
}

static void emit_text_shadows(std::vector<aether_paint_command_t>& cmds,
                              const aether_computed_style_t& style,
                              const char* text, aether_rectf rect,
                              int32_t text_align, bool wrap) {
    const auto& shadows = style.text_shadows;
    if (shadows.count <= 0 || !shadows.items) return;

    // Painter's order is back-to-front. The first CSS shadow is closest to
    // the glyph and therefore has to be emitted last among the shadows.
    for (int32_t index = shadows.count - 1; index >= 0; --index) {
        auto shadow = shadows.items[index];
        shadow.color = apply_opacity(shadow.color, style.opacity);
        emit_draw_text_shadow(cmds, text, rect, style.font_family, style.font_size,
                              style.font_weight, text_align, shadow, wrap);
    }
}

static void emit_push_clip(std::vector<aether_paint_command_t>& cmds, aether_rectf rect, aether_corner_radii radii) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_PUSH_CLIP;
    cmd.data.push_clip = {rect, radii};
    cmds.push_back(cmd);
}

static void emit_pop_clip(std::vector<aether_paint_command_t>& cmds) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_POP_CLIP;
    cmds.push_back(cmd);
}

static void emit_push_transform(std::vector<aether_paint_command_t>& cmds, aether_transform_matrix matrix) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_PUSH_TRANSFORM;
    cmd.data.push_transform = {matrix};
    cmds.push_back(cmd);
}

static void emit_pop_transform(std::vector<aether_paint_command_t>& cmds) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_POP_TRANSFORM;
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

static void paint_background_and_border(aether_display_list_builder_s* builder, aether_layout_box_s* box,
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

    // CSS transforms are post-layout and scope over the element's own paint
    // plus every descendant. Keeping this as Display List structure avoids
    // baking geometry into commands or duplicating matrix math in bindings.
    // Anonymous text fragments borrow typographic style from their owner but
    // are not independently transformable CSS elements. Their owning element
    // already brackets them, so emitting another scope here would apply the
    // same transform twice.
    const bool has_transform = box->kind == LAYOUT_BOX_ELEMENT &&
        style.transforms.count > 0 && style.transforms.items;
    if (has_transform) {
        emit_push_transform(cmds, aether_transform_matrix_resolve(
            style.transforms, style.transform_origin, box->border_box,
            style.font_size, builder->root_font_size, builder->viewport));
    }

    // Text box → emit text command and return
    if (box->kind == LAYOUT_BOX_TEXT) {
        if (!box->text.empty()) {
            emit_text_shadows(cmds, style, box->text.c_str(), box->content_box,
                              AETHER_TEXT_ALIGN_START, false);
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
        if (has_transform) emit_pop_transform(cmds);
        return;
    }

    // Box shadows (paint behind background)
    paint_box_shadows(box, cmds);

    // Background fill + border stroke
    paint_background_and_border(builder, box, cmds);

    // Clip for overflow:hidden/scroll
    bool is_scroll_container = style.overflow_y == AETHER_OVERFLOW_AUTO || style.overflow_y == AETHER_OVERFLOW_SCROLL;
    bool clips = style.overflow_x != AETHER_OVERFLOW_VISIBLE || style.overflow_y != AETHER_OVERFLOW_VISIBLE;
    if (clips) {
        const auto outer = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size);
        const auto clip = aether_corner_radii_inset(outer,
            box->padding_box.x - box->border_box.x, box->padding_box.y - box->border_box.y,
            box->border_box.x + box->border_box.width - (box->padding_box.x + box->padding_box.width),
            box->border_box.y + box->border_box.height - (box->padding_box.y + box->padding_box.height));
        emit_push_clip(cmds, box->padding_box, clip);
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
        auto focus_radii = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size);
        float* values = &focus_radii.top_left_x;
        for (int i = 0; i < 8; ++i) values[i] += 2.f;
        aether_rectf inflated = aether_rectf_inflate(box->border_box, 2);
        emit_stroke_rounded_rect(cmds, inflated, focus_radii, 1,
                                 {0.145f, 0.388f, 0.922f, 0.75f});
    }

    if (has_transform) emit_pop_transform(cmds);
}

/* ── paint_box_shadows ────────────────────────────────────────────── */

static void paint_box_shadows(aether_layout_box_s* box,
                               std::vector<aether_paint_command_t>& cmds)
{
    const auto& shadows = box->style.box_shadows;
    if (shadows.count <= 0 || !shadows.items) return;

    const auto radii = aether_border_radii_resolve(box->style.border_radii, box->border_box, box->style.font_size);
    // CSS paints the first declared shadow closest to the element. Commands
    // are painter's order, so emit the declaration list back-to-front.
    for (int32_t index = shadows.count - 1; index >= 0; --index) {
        const auto& shadow = shadows.items[index];
        // Inset shadow needs an interior mask and remains deliberately absent
        // from the outer-shadow paint path until that renderer exists.
        if (shadow.inset == AETHER_TRUE) continue;
        emit_draw_box_shadow(cmds, box->border_box, radii, shadow);
    }
}

/* ── paint_background_and_border ──────────────────────────────────── */

static void paint_background_and_border(aether_display_list_builder_s* builder, aether_layout_box_s* box,
                                         std::vector<aether_paint_command_t>& cmds)
{
    auto& style = box->style;
    const auto radii = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size);

    // Flat Display List v1 semantics: this snapshots exactly the commands already
    // painted before this box, never its own background, border, or descendants.
    emit_apply_backdrop_filter(cmds, box->border_box, radii, style.backdrop_filters);

    auto bg = apply_opacity(style.background_color, style.opacity);
    if (bg.a > 0) {
        emit_fill_rounded_rect(cmds, box->border_box, radii, bg);
    }
    // CSS paints the first declared background layer on top. Emit the list in reverse so the
    // flat display list preserves that order above the already-painted background color.
    if (style.background_layers.items) {
        for (int32_t index = style.background_layers.count - 1; index >= 0; --index) {
            const auto& layer = style.background_layers.items[index];
            if (layer.kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT) {
                emit_fill_rounded_rect_gradient(cmds, box->border_box, radii, layer.gradient, style.opacity);
            } else if (layer.kind == AETHER_BACKGROUND_LAYER_IMAGE_URL && layer.image_source) {
                aether_sizef image_size{};
                if (builder->image_dimensions &&
                    builder->image_dimensions(layer.image_source, &image_size, builder->image_dimensions_data) == AETHER_TRUE) {
                    aether_background_image_geometry_context_s context{};
                    context.paint_rect = box->border_box;
                    context.image_size = image_size;
                    context.position_x = layer.position_x;
                    context.position_y = layer.position_y;
                    context.size_kind = layer.size_kind;
                    context.size_x = layer.size_x;
                    context.size_y = layer.size_y;
                    context.repeat_x = layer.repeat_x;
                    context.repeat_y = layer.repeat_y;
                    context.font_size = style.font_size;
                    context.root_font_size = builder->root_font_size;
                    context.viewport_width = builder->viewport.width;
                    context.viewport_height = builder->viewport.height;
                    std::vector<aether_background_image_tile_s> tiles;
                    if (aether_background_image_resolve_geometry(context, tiles)) {
                        for (const auto& tile : tiles) {
                            emit_draw_background_image(cmds, box->border_box, layer, style.opacity,
                                                       tile.source, tile.destination, AETHER_TRUE);
                        }
                        continue;
                    }
                }
                // Pending or failed resources retain the declarative command so the binding can
                // request it; the renderer intentionally paints no unresolved command.
                emit_draw_background_image(cmds, box->border_box, layer, style.opacity);
            }
        }
    }

    aether_paint_border(style, box->border_box, radii, cmds);
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
    const float radius = thumb_width / 2;
    emit_fill_rounded_rect(cmds, thumb_rect, {radius, radius, radius, radius, radius, radius, radius, radius}, {0.5f, 0.5f, 0.5f, 0.55f});
}

/* ── paint_control (opaque input text + caret) ────────────────────── */

static void paint_control(aether_display_list_builder_s* builder,
                           aether_layout_box_s* box,
                           aether_node_s* focused_element,
                           std::vector<aether_paint_command_t>& cmds)
{
    auto* element = box->element;
    auto& style = box->style;

    const std::string displayed = aether_opaque_control_display_text(element, style);
    // Buttons are painted through the child fragments that layout created, so
    // sizing and paint use the exact same line breaks.
    auto val_it = element->attributes.find("value");
    std::string value = (val_it != element->attributes.end()) ? val_it->second : std::string();
    const bool shows_placeholder = value.empty();
    aether_color_rgba color = !shows_placeholder
        ? style.color
        : aether_color_rgba{0.42f, 0.45f, 0.5f, 1.0f};

    emit_text_shadows(cmds, style, displayed.c_str(), box->content_box, style.text_align, false);
    emit_draw_text(cmds, displayed.c_str(), box->content_box,
                   style.font_family, style.font_size, style.font_weight,
                   style.text_align,
                   apply_opacity(color, style.opacity), false);

    // Caret for focused input
    if (element == focused_element && builder->text_measurer) {
        auto metrics = builder->text_measurer(displayed.c_str(), &style,
                                             box->content_box.width,
                                             builder->text_measurer_data);
        float caret_x = std::min(box->content_box.x + box->content_box.width - 1,
                                 box->content_box.x + metrics.width + 1);
        float caret_top = box->content_box.y
            + std::max(1.f, (box->content_box.height - style.font_size * 1.15f) / 2);
        float caret_h = std::min(box->content_box.height - 2, style.font_size * 1.15f);
        emit_fill_rounded_rect(cmds,
                               {caret_x, caret_top, 1, std::max(1.f, caret_h)},
                               {}, apply_opacity(style.color, style.opacity));
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

AETHER_API void aether_display_list_builder_set_image_dimensions_callback(
    aether_display_list_builder_t builder, aether_image_dimensions_fn callback, void* user_data) {
    if (!builder) return;
    builder->image_dimensions = callback;
    builder->image_dimensions_data = user_data;
}

AETHER_API aether_status_t aether_display_list_build(
    aether_display_list_builder_t builder, aether_layout_result_t layout,
    aether_node_t focused_element, aether_display_list_t* out_list)
{
    if (!builder || !layout || !out_list) return AETHER_ERROR_NULL_ARG;
    if (!layout->root) return AETHER_ERROR_INVALID_STATE;

    builder->viewport = layout->viewport;
    builder->root_font_size = layout->root->style.font_size > 0.f ? layout->root->style.font_size : 16.f;

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
            out_cmd->data.fill_rounded_rect_gradient.radii = src.data.fill_rounded_rect_gradient.radii;
            out_cmd->data.fill_rounded_rect_gradient.opacity = src.data.fill_rounded_rect_gradient.opacity;
            out_cmd->data.fill_rounded_rect_gradient.gradient = copy_gradient(src.data.fill_rounded_rect_gradient.gradient);
            break;
        case AETHER_PAINT_STROKE_ROUNDED_RECT:
            out_cmd->data.stroke_rounded_rect = src.data.stroke_rounded_rect;
            break;
        case AETHER_PAINT_FILL_BORDER_SIDE:
            out_cmd->data.fill_border_side = src.data.fill_border_side;
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
        case AETHER_PAINT_DRAW_TEXT_SHADOW:
            out_cmd->data.draw_text_shadow.text = aether::strdup(src.data.draw_text_shadow.text);
            out_cmd->data.draw_text_shadow.rect = src.data.draw_text_shadow.rect;
            strncpy(out_cmd->data.draw_text_shadow.font_family, src.data.draw_text_shadow.font_family,
                    sizeof(out_cmd->data.draw_text_shadow.font_family) - 1);
            out_cmd->data.draw_text_shadow.font_size = src.data.draw_text_shadow.font_size;
            out_cmd->data.draw_text_shadow.font_weight = src.data.draw_text_shadow.font_weight;
            out_cmd->data.draw_text_shadow.text_align = src.data.draw_text_shadow.text_align;
            out_cmd->data.draw_text_shadow.shadow = src.data.draw_text_shadow.shadow;
            out_cmd->data.draw_text_shadow.wrap = src.data.draw_text_shadow.wrap;
            break;
        case AETHER_PAINT_PUSH_CLIP:
            out_cmd->data.push_clip = src.data.push_clip;
            break;
        case AETHER_PAINT_DRAW_BACKGROUND_IMAGE:
            out_cmd->data.draw_background_image = src.data.draw_background_image;
            out_cmd->data.draw_background_image.source = aether::strdup(src.data.draw_background_image.source);
            break;
        case AETHER_PAINT_APPLY_BACKDROP_FILTER:
            out_cmd->data.apply_backdrop_filter.rect = src.data.apply_backdrop_filter.rect;
            out_cmd->data.apply_backdrop_filter.radii = src.data.apply_backdrop_filter.radii;
            (void)aether_backdrop_filter_list_copy(&src.data.apply_backdrop_filter.filters,
                                                   &out_cmd->data.apply_backdrop_filter.filters);
            break;
        case AETHER_PAINT_PUSH_TRANSFORM:
            out_cmd->data.push_transform = src.data.push_transform;
            break;
        case AETHER_PAINT_POP_CLIP:
        case AETHER_PAINT_POP_TRANSFORM:
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
        case AETHER_PAINT_DRAW_TEXT_SHADOW:
            free(cmd->data.draw_text_shadow.text);
            cmd->data.draw_text_shadow.text = nullptr;
            break;
        case AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT:
            aether_linear_gradient_free(&cmd->data.fill_rounded_rect_gradient.gradient);
            break;
        case AETHER_PAINT_DRAW_BACKGROUND_IMAGE:
            free(cmd->data.draw_background_image.source);
            cmd->data.draw_background_image.source = nullptr;
            break;
        case AETHER_PAINT_APPLY_BACKDROP_FILTER:
            aether_backdrop_filter_list_free(&cmd->data.apply_backdrop_filter.filters);
            break;
        default: break;
    }
}

} // extern "C"
