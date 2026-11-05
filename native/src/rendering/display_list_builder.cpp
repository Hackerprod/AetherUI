/* ── DisplayListBuilder — faithful port from DisplayListBuilder.cs ── */
#include "display_list_types.h"
#include "../layout/layout_types.h"
#include "../layout/layout_internal.h"
#include "../layout/scrollbar_geometry.h"
#include "../layout/form_controls.h"
#include "../css/style_map_types.h"
#include "../dom/dom_types.h"
#include "../abi/aether_abi.h"
#include "../dom/dom_types.h"
#include "../css/border_radii.h"
#include "../css/linear_gradient.h"
#include "border_painter.h"
#include "background_image_geometry.h"
#include "transform_matrix.h"
#include "stacking.h"

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

static bool resolve_gradient_for_paint(const aether_display_list_builder_s* builder,
                                       const aether_linear_gradient& source,
                                       aether_rectf gradient_rect, float font_size,
                                       aether_linear_gradient* resolved) {
    aether_linear_gradient_resolution_context_s context{};
    context.gradient_rect = gradient_rect;
    context.font_size = font_size;
    context.root_font_size = builder->root_font_size;
    context.viewport_width = builder->viewport.width;
    context.viewport_height = builder->viewport.height;
    return aether_linear_gradient_resolve(source, context, resolved);
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
                                             const aether_linear_gradient& gradient, float opacity,
                                             aether_rectf gradient_rect = {}) {
    aether_paint_command_t cmd{};
    cmd.tag = AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT;
    if (gradient_rect.width <= 0.f || gradient_rect.height <= 0.f) gradient_rect = rect;
    cmd.data.fill_rounded_rect_gradient = {rect, radii, gradient_rect, copy_gradient(gradient), opacity};
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
                              int32_t text_align, bool wrap,
                              float opacity) {
    const auto& shadows = style.text_shadows;
    if (shadows.count <= 0 || !shadows.items) return;

    // Painter's order is back-to-front. The first CSS shadow is closest to
    // the glyph and therefore has to be emitted last among the shadows.
    for (int32_t index = shadows.count - 1; index >= 0; --index) {
        auto shadow = shadows.items[index];
        shadow.color = apply_opacity(shadow.color, opacity);
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
static void emit_push_scroll_offset(std::vector<aether_paint_command_t>& cmds, int64_t id) { aether_paint_command_t c{}; c.tag=AETHER_PAINT_PUSH_SCROLL_OFFSET; c.data.push_scroll_offset={id}; cmds.push_back(c); }
static void emit_pop_scroll_offset(std::vector<aether_paint_command_t>& cmds) { aether_paint_command_t c{}; c.tag=AETHER_PAINT_POP_SCROLL_OFFSET; cmds.push_back(c); }
static void emit_draw_scrollbar_chrome(std::vector<aether_paint_command_t>& cmds, int64_t id) { aether_paint_command_t c{}; c.tag=AETHER_PAINT_DRAW_SCROLLBAR_CHROME; c.data.draw_scrollbar_chrome={id}; cmds.push_back(c); }
static void emit_begin_paint_chunk(std::vector<aether_paint_command_t>& cmds, int64_t id) { if (id <= 0) return; aether_paint_command_t c{}; c.tag=AETHER_PAINT_BEGIN_PAINT_CHUNK; c.data.paint_chunk={id}; cmds.push_back(c); }
static void emit_end_paint_chunk(std::vector<aether_paint_command_t>& cmds, int64_t id) { if (id <= 0) return; aether_paint_command_t c{}; c.tag=AETHER_PAINT_END_PAINT_CHUNK; c.data.paint_chunk={id}; cmds.push_back(c); }

/* ══════════════════════════════════════════════════════════════════════
   DisplayListBuilder — walks LayoutBox tree, emits paint commands
   ══════════════════════════════════════════════════════════════════════ */

static const aether_computed_style_t* style_for_node(const aether_style_map_s* styles,
                                                      const aether_node_s* node) {
    if (!styles || !node) return nullptr;
    const auto found = styles->styles.find(const_cast<aether_node_s*>(node));
    return found == styles->styles.end() ? nullptr : &found->second;
}

static const aether_computed_style_t& style_for_box(const aether_display_list_builder_s* builder,
                                                     const aether_layout_box_s* box) {
    return layout_box_style(box, builder ? builder->current_style_map : nullptr);
}

static void paint_box(aether_display_list_builder_s* builder,
                      aether_layout_box_s* box,
                      aether_node_s* focused_element,
                      const paint_context_s& parent_ctx,
                      std::vector<aether_paint_command_t>& cmds);

static void paint_box_shadows(aether_display_list_builder_s* builder,
                               aether_layout_box_s* box,
                               std::vector<aether_paint_command_t>& cmds);

static void paint_background_and_border(aether_display_list_builder_s* builder, aether_layout_box_s* box,
                                         bool suppress_background, const paint_context_s& ctx,
                                         std::vector<aether_paint_command_t>& cmds);

static bool box_has_scrollbar_chrome(const aether_display_list_builder_s* builder,
                                     aether_layout_box_s* box);

static bool is_scroll_container_axis(int32_t overflow)
{
    return aether_overflow_is_scroll_container(overflow);
}

// The root box represents the viewport scroll: its scroll semantics follow the
// propagated viewport overflow while its own used overflow stays visible.
// Every other box uses its own style.
static int32_t scroll_overflow_axis(const aether_display_list_builder_s* builder,
                                    const aether_layout_box_s* box,
                                    bool horizontal)
{
    if (builder->layout && builder->layout->root == box) {
        return horizontal ? builder->layout->viewport_overflow_x
                          : builder->layout->viewport_overflow_y;
    }
    return horizontal ? box->style.overflow_x : box->style.overflow_y;
}

static bool box_has_dynamic_scroll(const aether_display_list_builder_s* builder,
                                   aether_layout_box_s* box)
{
    return box->element &&
        ((is_scroll_container_axis(scroll_overflow_axis(builder, box, true)) &&
          box->scroll.scrollable_overflow_x > 0.f) ||
         (is_scroll_container_axis(scroll_overflow_axis(builder, box, false)) &&
          box->scroll.scrollable_overflow_y > 0.f));
}

static void paint_control(aether_display_list_builder_s* builder,
                           aether_layout_box_s* box,
                           aether_node_s* focused_element,
                           const paint_context_s& ctx,
                           std::vector<aether_paint_command_t>& cmds);

/* ── paint_children (stacking-aware emission) ─────────────────────── */

/* Emits a container's children. When no element child carries an explicit
   (non-auto) z-index the children paint in exact DOM order — byte-identical
   to the pre-z-index pipeline. Otherwise they are reordered by CSS paint
   class (negative -> in-flow -> auto -> positive), ascending z-index within
   the negative/positive classes, and DOM order everywhere else.
   NOTE: this reorders visual paint only. Hit-testing keeps its own
   back-to-front DOM traversal; z-aware hit-testing is a future slice. */
static void paint_children(aether_display_list_builder_s* builder,
                           aether_layout_box_s* box,
                           aether_node_s* focused_element,
                           const paint_context_s& ctx,
                           std::vector<aether_paint_command_t>& cmds)
{
    using namespace aether::stacking;

    std::vector<stack_item_t> items;
    items.reserve(box->children.size());
    int32_t dom_index = 0;
    for (auto* child : box->children) {
        stack_item_t item;
        item.dom_index = dom_index;
        if (child->kind == LAYOUT_BOX_TEXT || child->is_atomic_control) {
            // Inline content (text runs and atomic inline controls) stays in
            // the IN_FLOW class at its DOM position regardless of styles.
            item.is_inline_content = true;
            item.cls = paint_class_t::in_flow;
        } else {
            const paint_classification_t classification =
                classify_element_paint(style_for_box(builder, child));
            item.cls = classification.cls;
            item.z_value = classification.z_value;
            item.has_explicit_z_index = classification.has_explicit_z_index;
        }
        items.push_back(item);
        ++dom_index;
    }

    if (!needs_stacking_order(items)) {
        for (auto* child : box->children) {
            paint_box(builder, child, focused_element, ctx, cmds);
        }
        return;
    }

    stable_sort_stack_items(items);
    for (const auto& item : items) {
        paint_box(builder, box->children[item.dom_index], focused_element, ctx, cmds);
    }
}

/* ── paint_box (core recursive) ───────────────────────────────────── */

static void paint_box(aether_display_list_builder_s* builder,
                      aether_layout_box_s* box,
                      aether_node_s* focused_element,
                      const paint_context_s& parent_ctx,
                      std::vector<aether_paint_command_t>& cmds)
{
    const auto& style = style_for_box(builder, box);
    if (style.display == AETHER_DISPLAY_NONE) return;
    const bool visible = layout_box_presentation_style(box, builder->current_style_map).visible;
    const bool empty_border_box = rect_is_empty(box->border_box);
    const bool has_paint_chunk = box->kind == LAYOUT_BOX_ELEMENT &&
        box->element && box->element->style_node_id > 0;
    if (has_paint_chunk) emit_begin_paint_chunk(cmds, box->element->style_node_id);

    // Build this box's paint context: the single source of truth for the paint
    // calls below and for what descendants inherit. Live CSS transitions.
    // Opacity overrides apply to any box whose element carries an active
    // transition (element, control, and text fragments all borrow their owning
    // element's opacity), while the transform override is restricted to element
    // boxes: text fragments never emit their own transform bracket. Without a
    // runtime set, this is a byte-identical no-op.
    paint_context_s ctx;
    float own_opacity = style.opacity;
    if (box->element && builder->transition_runtime) {
        (void)aether_transition_runtime_opacity_override(
            builder->transition_runtime, box->element, &own_opacity);
    }
    // CSS opacity applies to the element's whole subtree (it creates a stacking
    // context in Chrome): multiply this box's own opacity (with any live
    // transition override) by the accumulated opacity of its ancestors.
    ctx.effective_opacity = own_opacity * parent_ctx.inherited_opacity;
    ctx.inherited_opacity = ctx.effective_opacity;

    // CSS transforms are post-layout and scope over the element's own paint
    // plus every descendant. Keeping this as Display List structure avoids
    // baking geometry into commands or duplicating matrix math in bindings.
    // Anonymous text fragments borrow typographic style from their owner but
    // are not independently transformable CSS elements. Their owning element
    // already brackets them, so emitting another scope here would apply the
    // same transform twice.
    ctx.has_transform = box->kind == LAYOUT_BOX_ELEMENT &&
        style.transforms.count > 0 && style.transforms.items;
    if (box->kind == LAYOUT_BOX_ELEMENT && box->element && builder->transition_runtime &&
        aether_transition_runtime_transform_override(
            builder->transition_runtime, box->element, &ctx.transform_override) == AETHER_TRUE) {
        // An active transform transition (including to/from `transform: none`)
        // wins over the static resolution so the interpolated matrix is used.
        ctx.has_transform_override = true;
        ctx.has_transform = true;
    } else if (ctx.has_transform) {
        ctx.transform_override = aether_transform_matrix_resolve(
            style.transforms, style.transform_origin, box->border_box,
            style.font_size, builder->root_font_size, builder->viewport);
    }

    if (ctx.has_transform) emit_push_transform(cmds, ctx.transform_override);

    // Text box → emit text command and return
    if (box->kind == LAYOUT_BOX_TEXT) {
        if (visible && !empty_border_box && !box->text.empty()) {
            emit_text_shadows(cmds, style, box->text.c_str(), box->content_box,
                              AETHER_TEXT_ALIGN_START, false, ctx.effective_opacity);
            if (style.has_text_gradient) {
                aether_linear_gradient gradient{};
                if (resolve_gradient_for_paint(builder, style.text_gradient, box->content_box,
                                               style.font_size, &gradient)) {
                    emit_draw_text_gradient(cmds, box->text.c_str(), box->content_box,
                                            style.font_family, style.font_size, style.font_weight,
                                            AETHER_TEXT_ALIGN_START, gradient, ctx.effective_opacity);
                    aether_linear_gradient_free(&gradient);
                }
            } else {
                emit_draw_text(cmds, box->text.c_str(), box->content_box,
                               style.font_family, style.font_size, style.font_weight,
                               AETHER_TEXT_ALIGN_START,
                               apply_opacity(style.color, ctx.effective_opacity), false);
            }
        }
        if (ctx.has_transform) emit_pop_transform(cmds);
        return;
    }

    if (visible && !empty_border_box) {
        // Box shadows (paint behind background)
        paint_box_shadows(builder, box, cmds);

        // Background fill + border stroke
        paint_background_and_border(builder, box, box->element == builder->canvas_background_source,
                                    ctx, cmds);
    }

    bool clips = style.overflow_x != AETHER_OVERFLOW_VISIBLE || style.overflow_y != AETHER_OVERFLOW_VISIBLE;
    if (clips) {
        const auto outer = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size);
        const auto clip = aether_corner_radii_inset(outer,
            box->padding_box.x - box->border_box.x, box->padding_box.y - box->border_box.y,
            box->border_box.x + box->border_box.width - (box->padding_box.x + box->padding_box.width),
            box->border_box.y + box->border_box.height - (box->padding_box.y + box->padding_box.height));
        emit_push_clip(cmds, box->padding_box, clip);
    }

    const bool dynamic_scroll = box_has_dynamic_scroll(builder, box);
    const bool scrollbar_chrome = visible && box->element && !empty_border_box && box_has_scrollbar_chrome(builder, box);
    if (dynamic_scroll) emit_push_scroll_offset(cmds, box->element->style_node_id);
    // Children (or atomic control paint)
    if (box->is_atomic_control) {
        if (visible) paint_control(builder, box, focused_element, ctx, cmds);
    } else {
        paint_children(builder, box, focused_element, ctx, cmds);
    }

    if (dynamic_scroll) emit_pop_scroll_offset(cmds);
    // Scrollbar chrome is retained separately from content offset. Bindings
    // resolve its live thumb position from the same native geometry formula
    // used by hit-testing.
    if (scrollbar_chrome) emit_draw_scrollbar_chrome(cmds, box->element->style_node_id);

    if (clips) {
        emit_pop_clip(cmds);
    }

    // Focus ring
    if (visible && !empty_border_box && box->element == focused_element) {
        auto focus_radii = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size);
        float* values = &focus_radii.top_left_x;
        for (int i = 0; i < 8; ++i) values[i] += 2.f;
        aether_rectf inflated = aether_rectf_inflate(box->border_box, 2);
        emit_stroke_rounded_rect(cmds, inflated, focus_radii, 1,
                                 {0.145f, 0.388f, 0.922f, 0.75f});
    }

    if (ctx.has_transform) emit_pop_transform(cmds);
    if (has_paint_chunk) emit_end_paint_chunk(cmds, box->element->style_node_id);
}

/* ── paint_box_shadows ────────────────────────────────────────────── */

static void paint_box_shadows(aether_display_list_builder_s* builder,
                               aether_layout_box_s* box,
                               std::vector<aether_paint_command_t>& cmds)
{
    const auto& style = style_for_box(builder, box);
    const auto& shadows = style.box_shadows;
    if (shadows.count <= 0 || !shadows.items) return;

    const auto radii = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size);
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

static void paint_background_layers(aether_display_list_builder_s* builder,
                                    const aether_computed_style_t& style,
                                    const aether_rectf& paint_rect,
                                    const aether_corner_radii& radii,
                                    const paint_context_s& ctx,
                                    std::vector<aether_paint_command_t>& cmds)
{
    const auto bg = apply_opacity(style.background_color, ctx.effective_opacity);
    if (bg.a > 0) {
        emit_fill_rounded_rect(cmds, paint_rect, radii, bg);
    }
    // CSS paints the first declared background layer on top. Emit the list in reverse so the
    // flat display list preserves that order above the already-painted background color.
    if (style.background_layers.items) {
        // Background layers are clipped as a group. Besides obeying border-radius, this keeps
        // repeated procedural tiles from assigning the corner radius to every individual tile.
        emit_push_clip(cmds, paint_rect, radii);
        for (int32_t index = style.background_layers.count - 1; index >= 0; --index) {
            const auto& layer = style.background_layers.items[index];
            if (layer.kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT) {
                aether_background_image_geometry_context_s context{};
                context.paint_rect = paint_rect;
                context.has_intrinsic_size = false;
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
                        aether_linear_gradient gradient{};
                        if (!resolve_gradient_for_paint(builder, layer.gradient, tile.tile_rect,
                                                        style.font_size, &gradient)) continue;
                        emit_fill_rounded_rect_gradient(cmds, tile.destination, {}, gradient,
                                                        ctx.effective_opacity, tile.tile_rect);
                        aether_linear_gradient_free(&gradient);
                    }
                }
            } else if (layer.kind == AETHER_BACKGROUND_LAYER_IMAGE_URL && layer.image_source) {
                aether_sizef image_size{};
                if (builder->image_dimensions &&
                    builder->image_dimensions(layer.image_source, &image_size, builder->image_dimensions_data) == AETHER_TRUE) {
                    aether_background_image_geometry_context_s context{};
                    context.paint_rect = paint_rect;
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
                            emit_draw_background_image(cmds, paint_rect, layer, ctx.effective_opacity,
                                                       tile.source, tile.destination, AETHER_TRUE);
                        }
                        continue;
                    }
                }
                // Pending or failed resources retain the declarative command so the binding can
                // request it; the renderer intentionally paints no unresolved command.
                emit_draw_background_image(cmds, paint_rect, layer, ctx.effective_opacity);
            }
        }
        emit_pop_clip(cmds);
    }
}

static void paint_background_and_border(aether_display_list_builder_s* builder, aether_layout_box_s* box,
                                         const bool suppress_background, const paint_context_s& ctx,
                                         std::vector<aether_paint_command_t>& cmds)
{
    const auto& style = style_for_box(builder, box);
    const auto radii = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size);

    // Flat Display List v1 semantics: this snapshots exactly the commands already
    // painted before this box, never its own background, border, or descendants.
    emit_apply_backdrop_filter(cmds, box->border_box, radii, style.backdrop_filters);
    if (!suppress_background) {
        paint_background_layers(builder, style, box->border_box, radii, ctx, cmds);
    }

    aether_paint_border(style, box->border_box, radii, cmds);
}

static bool has_canvas_eligible_background(const aether_computed_style_t& style) {
    return style.background_color.a > 0.f ||
        (style.background_layers.count > 0 && style.background_layers.items != nullptr);
}

static const aether_node_s* document_element_for(const aether_node_s* body) {
    for (auto* node = body ? body->parent : nullptr; node; node = node->parent) {
        if (node->type == AETHER_NODE_ELEMENT && node->tag_name == "html") return node;
    }
    return nullptr;
}

static void paint_canvas_background(aether_display_list_builder_s* builder,
                                    const aether_layout_result_s* layout,
                                    std::vector<aether_paint_command_t>& cmds) {
    const auto* body = layout->root ? layout->root->element : nullptr;
    const auto* html = document_element_for(body);
    const auto* html_style = style_for_node(builder->current_style_map, html);
    const auto* body_style = layout->root ? &style_for_box(builder, layout->root) : nullptr;

    // CSS 2.1 §14.2: the root background paints the canvas.  When it is fully
    // transparent/no-image, body's background is propagated instead and body
    // becomes transparent for its ordinary box paint.
    const aether_computed_style_t* source_style = nullptr;
    const aether_node_s* source_node = nullptr;
    if (html_style && has_canvas_eligible_background(*html_style)) {
        source_style = html_style;
        source_node = html;
    } else if (body_style && has_canvas_eligible_background(*body_style)) {
        source_style = body_style;
        source_node = body;
    }
    if (!source_style) return;

    const aether_rectf canvas{0.f, 0.f, layout->viewport.width, layout->viewport.height};
    const aether_corner_radii square{};
    // Canvas background uses the propagated root style's own opacity; it is not
    // an element box, so transition overrides never apply here.
    paint_context_s ctx;
    ctx.effective_opacity = source_style->opacity;
    paint_background_layers(builder, *source_style, canvas, square, ctx, cmds);
    builder->canvas_background_source = source_node == body ? source_node : nullptr;
}

/* ── paint_scrollbar ──────────────────────────────────────────────── */

static bool box_has_scrollbar_chrome(const aether_display_list_builder_s* builder,
                                     aether_layout_box_s* box)
{
    (void)builder;
    const auto geometry = aether_resolve_scrollbar_geometry(*box);
    return geometry.vertical_visible || geometry.horizontal_visible;
}

/* ── paint_control (opaque input text + caret) ────────────────────── */

static void paint_control(aether_display_list_builder_s* builder,
                           aether_layout_box_s* box,
                           aether_node_s* focused_element,
                           const paint_context_s& ctx,
                           std::vector<aether_paint_command_t>& cmds)
{
    auto* element = box->element;
    const auto& style = style_for_box(builder, box);

    const std::string displayed = aether_opaque_control_display_text(element, style);
    // Buttons are painted through the child fragments that layout created, so
    // sizing and paint use the exact same line breaks.
    auto val_it = element->attributes.find("value");
    std::string value = (val_it != element->attributes.end()) ? val_it->second : std::string();
    const bool shows_placeholder = value.empty();
    aether_color_rgba color = !shows_placeholder
        ? style.color
        : aether_color_rgba{0.42f, 0.45f, 0.5f, 1.0f};

    emit_text_shadows(cmds, style, displayed.c_str(), box->content_box, style.text_align, false,
                      ctx.effective_opacity);
    emit_draw_text(cmds, displayed.c_str(), box->content_box,
                   style.font_family, style.font_size, style.font_weight,
                   style.text_align,
                   apply_opacity(color, ctx.effective_opacity), false);

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
                               {}, apply_opacity(style.color, ctx.effective_opacity));
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

AETHER_API void aether_display_list_builder_set_transition_runtime(
    aether_display_list_builder_t builder, aether_transition_runtime_t runtime) {
    if (!builder) return;
    // The builder borrows the runtime handle; the caller owns its lifetime.
    builder->transition_runtime = runtime;
}

AETHER_API aether_status_t aether_display_list_build(
    aether_display_list_builder_t builder, aether_layout_result_t layout,
    aether_node_t focused_element, aether_display_list_t* out_list)
{
    return aether_display_list_build_with_styles(builder, layout,
                                                 layout ? layout->style_map : nullptr,
                                                 focused_element, out_list);
}

AETHER_API aether_status_t aether_display_list_build_with_styles(
    aether_display_list_builder_t builder, aether_layout_result_t layout,
    aether_style_map_t current_styles, aether_node_t focused_element,
    aether_display_list_t* out_list)
{
    if (!builder || !layout || !out_list) return AETHER_ERROR_NULL_ARG;
    if (!layout->root) return AETHER_ERROR_INVALID_STATE;

    builder->viewport = layout->viewport;
    builder->root_font_size = layout->root->style.font_size > 0.f ? layout->root->style.font_size : 16.f;
    builder->current_style_map = current_styles ? current_styles : layout->style_map;
    builder->layout = layout;

    auto* list = new(std::nothrow) aether_display_list_s();
    if (!list) return AETHER_ERROR_OUT_OF_MEMORY;

    builder->canvas_background_source = nullptr;
    paint_canvas_background(builder, layout, list->commands);
    paint_box(builder, layout->root, focused_element, paint_context_s{}, list->commands);
    builder->canvas_background_source = nullptr;
    builder->current_style_map = nullptr;
    builder->layout = nullptr;
    *out_list = list;
    return AETHER_OK;
}

AETHER_API aether_status_t aether_display_list_build_node_with_styles(
    aether_display_list_builder_t builder,
    aether_layout_result_t layout,
    aether_style_map_t current_styles,
    aether_node_t element,
    aether_node_t focused_element,
    aether_display_list_t* out_list)
{
    if (!builder || !layout || !element || !out_list) return AETHER_ERROR_NULL_ARG;
    if (!layout->root) return AETHER_ERROR_INVALID_STATE;

    builder->viewport = layout->viewport;
    builder->root_font_size = layout->root->style.font_size > 0.f ? layout->root->style.font_size : 16.f;
    builder->current_style_map = current_styles ? current_styles : layout->style_map;
    builder->layout = layout;
    builder->canvas_background_source = nullptr;

    auto* list = new(std::nothrow) aether_display_list_s();
    if (!list) {
        builder->current_style_map = nullptr;
        builder->layout = nullptr;
        return AETHER_ERROR_OUT_OF_MEMORY;
    }

    if (auto* box = layout->find_primary_box(element)) {
        paint_box(builder, box, focused_element, paint_context_s{}, list->commands);
    }

    builder->current_style_map = nullptr;
    builder->layout = nullptr;
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
            out_cmd->data.fill_rounded_rect_gradient.gradient_rect = src.data.fill_rounded_rect_gradient.gradient_rect;
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
        case AETHER_PAINT_PUSH_SCROLL_OFFSET:
            out_cmd->data.push_scroll_offset = src.data.push_scroll_offset;
            break;
        case AETHER_PAINT_DRAW_SCROLLBAR_CHROME:
            out_cmd->data.draw_scrollbar_chrome = src.data.draw_scrollbar_chrome;
            break;
        case AETHER_PAINT_BEGIN_PAINT_CHUNK:
        case AETHER_PAINT_END_PAINT_CHUNK:
            out_cmd->data.paint_chunk = src.data.paint_chunk;
            break;
        case AETHER_PAINT_POP_CLIP:
        case AETHER_PAINT_POP_TRANSFORM:
        case AETHER_PAINT_POP_SCROLL_OFFSET:
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
