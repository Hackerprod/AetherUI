#include <aether/aether.h>
#include <cstring>

AETHER_API aether_computed_style_t aether_computed_style_default(void) {
    aether_computed_style_t s{};
    s.display = AETHER_DISPLAY_INLINE;
    s.flex_direction = AETHER_FLEX_DIRECTION_ROW;
    s.justify_content = AETHER_JUSTIFY_START;
    s.align_items = AETHER_ALIGN_STRETCH;
    s.box_sizing = AETHER_BOX_SIZING_CONTENT_BOX;
    s.color = {0, 0, 0, 1};
    s.opacity = 1.0f;
    s.font_size = 16.0f;
    s.font_weight = AETHER_FONT_WEIGHT_NORMAL;
    s.line_height = 1.25f;
    s.overflow = AETHER_OVERFLOW_VISIBLE;
    s.visible = AETHER_TRUE;
    s.border_radius = aether_css_length_zero(); // CSS initial value for border-radius is 0, not auto
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
        s.cursor = parent->cursor;
    }
    return s;
}

AETHER_API void aether_computed_style_free(aether_computed_style_t* s) {
    if (s) {
        aether_linear_gradient_free(&s->background_gradient);
        aether_linear_gradient_free(&s->text_gradient);
    }
}
