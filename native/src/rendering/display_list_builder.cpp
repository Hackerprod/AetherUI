/* ── DisplayListBuilder — faithful port from DisplayListBuilder.cs ── */
#include "display_list_types.h"
#include "../layout/layout_types.h"
#include "../layout/layout_internal.h"
#include "../layout/scrollbar_geometry.h"
#include "../layout/form_controls.h"
#include "../css/style_map_types.h"
#include "../css/frozen_style.h"
#include "../css/style_c_export.h"
#include "../layout/operation_pin.h"
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
#include <stdexcept>
#include <vector>

using aether::style::ComputedStyleReadView;
using aether::style::BackgroundLayerReadView;
using aether::style::GradientReadView;
using aether::style::ReadOnlyArrayView;
using aether::rendering::IndexedReadRange;
using aether::rendering::GradientKernelReadView;
using aether::layout::BoxStylePolicy;

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

struct paint_command_guard_s {
    aether_paint_command_t& command;
    bool owns = true;
    ~paint_command_guard_s() { if (owns) aether_paint_command_free(&command); }
    void release() { owns = false; }
};
struct paint_gradient_guard_s {
    aether_linear_gradient& gradient;
    ~paint_gradient_guard_s() { aether_linear_gradient_free(&gradient); }
};
static char* checked_strdup(const char* text) {
    if (!text) return nullptr;
    auto* copy = aether::strdup(text);
    if (!copy) throw std::bad_alloc();
    return copy;
}

static aether_linear_gradient copy_gradient(const aether_linear_gradient& src) {
    aether_linear_gradient g = src;
    if (src.stop_count > 0 && src.stops) {
        g.stops = static_cast<aether_gradient_stop*>(std::malloc(sizeof(aether_gradient_stop) * src.stop_count));
        if (!g.stops) throw std::bad_alloc();
        std::memcpy(g.stops, src.stops, sizeof(aether_gradient_stop) * src.stop_count);
    }
    return g;
}

static bool resolve_gradient_for_paint(const aether_display_list_builder_s* builder,
                                       GradientReadView source,
                                       aether_rectf gradient_rect, float font_size,
                                       aether_linear_gradient* resolved) {
    aether_linear_gradient_resolution_context_s context{};
    context.gradient_rect = gradient_rect;
    context.font_size = font_size;
    context.root_font_size = builder->root_font_size;
    context.viewport_width = builder->viewport.width;
    context.viewport_height = builder->viewport.height;
    return builder->opacity_events
        ? aether_linear_gradient_resolve_native_srgb(GradientKernelReadView(source), context, resolved)
        : aether_linear_gradient_resolve(GradientKernelReadView(source), context, resolved);
}

/* ── command emitters ─────────────────────────────────────────────── */

static void emit_fill_rounded_rect(std::vector<aether_paint_command_t>& cmds,
                                    aether_rectf rect, aether_corner_radii radii, aether_color_rgba color) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_FILL_ROUNDED_RECT;
    cmd.data.fill_rounded_rect = {rect, radii, color};
    cmds.push_back(cmd);
    guard.release();
}

static void emit_fill_rounded_rect_gradient(std::vector<aether_paint_command_t>& cmds,
                                             aether_rectf rect, aether_corner_radii radii,
                                             const aether_linear_gradient& gradient, float opacity,
                                             aether_rectf gradient_rect = {}) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT;
    if (gradient_rect.width <= 0.f || gradient_rect.height <= 0.f) gradient_rect = rect;
    cmd.data.fill_rounded_rect_gradient = {rect, radii, gradient_rect, copy_gradient(gradient), opacity};
    cmds.push_back(cmd);
    guard.release();
}

static void emit_draw_background_image(std::vector<aether_paint_command_t>& cmds, aether_rectf rect,
                                       BackgroundLayerReadView layer_view, float opacity,
                                       aether_rectf source_rect = {}, aether_rectf destination_rect = {},
                                       aether_bool_t has_resolved_geometry = AETHER_FALSE) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_DRAW_BACKGROUND_IMAGE;
    const auto& layer = layer_view.scalars;
    cmd.data.draw_background_image.source = checked_strdup(layer_view.image_source.c_str());
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
    guard.release();
}

static void emit_apply_backdrop_filter(std::vector<aether_paint_command_t>& cmds,
                                       aether_rectf rect, aether_corner_radii radii,
                                       ReadOnlyArrayView<aether_backdrop_filter> filters) {
    if (filters.empty()) return;
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_APPLY_BACKDROP_FILTER;
    cmd.data.apply_backdrop_filter.rect = rect;
    cmd.data.apply_backdrop_filter.radii = radii;
    auto& output = cmd.data.apply_backdrop_filter.filters;
    output.items = static_cast<aether_backdrop_filter*>(std::malloc(sizeof(aether_backdrop_filter) * filters.size()));
    if (!output.items) throw std::bad_alloc();
    output.count = static_cast<int32_t>(filters.size());
    for (size_t index = 0; index < filters.size(); ++index) output.items[index] = filters[index];
    cmds.push_back(cmd);
    guard.release();
}

static void emit_draw_box_shadow(std::vector<aether_paint_command_t>& cmds,
                                  aether_rectf rect, aether_corner_radii radii, aether_shadow shadow) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_DRAW_BOX_SHADOW;
    cmd.data.draw_box_shadow = {rect, radii, shadow};
    cmds.push_back(cmd);
    guard.release();
}

static void emit_stroke_rounded_rect(std::vector<aether_paint_command_t>& cmds,
                                      aether_rectf rect, aether_corner_radii radii, float stroke_width, aether_color_rgba color) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_STROKE_ROUNDED_RECT;
    cmd.data.stroke_rounded_rect = {rect, radii, stroke_width, color};
    cmds.push_back(cmd);
    guard.release();
}

static void emit_draw_text(std::vector<aether_paint_command_t>& cmds,
                            const char* text, aether_rectf rect,
                            const char* font_family, float font_size, int32_t font_weight,
                            int32_t text_align, aether_color_rgba color, bool wrap) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_DRAW_TEXT;
    cmd.data.draw_text.text = checked_strdup(text);
    cmd.data.draw_text.rect = rect;
    strncpy(cmd.data.draw_text.font_family, font_family, sizeof(cmd.data.draw_text.font_family) - 1);
    cmd.data.draw_text.font_size = font_size;
    cmd.data.draw_text.font_weight = font_weight;
    cmd.data.draw_text.text_align = text_align;
    cmd.data.draw_text.color = color;
    cmd.data.draw_text.wrap = wrap ? AETHER_TRUE : AETHER_FALSE;
    cmds.push_back(cmd);
    guard.release();
}

static void emit_draw_text_gradient(std::vector<aether_paint_command_t>& cmds,
                                     const char* text, aether_rectf rect,
                                     const char* font_family, float font_size, int32_t font_weight,
                                     int32_t text_align, const aether_linear_gradient& gradient, float opacity) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_DRAW_TEXT_GRADIENT;
    cmd.data.draw_text_gradient.text = checked_strdup(text);
    cmd.data.draw_text_gradient.rect = rect;
    strncpy(cmd.data.draw_text_gradient.font_family, font_family, sizeof(cmd.data.draw_text_gradient.font_family) - 1);
    cmd.data.draw_text_gradient.font_size = font_size;
    cmd.data.draw_text_gradient.font_weight = font_weight;
    cmd.data.draw_text_gradient.text_align = text_align;
    cmd.data.draw_text_gradient.gradient = copy_gradient(gradient);
    cmd.data.draw_text_gradient.opacity = opacity;
    cmds.push_back(cmd);
    guard.release();
}

static void emit_draw_text_shadow(std::vector<aether_paint_command_t>& cmds,
                                  const char* text, aether_rectf rect,
                                  const char* font_family, float font_size, int32_t font_weight,
                                  int32_t text_align, aether_shadow shadow, bool wrap) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_DRAW_TEXT_SHADOW;
    cmd.data.draw_text_shadow.text = checked_strdup(text);
    cmd.data.draw_text_shadow.rect = rect;
    strncpy(cmd.data.draw_text_shadow.font_family, font_family, sizeof(cmd.data.draw_text_shadow.font_family) - 1);
    cmd.data.draw_text_shadow.font_size = font_size;
    cmd.data.draw_text_shadow.font_weight = font_weight;
    cmd.data.draw_text_shadow.text_align = text_align;
    cmd.data.draw_text_shadow.shadow = shadow;
    cmd.data.draw_text_shadow.wrap = wrap ? AETHER_TRUE : AETHER_FALSE;
    cmds.push_back(cmd);
    guard.release();
}

static void emit_text_shadows(std::vector<aether_paint_command_t>& cmds,
                              ComputedStyleReadView view,
                              const char* text, aether_rectf rect,
                              int32_t text_align, bool wrap,
                              float opacity) {
    const auto& style = view.scalars();
    const auto shadows = view.text_shadows();
    if (shadows.empty()) return;

    // Painter's order is back-to-front. The first CSS shadow is closest to
    // the glyph and therefore has to be emitted last among the shadows.
    for (size_t index = shadows.size(); index-- > 0;) {
        auto shadow = shadows[index];
        shadow.color = apply_opacity(shadow.color, opacity);
        emit_draw_text_shadow(cmds, text, rect, style.font_family.data(), style.font_size,
                              style.font_weight, text_align, shadow, wrap);
    }
}

static void emit_push_clip(std::vector<aether_paint_command_t>& cmds, aether_rectf rect, aether_corner_radii radii) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_PUSH_CLIP;
    cmd.data.push_clip = {rect, radii};
    cmds.push_back(cmd);
    guard.release();
}

static void emit_pop_clip(std::vector<aether_paint_command_t>& cmds) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_POP_CLIP;
    cmds.push_back(cmd);
    guard.release();
}

static void emit_push_transform(std::vector<aether_paint_command_t>& cmds, aether_transform_matrix matrix) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_PUSH_TRANSFORM;
    cmd.data.push_transform = {matrix};
    cmds.push_back(cmd);
    guard.release();
}

static void emit_pop_transform(std::vector<aether_paint_command_t>& cmds) {
    aether_paint_command_t cmd{};
    paint_command_guard_s guard{cmd};
    cmd.tag = AETHER_PAINT_POP_TRANSFORM;
    cmds.push_back(cmd);
    guard.release();
}
static void emit_push_scroll_offset(std::vector<aether_paint_command_t>& cmds, int64_t id) { aether_paint_command_t c{}; c.tag=AETHER_PAINT_PUSH_SCROLL_OFFSET; c.data.push_scroll_offset={id}; cmds.push_back(c); }
static void emit_pop_scroll_offset(std::vector<aether_paint_command_t>& cmds) { aether_paint_command_t c{}; c.tag=AETHER_PAINT_POP_SCROLL_OFFSET; cmds.push_back(c); }
static void emit_draw_scrollbar_chrome(std::vector<aether_paint_command_t>& cmds, int64_t id) { aether_paint_command_t c{}; c.tag=AETHER_PAINT_DRAW_SCROLLBAR_CHROME; c.data.draw_scrollbar_chrome={id}; cmds.push_back(c); }
static void emit_begin_paint_chunk(std::vector<aether_paint_command_t>& cmds, int64_t id) { if (id <= 0) return; aether_paint_command_t c{}; c.tag=AETHER_PAINT_BEGIN_PAINT_CHUNK; c.data.paint_chunk={id}; cmds.push_back(c); }
static void emit_end_paint_chunk(std::vector<aether_paint_command_t>& cmds, int64_t id) { if (id <= 0) return; aether_paint_command_t c{}; c.tag=AETHER_PAINT_END_PAINT_CHUNK; c.data.paint_chunk={id}; cmds.push_back(c); }

/* ══════════════════════════════════════════════════════════════════════
   DisplayListBuilder — walks LayoutBox tree, emits paint commands
   ══════════════════════════════════════════════════════════════════════ */

static ComputedStyleReadView style_for_box(const aether_display_list_builder_s* builder,
                                          const aether_layout_box_s* box) {
    return builder->style_pin->lookup().lookup_box(*box, BoxStylePolicy::effective_map);
}

template<class Callback>
static auto checked_paint_callback(aether_display_list_builder_s* builder, Callback&& callback) {
    const aether::layout::CallbackEpochGuard selected(builder->style_pin->selected_version());
    const aether::layout::CallbackEpochGuard base(builder->style_pin->base_version());
    const aether::layout::CallbackEpochGuard document(builder->style_pin->document_lifetime());
    auto result = callback();
    if (!selected.valid() || !base.valid() || !document.valid() || !builder->style_pin->structure_valid())
        throw std::runtime_error("DOM mutated during paint callback");
    return result;
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

// Keep the private callback frame out of each recursive paint_box activation.
// Only the nonrecursive atomic-control branch needs this stack storage.
#if defined(_MSC_VER)
#define AETHER_PAINT_CONTROL_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define AETHER_PAINT_CONTROL_NOINLINE __attribute__((noinline))
#else
#define AETHER_PAINT_CONTROL_NOINLINE
#endif

static AETHER_PAINT_CONTROL_NOINLINE void paint_control(aether_display_list_builder_s* builder,
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
   Hit testing traverses the same local order in reverse. */
static void paint_children(aether_display_list_builder_s* builder,
                           aether_layout_box_s* box,
                           aether_node_s* focused_element,
                           const paint_context_s& ctx,
                           std::vector<aether_paint_command_t>& cmds)
{
    const auto items = layout_children_paint_order(box, builder->style_pin->lookup(), BoxStylePolicy::effective_map);
    if (items.empty()) {
        for (auto* child : box->children) {
            paint_box(builder, child, focused_element, ctx, cmds);
        }
        return;
    }
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
    const auto view = style_for_box(builder, box);
    const auto& style = view.scalars();
    if (style.display == AETHER_DISPLAY_NONE) return;
    const bool visible = builder->style_pin->lookup().presentation(*box, BoxStylePolicy::effective_map).visible;
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
    float own_opacity = box->visual_sample_valid ? box->visual_opacity : style.opacity;
    if (!box->visual_sample_valid && box->element && builder->transition_runtime) {
        (void)aether_transition_runtime_opacity_override(
            builder->transition_runtime, box->element, &own_opacity);
    }
    // CSS opacity applies to the element's whole subtree (it creates a stacking
    // context in Chrome): multiply this box's own opacity (with any live
    // transition override) by the accumulated opacity of its ancestors.
    const bool native = builder->opacity_events != nullptr;
    if(native && (style.position==AETHER_POSITION_FIXED || style.position==AETHER_POSITION_ABSOLUTE)) {
        // The native tree traversal can scroll a positioned descendant with its
        // transformed owner, but cannot escape an intervening clip/scroll scope.
        bool transformed_owner = false, intervening_clip = false;
        for (auto* ancestor = box->element ? box->element->parent : nullptr; ancestor; ancestor=ancestor->parent) {
            if (builder->layout->effective_fixed_container_nodes.count(ancestor)) { transformed_owner=true; break; }
            const auto* owner=builder->layout->find_primary_box(ancestor);
            if (owner && style.position==AETHER_POSITION_ABSOLUTE && owner->style.position!=AETHER_POSITION_STATIC) break;
            if (owner && owner!=builder->layout->root &&
                (owner->style.overflow_x!=AETHER_OVERFLOW_VISIBLE || owner->style.overflow_y!=AETHER_OVERFLOW_VISIBLE))
                intervening_clip = true;
        }
        if (intervening_clip && (transformed_owner || style.position==AETHER_POSITION_FIXED))
            throw std::runtime_error("positioned escape from intermediate clip unsupported");
        if (style.position==AETHER_POSITION_FIXED && !transformed_owner && builder->native_scroll_depth)
            throw std::runtime_error("fixed under scroll unsupported");
    }
    if(native && (!std::isfinite(own_opacity) || own_opacity<0 || own_opacity>1)) throw std::runtime_error("opacity range");
    if(native && box==builder->layout->root && own_opacity!=1) throw std::runtime_error("root opacity unsupported");
    const bool group = native && box->kind==LAYOUT_BOX_ELEMENT && own_opacity!=1;
    std::uint64_t group_id=0;
    if(group) {
        group_id=++builder->opacity_serial;
        builder->opacity_events->push_back({cmds.size(),true,group_id,builder->opacity_parents.empty()?0:builder->opacity_parents.back(),own_opacity});
        builder->opacity_parents.push_back(group_id);
    }
    ctx.effective_opacity = native ? 1.f : own_opacity * parent_ctx.inherited_opacity;
    ctx.inherited_opacity = ctx.effective_opacity;

    // CSS transforms are post-layout and scope over the element's own paint
    // plus every descendant. Keeping this as Display List structure avoids
    // baking geometry into commands or duplicating matrix math in bindings.
    // Anonymous text fragments borrow typographic style from their owner but
    // are not independently transformable CSS elements. Their owning element
    // already brackets them, so emitting another scope here would apply the
    // same transform twice.
    ctx.has_transform = box->kind == LAYOUT_BOX_ELEMENT &&
        !view.transforms().empty();
    if (box->visual_sample_valid) {
        ctx.has_transform = box->kind == LAYOUT_BOX_ELEMENT && box->visual_has_transform;
        ctx.transform_override = box->visual_transform;
        ctx.has_transform_override = ctx.has_transform;
    } else if (box->kind == LAYOUT_BOX_ELEMENT && box->element && builder->transition_runtime &&
        aether_transition_runtime_transform_override(
            builder->transition_runtime, box->element, &ctx.transform_override) == AETHER_TRUE) {
        // An active transform transition (including to/from `transform: none`)
        // wins over the static resolution so the interpolated matrix is used.
        ctx.has_transform_override = true;
        ctx.has_transform = true;
    } else if (ctx.has_transform) {
        ctx.transform_override = aether_transform_matrix_resolve(
            IndexedReadRange<aether_transform_operation>(view.transforms()), style.transform_origin, box->border_box,
            style.font_size, builder->root_font_size, builder->viewport);
    }

    if (ctx.has_transform) emit_push_transform(cmds, ctx.transform_override);

    // Text box → emit text command and return
    if (box->kind == LAYOUT_BOX_TEXT) {
        const auto first_text_command = cmds.size();
        if (visible && !empty_border_box && !box->text.empty()) {
            if(builder->native_text_runs && (!box->native_text.run ||
               box->native_text.run->source != box->text ||
               box->native_text.baseline != box->native_text.run->metrics.baseline))
                throw std::runtime_error("native text identity missing");

            emit_text_shadows(cmds, view, box->text.c_str(), box->content_box,
                              AETHER_TEXT_ALIGN_START, false, ctx.effective_opacity);
            if (style.has_text_gradient) {
                aether_linear_gradient gradient{};
                paint_gradient_guard_s gradient_guard{gradient};
                if (resolve_gradient_for_paint(builder, view.text_gradient(), box->content_box,
                                               style.font_size, &gradient)) {
                    emit_draw_text_gradient(cmds, box->text.c_str(), box->content_box,
                                            style.font_family.data(), style.font_size, style.font_weight,
                                            AETHER_TEXT_ALIGN_START, gradient, ctx.effective_opacity);
                    /* resolved gradient owned by its scope */
                }
            } else {
                emit_draw_text(cmds, box->text.c_str(), box->content_box,
                               style.font_family.data(), style.font_size, style.font_weight,
                               AETHER_TEXT_ALIGN_START,
                               apply_opacity(style.color, ctx.effective_opacity), false);
            }
        }
            if(builder->native_text_runs && cmds.size()!=first_text_command) {
                // Reject effects/gradient until their resource contract exists.
                if(cmds.size()!=first_text_command+1 || cmds.back().tag!=AETHER_PAINT_DRAW_TEXT)
                    throw std::runtime_error("native text effects unsupported");
                auto placement=box->native_text;
                placement.baseline += box->content_box.y;
                builder->native_text_runs->push_back({first_text_command,std::move(placement)});
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

    bool clips = layout_box_clips(style);
    if (clips) {
        const auto outer = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size, builder->root_font_size, builder->viewport.width, builder->viewport.height);
        const auto clip = aether_corner_radii_inset(outer,
            box->padding_box.x - box->border_box.x, box->padding_box.y - box->border_box.y,
            box->border_box.x + box->border_box.width - (box->padding_box.x + box->padding_box.width),
            box->border_box.y + box->border_box.height - (box->padding_box.y + box->padding_box.height));
        emit_push_clip(cmds, box->padding_box, clip);
    }

    const bool dynamic_scroll = layout_box_has_dynamic_scroll(builder->layout, box);
    const bool scrollbar_chrome = visible && box->element && !empty_border_box && box_has_scrollbar_chrome(builder, box);
    if (dynamic_scroll) { emit_push_scroll_offset(cmds, box->element->style_node_id); if(builder->opacity_events)++builder->native_scroll_depth; }
    // Children (or atomic control paint)
    if (box->is_initial_canvas) {
        // Initial bitmap is transparent; decorations already painted.
    } else if (box->is_atomic_control) {
        if (visible) paint_control(builder, box, focused_element, ctx, cmds);
    } else {
        paint_children(builder, box, focused_element, ctx, cmds);
    }

    if (dynamic_scroll) { emit_pop_scroll_offset(cmds); if(builder->opacity_events)--builder->native_scroll_depth; }
    // Scrollbar chrome is retained separately from content offset. Bindings
    // resolve its live thumb position from the same native geometry formula
    // used by hit-testing.
    if (scrollbar_chrome) emit_draw_scrollbar_chrome(cmds, box->element->style_node_id);

    if (clips) {
        emit_pop_clip(cmds);
    }

    // Focus ring
    if (visible && !empty_border_box && box->element == focused_element) {
        auto focus_radii = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size, builder->root_font_size, builder->viewport.width, builder->viewport.height);
        float* values = &focus_radii.top_left_x;
        for (int i = 0; i < 8; ++i) values[i] += 2.f;
        aether_rectf inflated = aether_rectf_inflate(box->border_box, 2);
        emit_stroke_rounded_rect(cmds, inflated, focus_radii, 1,
                                 {0.145f, 0.388f, 0.922f, 0.75f});
    }

    if (ctx.has_transform) emit_pop_transform(cmds);
    if(group) {
        builder->opacity_events->push_back({cmds.size(),false,group_id,builder->opacity_parents.size()>1?builder->opacity_parents[builder->opacity_parents.size()-2]:0,own_opacity});
        builder->opacity_parents.pop_back();
    }
    if (has_paint_chunk) emit_end_paint_chunk(cmds, box->element->style_node_id);
}

/* ── paint_box_shadows ────────────────────────────────────────────── */

static void paint_box_shadows(aether_display_list_builder_s* builder,
                               aether_layout_box_s* box,
                               std::vector<aether_paint_command_t>& cmds)
{
    const auto view = style_for_box(builder, box);
    const auto& style = view.scalars();
    const auto shadows = view.box_shadows();
    if (shadows.empty()) return;

    const auto radii = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size, builder->root_font_size, builder->viewport.width, builder->viewport.height);
    // CSS paints the first declared shadow closest to the element. Commands
    // are painter's order, so emit the declaration list back-to-front.
    for (size_t index = shadows.size(); index-- > 0;) {
        const auto& shadow = shadows[index];
        // Inset shadow needs an interior mask and remains deliberately absent
        // from the outer-shadow paint path until that renderer exists.
        if (shadow.inset == AETHER_TRUE) continue;
        emit_draw_box_shadow(cmds, box->border_box, radii, shadow);
    }
}

/* ── paint_background_and_border ──────────────────────────────────── */

static void paint_background_layers(aether_display_list_builder_s* builder,
                                    ComputedStyleReadView view,
                                    const aether_node_s* evidence_node,
                                    const aether_rectf& paint_rect,
                                    const aether_corner_radii& radii,
                                    const paint_context_s& ctx,
                                    std::vector<aether_paint_command_t>& cmds)
{
    const auto& style = view.scalars();
    const auto layers = view.background_layers();
    const auto bg = apply_opacity(style.background_color, ctx.effective_opacity);
    if (bg.a > 0) {
        emit_fill_rounded_rect(cmds, paint_rect, radii, bg);
    }
    // CSS paints the first declared background layer on top. Emit the list in reverse so the
    // flat display list preserves that order above the already-painted background color.
    if (!layers.empty()) {
        // Background layers are clipped as a group. Besides obeying border-radius, this keeps
        // repeated procedural tiles from assigning the corner radius to every individual tile.
        emit_push_clip(cmds, paint_rect, radii);
        for (size_t index = layers.size(); index-- > 0;) {
            const auto layer_view = layers[index].view();
            const auto& layer = layer_view.scalars;
            if (layer.kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT) {
                if (builder->opacity_events) {
                    const auto measures=builder->style_pin->lookup().lookup_measures(evidence_node);
                    if (layers.size()!=1 || !measures || measures->background_gradient()!=aether::css::GradientEvidence::explicit_srgb_cardinal_angle) {
                        builder->native_gradient_output->native_gradient_unproven=true;
                        continue;
                    }
                }
                aether_background_image_geometry_context_s context{};
                context.paint_rect = paint_rect;
                context.has_intrinsic_size = false;
                if (builder->opacity_events) context.max_tiles=65536/layer_view.gradient.stops.size();
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
                if (aether_background_image_resolve_geometry(context, tiles) && !tiles.empty()) {
                    for (const auto& tile : tiles) {
                        if (builder->native_gradient_output) {
                            const size_t n=layer_view.gradient.stops.size();
                            auto& used=builder->native_gradient_output->native_gradient_stop_count;
                            if (n<2 || n>64 || used>65536 || n>65536-used)
                                throw std::runtime_error("native gradient stop budget");
                            used+=n;
                        }
                        aether_linear_gradient gradient{};
                paint_gradient_guard_s gradient_guard{gradient};
                        if (!resolve_gradient_for_paint(builder, layer_view.gradient, tile.tile_rect,
                                                        style.font_size, &gradient)) {
                            if (builder->native_gradient_output) builder->native_gradient_output->native_gradient_unproven=true;
                            continue;
                        }
                        emit_fill_rounded_rect_gradient(cmds, tile.destination, {}, gradient,
                                                        ctx.effective_opacity, tile.tile_rect);
                        if (builder->native_gradient_output) builder->native_gradient_output->native_srgb_gradients.insert(
                            cmds.back().data.fill_rounded_rect_gradient.gradient.stops);
                        /* resolved gradient owned by its scope */
                    }
                } else if (builder->native_gradient_output) {
                    builder->native_gradient_output->native_gradient_unproven=true;
                }
            } else if (layer.kind == AETHER_BACKGROUND_LAYER_IMAGE_URL && layer_view.image_source.present()) {
                aether_sizef image_size{};
                const auto dimensions = builder->image_dimensions;
                void* const dimensions_data = builder->image_dimensions_data;
                if (dimensions &&
                    checked_paint_callback(builder, [&] { return dimensions(layer_view.image_source.c_str(), &image_size, dimensions_data); }) == AETHER_TRUE) {
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
                            emit_draw_background_image(cmds, paint_rect, layer_view, ctx.effective_opacity,
                                                       tile.source, tile.destination, AETHER_TRUE);
                        }
                        continue;
                    }
                }
                // Pending or failed resources retain the declarative command so the binding can
                // request it; the renderer intentionally paints no unresolved command.
                emit_draw_background_image(cmds, paint_rect, layer_view, ctx.effective_opacity);
            }
        }
        emit_pop_clip(cmds);
    }
}

static void paint_background_and_border(aether_display_list_builder_s* builder, aether_layout_box_s* box,
                                         const bool suppress_background, const paint_context_s& ctx,
                                         std::vector<aether_paint_command_t>& cmds)
{
    const auto view = style_for_box(builder, box);
    const auto& style = view.scalars();
    const auto radii = aether_border_radii_resolve(style.border_radii, box->border_box, style.font_size, builder->root_font_size, builder->viewport.width, builder->viewport.height);

    // Flat Display List v1 semantics: this snapshots exactly the commands already
    // painted before this box, never its own background, border, or descendants.
    emit_apply_backdrop_filter(cmds, box->border_box, radii, view.backdrop_filters());
    if (!suppress_background) {
        paint_background_layers(builder, view, box->element, box->border_box, radii, ctx, cmds);
    }

    const auto border_begin = cmds.size();
    // Native groups own opacity; applying it in the border painter again
    // would square opacity for the very same element. Legacy output is unchanged.
    auto border_style = style;
    if (builder->opacity_events) border_style.opacity = 1.f;
    aether_paint_border(border_style, box->border_box, radii, box->used_border, cmds);
    if (builder->border_groups && cmds.size() > border_begin &&
        cmds[border_begin].tag == AETHER_PAINT_FILL_BORDER_SIDE)
        builder->border_groups->push_back({border_begin, cmds.size()-border_begin});
}

static bool has_canvas_eligible_background(ComputedStyleReadView style) {
    return style.scalars().background_color.a > 0.f || !style.background_layers().empty();
}

static const aether_node_s* document_element_for(const aether_node_s* body) {
    for (auto* node = body ? body->parent : nullptr; node; node = node->parent) {
        if (node->type == AETHER_NODE_ELEMENT && node->tag_name == "html") return node;
    }
    return nullptr;
}

// Validate the effective gradient domain before visibility/opacity can suppress commands.
// This intentionally rejects unlaid-out gradient nodes, rather than silently blessing them.
static void validate_native_gradient_domain(aether_display_list_builder_s* builder,
                                           const aether_node_s* root) {
    if (!builder->opacity_events || !root) return;
    std::vector<const aether_node_s*> pending{root};
    size_t visited=0, total_stops=0;
    while (!pending.empty()) {
        if (++visited>65536) throw std::runtime_error("native gradient node budget");
        const auto* node=pending.back(); pending.pop_back();
        for (const auto* child:node->children) {
            if (pending.size()>=65536) throw std::runtime_error("native gradient node budget");
            pending.push_back(child);
        }
        if (node->type!=AETHER_NODE_ELEMENT) continue;
        const auto view=builder->style_pin->lookup().lookup_node(node);
        if (!view) continue;
        const auto layers=view->background_layers();
        for (const auto& entry:layers) {
            const auto layer=entry.view();
            if (layer.scalars.kind!=AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT) continue;
            const auto measures=builder->style_pin->lookup().lookup_measures(node);
            const auto* box=builder->layout->find_primary_box(const_cast<aether_node_s*>(node));
            if (layers.size()!=1 || !measures || !box ||
                measures->background_gradient()!=aether::css::GradientEvidence::explicit_srgb_cardinal_angle ||
                layer.gradient.stops.size()<2 || layer.gradient.stops.size()>64)
                throw std::runtime_error("native gradient provenance unsupported");
            aether_background_image_geometry_context_s geometry{};
            geometry.paint_rect=box->border_box; geometry.has_intrinsic_size=false;
            geometry.max_tiles=(65536-total_stops)/layer.gradient.stops.size();
            if (!geometry.max_tiles) throw std::runtime_error("native gradient stop budget");
            geometry.position_x=layer.scalars.position_x; geometry.position_y=layer.scalars.position_y;
            geometry.size_kind=layer.scalars.size_kind; geometry.size_x=layer.scalars.size_x; geometry.size_y=layer.scalars.size_y;
            geometry.repeat_x=layer.scalars.repeat_x; geometry.repeat_y=layer.scalars.repeat_y;
            geometry.font_size=view->scalars().font_size; geometry.root_font_size=builder->root_font_size;
            geometry.viewport_width=builder->viewport.width; geometry.viewport_height=builder->viewport.height;
            std::vector<aether_background_image_tile_s> tiles;
            if (!aether_background_image_resolve_geometry(geometry,tiles) || tiles.empty() ||
                tiles.size()>(65536-total_stops)/layer.gradient.stops.size())
                throw std::runtime_error("native gradient geometry unsupported");
            total_stops+=tiles.size()*layer.gradient.stops.size();
            for (const auto& tile:tiles) {
                aether_linear_gradient gradient{}; paint_gradient_guard_s guard{gradient};
                if (!resolve_gradient_for_paint(builder,layer.gradient,tile.tile_rect,view->scalars().font_size,&gradient))
                    throw std::runtime_error("native gradient stop range unsupported");
            }
        }
    }
}

static void validate_native_root(aether_display_list_builder_s* builder,const aether_layout_result_s* layout) {
    const auto* body=layout->root?layout->root->element:nullptr;
    const auto* html=document_element_for(body);
    if(builder->opacity_events) {
        for(const auto* n : {html,body}) {
            if(!n) continue;
            const auto st=builder->style_pin->lookup().lookup_node(n);
            float opacity=st?st->scalars().opacity:1.f;
            if(builder->transition_runtime) (void)aether_transition_runtime_opacity_override(builder->transition_runtime,const_cast<aether_node_s*>(n),&opacity);
            if(!std::isfinite(opacity)||opacity!=1) throw std::runtime_error("canvas opacity unsupported");
        }
    }
    if(builder->opacity_events && layout->root && layout->root->visual_sample_valid && layout->root->visual_opacity!=1)
        throw std::runtime_error("root visual opacity unsupported");
}

static void paint_canvas_background(aether_display_list_builder_s* builder,
                                    const aether_layout_result_s* layout,
                                    std::vector<aether_paint_command_t>& cmds) {
    const auto* body = layout->root ? layout->root->element : nullptr;
    const auto* html = document_element_for(body);
    const auto html_style = builder->style_pin->lookup().lookup_node(html);
    const auto body_style = layout->root
        ? std::optional<ComputedStyleReadView>(style_for_box(builder, layout->root)) : std::nullopt;

    // CSS 2.1 §14.2: the root background paints the canvas.  When it is fully
    // transparent/no-image, body's background is propagated instead and body
    // becomes transparent for its ordinary box paint.
    std::optional<ComputedStyleReadView> source_style;
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
    ctx.effective_opacity = source_style->scalars().opacity;
    paint_background_layers(builder, *source_style, source_node, canvas, square, ctx, cmds);
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

static AETHER_PAINT_CONTROL_NOINLINE void paint_control(aether_display_list_builder_s* builder,
                           aether_layout_box_s* box,
                           aether_node_s* focused_element,
                           const paint_context_s& ctx,
                           std::vector<aether_paint_command_t>& cmds)
{
    if(builder->native_text_runs) throw std::runtime_error("native controls unsupported");
    auto* element = box->element;
    const auto view = style_for_box(builder, box);
    const auto& style = view.scalars();

    const bool contextual = builder->style_pin->base_contextual();
    const std::string displayed = contextual ? box->control_display_text : aether_opaque_control_display_text(element, style);
    // Contextual paint never consults live control values. Legacy callers keep
    // the historical live-DOM adapter until they adopt revision snapshots.
    bool shows_placeholder = box->control_shows_placeholder;
    if (!contextual) {
        const auto value = element->attributes.find("value");
        shows_placeholder = value == element->attributes.end() || value->second.empty();
    }
    aether_color_rgba color = !shows_placeholder
        ? style.color
        : aether_color_rgba{0.42f, 0.45f, 0.5f, 1.0f};

    emit_text_shadows(cmds, view, displayed.c_str(), box->content_box, style.text_align, false,
                      ctx.effective_opacity);
    emit_draw_text(cmds, displayed.c_str(), box->content_box,
                   style.font_family.data(), style.font_size, style.font_weight,
                   style.text_align,
                   apply_opacity(color, ctx.effective_opacity), false);

    // Caret for focused input
    if (element == focused_element && (builder->contextual_text_measurer || builder->text_measurer)) {
        const auto measures = builder->style_pin->lookup().lookup_measures(element);
        aether::style::CallbackStyleCopy callback_style;
        const auto copy_status = callback_style.prepare(view);
        if (copy_status == AETHER_ERROR_OUT_OF_MEMORY) throw std::bad_alloc();
        if (copy_status != AETHER_OK) throw std::runtime_error("Cannot copy callback style");
        const auto contextual_measurer = builder->contextual_text_measurer;
        const auto measurer = builder->text_measurer;
        void* const callback_data = contextual_measurer
            ? builder->contextual_text_measurer_data : builder->text_measurer_data;
        auto metrics = checked_paint_callback(builder, [&] { return contextual_measurer
            ? contextual_measurer(displayed.c_str(), callback_style.get(),
                measure_used_line_height(measures ? &*measures : nullptr, style), box->content_box.width, callback_data)
            : measurer(displayed.c_str(), callback_style.get(), box->content_box.width, callback_data); });
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

#undef AETHER_PAINT_CONTROL_NOINLINE

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
                                                 nullptr,
                                                 focused_element, out_list);
}

AETHER_API aether_status_t aether_display_list_build_with_styles(
    aether_display_list_builder_t builder, aether_layout_result_t layout,
    aether_style_map_t current_styles, aether_node_t focused_element,
    aether_display_list_t* out_list)
{
    if (!builder || !layout || !out_list) return AETHER_ERROR_NULL_ARG;
    if (builder->style_pin) return AETHER_ERROR_INVALID_STATE;
    aether::layout::LayoutOperationPin geometry_pin(layout);
    aether::layout::StyleOperationPin style_pin(layout->assets,
        aether::style::pin_style_map(current_styles), current_styles != nullptr);
    if (!layout->root || !style_pin.structure_valid() || !style_pin.accepts_override())
        return AETHER_ERROR_INVALID_STATE;
    const aether::layout::CallbackEpochGuard base_epoch(style_pin.base_version());
    const aether::layout::CallbackEpochGuard selected_epoch(style_pin.selected_version());
    const aether::layout::CallbackEpochGuard document_epoch(style_pin.document_lifetime());
    struct build_scope_s {
        aether_display_list_builder_s* builder;
        ~build_scope_s() { builder->border_groups = nullptr; builder->native_gradient_output = nullptr; builder->canvas_background_source = nullptr; builder->style_pin = nullptr; builder->layout = nullptr; }
    } scope{builder};
    builder->viewport = layout->viewport;
    builder->root_font_size = layout->root_font_size;
    builder->style_pin = &style_pin;
    builder->layout = layout;

    std::unique_ptr<aether_display_list_s> list_owner(new(std::nothrow) aether_display_list_s());
    auto* list = list_owner.get();
    if (!list) return AETHER_ERROR_OUT_OF_MEMORY;

    try {
    validate_native_root(builder,layout);
    validate_native_gradient_domain(builder,layout->root->element);
    builder->border_groups = &list->border_groups;
    builder->native_gradient_output = builder->opacity_events ? list : nullptr;
    builder->canvas_background_source = nullptr;
    paint_canvas_background(builder, layout, list->commands);
    paint_box(builder, layout->root, focused_element, paint_context_s{}, list->commands);
    builder->canvas_background_source = nullptr;
    builder->style_pin = nullptr;
    builder->layout = nullptr;
    if (!base_epoch.valid() || !selected_epoch.valid() || !document_epoch.valid() || !style_pin.structure_valid())
        return AETHER_ERROR_INVALID_STATE;
    *out_list = list_owner.release();
    return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
    catch (...) { return AETHER_ERROR_INVALID_STATE; }
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
    if (builder->style_pin) return AETHER_ERROR_INVALID_STATE;
    aether::layout::LayoutOperationPin geometry_pin(layout);
    aether::layout::StyleOperationPin style_pin(layout->assets,
        aether::style::pin_style_map(current_styles), current_styles != nullptr);
    if (!layout->root || !style_pin.structure_valid() || !style_pin.accepts_override())
        return AETHER_ERROR_INVALID_STATE;
    const aether::layout::CallbackEpochGuard base_epoch(style_pin.base_version());
    const aether::layout::CallbackEpochGuard selected_epoch(style_pin.selected_version());
    const aether::layout::CallbackEpochGuard document_epoch(style_pin.document_lifetime());
    struct build_scope_s {
        aether_display_list_builder_s* builder;
        ~build_scope_s() { builder->border_groups = nullptr; builder->native_gradient_output = nullptr; builder->canvas_background_source = nullptr; builder->style_pin = nullptr; builder->layout = nullptr; }
    } scope{builder};
    builder->viewport = layout->viewport;
    builder->root_font_size = layout->root_font_size;
    builder->style_pin = &style_pin;
    builder->layout = layout;
    builder->canvas_background_source = nullptr;

    std::unique_ptr<aether_display_list_s> list_owner(new(std::nothrow) aether_display_list_s());
    auto* list = list_owner.get();
    if (!list) {
        builder->style_pin = nullptr;
        builder->layout = nullptr;
        return AETHER_ERROR_OUT_OF_MEMORY;
    }

    try {
    // An isolated subtree excludes ancestor/canvas state. Root/document nodes
    // cannot use this route, even when their current opacity happens to be 1.
    if(builder->opacity_events && (element==layout->root->element ||
       element==document_element_for(layout->root->element) || element->type==AETHER_NODE_DOCUMENT))
        return AETHER_ERROR_INVALID_STATE;
    validate_native_gradient_domain(builder,element);
    builder->border_groups = &list->border_groups;
    builder->native_gradient_output = builder->opacity_events ? list : nullptr;
    if (auto* box = layout->find_primary_box(element)) {
        paint_box(builder, box, focused_element, paint_context_s{}, list->commands);
    }

    builder->style_pin = nullptr;
    builder->layout = nullptr;
    if (!base_epoch.valid() || !selected_epoch.valid() || !document_epoch.valid() || !style_pin.structure_valid())
        return AETHER_ERROR_INVALID_STATE;
    *out_list = list_owner.release();
    return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
    catch (...) { return AETHER_ERROR_INVALID_STATE; }
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
    if (index < 0 || index >= static_cast<int32_t>(list->commands.size())) {
        return AETHER_ERROR_INVALID_STATE;
    }

    auto* destination = out_cmd;
    aether_paint_command_t prepared{};
    paint_command_guard_s guard{prepared};
    out_cmd = &prepared;
    try {
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
            out_cmd->data.draw_text.text = checked_strdup(src.data.draw_text.text);
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
            out_cmd->data.draw_text_gradient.text = checked_strdup(src.data.draw_text_gradient.text);
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
            out_cmd->data.draw_text_shadow.text = checked_strdup(src.data.draw_text_shadow.text);
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
            out_cmd->data.draw_background_image.source = nullptr;
            out_cmd->data.draw_background_image.source = checked_strdup(src.data.draw_background_image.source);
            break;
        case AETHER_PAINT_APPLY_BACKDROP_FILTER:
            out_cmd->data.apply_backdrop_filter.rect = src.data.apply_backdrop_filter.rect;
            out_cmd->data.apply_backdrop_filter.radii = src.data.apply_backdrop_filter.radii;
            if (aether_backdrop_filter_list_copy(&src.data.apply_backdrop_filter.filters,
                                                   &out_cmd->data.apply_backdrop_filter.filters) != AETHER_TRUE) throw std::bad_alloc();
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
    *destination = prepared;
    guard.release();
    return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
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

AETHER_API void aether_display_list_builder_set_contextual_text_measurer(aether_display_list_builder_t builder,
    aether_text_measurer_contextual_fn callback, void* data) {
    if (!builder) return;
    builder->contextual_text_measurer = callback;
    builder->contextual_text_measurer_data = data;
}

namespace aether::rendering {
namespace {
aether_status_t build_scroll(aether_display_list_builder_t b,aether_layout_result_t l,
 aether_style_map_t styles,aether_node_t node,const NativeScrollInput* input,std::unique_ptr<NativePaintList>& out) noexcept {
 if(!b||!l)return AETHER_ERROR_NULL_ARG;
 if(b->style_pin||b->opacity_events)return AETHER_ERROR_INVALID_STATE;
 try {
  if(input && input->size()>65536)return AETHER_ERROR_INVALID_STATE;
  auto candidate=std::make_unique<NativePaintList>();
  struct Reset {aether_display_list_builder_t b; ~Reset(){b->opacity_events=nullptr;b->native_text_runs=nullptr;b->opacity_parents.clear();b->opacity_serial=0;b->native_scroll_depth=0;}} reset{b};
  b->opacity_events=&candidate->events;
  b->native_text_runs=&candidate->text_runs;
  const auto status=node?aether_display_list_build_node_with_styles(b,l,styles,node,nullptr,&NativeListAccess::list(*candidate)):
    aether_display_list_build_with_styles(b,l,styles,nullptr,&NativeListAccess::list(*candidate));
  if(status!=AETHER_OK)return status;
  auto list=NativeListAccess::list(*candidate);
  if(list->commands.size()>65536)return AETHER_ERROR_INVALID_STATE;
  std::size_t text_index=0, glyph_count=0;
  for(std::size_t i=0;i<list->commands.size();++i) {
   const auto tag=list->commands[i].tag;
   if(tag==AETHER_PAINT_DRAW_TEXT_GRADIENT || tag==AETHER_PAINT_DRAW_TEXT_SHADOW)
    return AETHER_ERROR_INVALID_STATE;
   if(tag!=AETHER_PAINT_DRAW_TEXT)continue;
   if(text_index>=candidate->text_runs.size())return AETHER_ERROR_INVALID_STATE;
   const auto& entry=candidate->text_runs[text_index++];
   if(entry.command_index!=i || !entry.placement.run ||
      !std::isfinite(entry.placement.baseline))return AETHER_ERROR_INVALID_STATE;
   glyph_count+=entry.placement.run->glyphs.size();
   if(glyph_count>text::max_frame_glyphs)return AETHER_ERROR_INVALID_STATE;
  }
  if(text_index!=candidate->text_runs.size())return AETHER_ERROR_INVALID_STATE;
  std::vector<std::int64_t> ids;
  for(const auto& c:list->commands) {
   if(c.tag==AETHER_PAINT_PUSH_SCROLL_OFFSET)ids.push_back(c.data.push_scroll_offset.scroll_container_id);
   if(c.tag==AETHER_PAINT_DRAW_SCROLLBAR_CHROME)ids.push_back(c.data.draw_scrollbar_chrome.scroll_container_id);
  }
  std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
  NativeScrollInput sorted=input?*input:NativeScrollInput{};
  if(!input)for(auto id:ids)sorted.push_back({id,0,0});
  std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.id<b.id;});
  if(sorted.size()!=ids.size())return AETHER_ERROR_INVALID_STATE;
  for(std::size_t i=0;i<ids.size();++i)if(ids[i]<=0||sorted[i].id!=ids[i])return AETHER_ERROR_INVALID_STATE;
  // Build one bounded map of element IDs, not one traversal per command.
  std::unordered_map<std::int64_t,const aether_layout_box_s*> boxes;
  for(const auto& item:l->primary_boxes)if(item.first)boxes.emplace(item.first->style_node_id,item.second);
  for(const auto& offset:sorted) {
   const auto it=boxes.find(offset.id);
   if(it==boxes.end()||it->second==l->root)return AETHER_ERROR_INVALID_STATE;
   const auto& box=*it->second;
   const double rx=box.scroll.scrollable_overflow_x,ry=box.scroll.scrollable_overflow_y;
   if(!std::isfinite(rx)||!std::isfinite(ry)||rx<0||ry<0||rx>65536||ry>65536||
      !std::isfinite(offset.x)||!std::isfinite(offset.y)||offset.x<0||offset.y<0||offset.x>rx||offset.y>ry)
    return AETHER_ERROR_INVALID_STATE;
   // Resolver API is float. Require exact representability so the thumb and
   // content use precisely the same frozen offset rather than separate rounding.
   const float x=static_cast<float>(offset.x),y=static_cast<float>(offset.y);
   if(double(x)!=offset.x||double(y)!=offset.y)return AETHER_ERROR_INVALID_STATE;
   candidate->scroll.push_back({offset.id,offset.x,offset.y,rx,ry,aether_resolve_scrollbar_geometry(box,{x,y})});
  }
  out=std::move(candidate);return AETHER_OK;
 }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
}
aether_status_t build_native_opacity(aether_display_list_builder_t b,aether_layout_result_t l,
 aether_style_map_t styles,aether_node_t node,std::unique_ptr<NativePaintList>& out) noexcept {
 return build_scroll(b,l,styles,node,nullptr,out);
}
aether_status_t build_native_opacity(aether_display_list_builder_t b,aether_layout_result_t l,
 aether_style_map_t styles,aether_node_t node,const NativeScrollInput& input,std::unique_ptr<NativePaintList>& out) noexcept {
 return build_scroll(b,l,styles,node,&input,out);
}
}
