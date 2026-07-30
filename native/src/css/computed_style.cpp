#include <aether/aether.h>
#include <cstring>

AETHER_API aether_computed_style_t aether_computed_style_default(void) {
    aether_computed_style_t s{};
    s.display = AETHER_DISPLAY_INLINE;
    s.flex_direction = AETHER_FLEX_DIRECTION_ROW;
    s.justify_content = AETHER_JUSTIFY_START;
    s.align_items = AETHER_ALIGN_STRETCH;
    s.align_self = AETHER_ALIGN_SELF_AUTO;
    s.flex_shrink = 1.0f;
    s.flex_basis = { AETHER_FLEX_BASIS_AUTO, aether_css_length_auto() };
    s.position = AETHER_POSITION_STATIC;
    s.inset = { aether_css_length_auto(), aether_css_length_auto(),
                aether_css_length_auto(), aether_css_length_auto() };
    s.margin = aether_css_edges_zero();
    s.padding = aether_css_edges_zero();
    s.border_width = aether_css_edges_zero();
    s.box_sizing = AETHER_BOX_SIZING_CONTENT_BOX;
    s.color = {0, 0, 0, 1};
    // CSS Color: each border color initially resolves to currentColor.
    // The style runtime preserves that symbolic relationship until its
    // post-cascade color-resolution phase; direct default callers receive the
    // equivalent concrete initial color.
    s.border_color = s.color;
    s.border_colors = {s.border_color, s.border_color, s.border_color, s.border_color};
    s.border_styles = {AETHER_BORDER_STYLE_NONE, AETHER_BORDER_STYLE_NONE,
                       AETHER_BORDER_STYLE_NONE, AETHER_BORDER_STYLE_NONE};
    s.opacity = 1.0f;
    s.transform_origin = {aether_css_length{50.0f, AETHER_CSS_UNIT_PERCENT},
                          aether_css_length{50.0f, AETHER_CSS_UNIT_PERCENT}};
    s.font_size = 16.0f;
    s.font_weight = AETHER_FONT_WEIGHT_NORMAL;
    s.line_height = 1.25f;
    s.white_space = AETHER_WHITE_SPACE_NORMAL;
    s.overflow_x = AETHER_OVERFLOW_VISIBLE;
    s.overflow_y = AETHER_OVERFLOW_VISIBLE;
    s.pointer_events = AETHER_POINTER_EVENTS_AUTO;
    s.visible = AETHER_TRUE;
    const auto zero_radius = aether_corner_radius{aether_css_length_zero(), aether_css_length_zero()};
    s.border_radii = {zero_radius, zero_radius, zero_radius, zero_radius};
    strncpy(s.font_family, "Segoe UI", sizeof(s.font_family) - 1);
    return s;
}

AETHER_API aether_computed_style_t aether_computed_style_inherit_from(const aether_computed_style_t* parent) {
    aether_computed_style_t s = aether_computed_style_default();
    if (parent) {
        s.color = parent->color;
        strncpy(s.font_family, parent->font_family, sizeof(s.font_family) - 1);
        s.font_size = parent->font_size;
        s.font_weight = parent->font_weight;
        s.line_height = parent->line_height;
        s.text_align = parent->text_align;
        s.text_transform = parent->text_transform;
        s.white_space = parent->white_space;
        s.cursor = parent->cursor;
        s.pointer_events = parent->pointer_events;
        // text-shadow is inherited. Keep an owned copy so parent and child
        // style handles never alias list storage.
        (void)aether_shadow_list_copy(&parent->text_shadows, &s.text_shadows);
    }
    return s;
}

AETHER_API void aether_computed_style_free(aether_computed_style_t* s) {
    if (s) {
        aether_background_layer_list_free(&s->background_layers);
        aether_backdrop_filter_list_free(&s->backdrop_filters);
        aether_transform_list_free(&s->transforms);
        aether_linear_gradient_free(&s->text_gradient);
        aether_shadow_list_free(&s->text_shadows);
        aether_shadow_list_free(&s->box_shadows);
    }
}
