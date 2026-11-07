#include "border_painter.h"
#include "../css/border_utils.h"
#include "../css/border_radii.h"
#include "../css/css_types.h"
#include <algorithm>
#include <cmath>

static bool empty(aether_rectf r) { return r.width <= 0 || r.height <= 0; }
static float width(aether_css_length value, float reference, float font_size) {
    const float result = aether_css_length_resolve(value, reference, font_size, font_size, reference, reference);
    return std::isfinite(result) ? std::max(0.f, result) : 0.f;
}
static bool same(aether_color_rgba a, aether_color_rgba b) {
    return std::abs(a.r-b.r)<.001f && std::abs(a.g-b.g)<.001f && std::abs(a.b-b.b)<.001f && std::abs(a.a-b.a)<.001f;
}
static void fill(std::vector<aether_paint_command_t>& out, aether_rectf rect, aether_color_rgba color) {
    if (empty(rect) || color.a <= 0) return;
    aether_paint_command_t command{}; command.tag = AETHER_PAINT_FILL_ROUNDED_RECT;
    command.data.fill_rounded_rect = {rect, {}, color}; out.push_back(command);
}
static void clip(std::vector<aether_paint_command_t>& out, aether_rectf rect, aether_corner_radii radii) {
    aether_paint_command_t command{}; command.tag = AETHER_PAINT_PUSH_CLIP; command.data.push_clip = {rect, radii}; out.push_back(command);
}
static void edge(std::vector<aether_paint_command_t>& out, aether_rectf rect, aether_border_side_t side, int32_t style, aether_color_rgba color) {
    if (empty(rect) || color.a <= 0 || !aether_border_style_occupies_space(style)) return;
    const bool horizontal = side == aether_border_side_t::top || side == aether_border_side_t::bottom;
    const float thickness = horizontal ? rect.height : rect.width;
    if (style == AETHER_BORDER_STYLE_DOTTED || style == AETHER_BORDER_STYLE_DASHED) {
        const float segment = style == AETHER_BORDER_STYLE_DOTTED ? thickness : std::max(3.f * thickness, 3.f);
        const float length = horizontal ? rect.width : rect.height;
        for (float position = 0; position < length; position += segment + std::max(thickness, 1.f)) {
            aether_rectf part = horizontal ? aether_rectf{rect.x + position, rect.y, std::min(segment, length-position), rect.height}
                                           : aether_rectf{rect.x, rect.y + position, rect.width, std::min(segment, length-position)};
            fill(out, part, color);
        }
        return;
    }
    if (style == AETHER_BORDER_STYLE_DOUBLE && thickness >= 3.f) {
        const float band = thickness / 3.f;
        if (horizontal) { fill(out,{rect.x,rect.y,rect.width,band},color); fill(out,{rect.x,rect.y+rect.height-band,rect.width,band},color); }
        else { fill(out,{rect.x,rect.y,band,rect.height},color); fill(out,{rect.x+rect.width-band,rect.y,band,rect.height},color); }
        return;
    }
    const bool top_or_left = aether_border_side_is_top_or_left(side);
    auto shade = [](aether_color_rgba c, float f) { c.r=std::clamp(c.r*f,0.f,1.f); c.g=std::clamp(c.g*f,0.f,1.f); c.b=std::clamp(c.b*f,0.f,1.f); return c; };
    if (style == AETHER_BORDER_STYLE_INSET || style == AETHER_BORDER_STYLE_OUTSET) {
        const bool dark = style == AETHER_BORDER_STYLE_INSET ? top_or_left : !top_or_left; fill(out, rect, shade(color, dark ? .65f : 1.35f)); return;
    }
    if ((style == AETHER_BORDER_STYLE_GROOVE || style == AETHER_BORDER_STYLE_RIDGE) && thickness >= 2.f) {
        const float first = thickness / 2.f; const bool dark = style == AETHER_BORDER_STYLE_GROOVE ? top_or_left : !top_or_left;
        if (horizontal) { fill(out,{rect.x,rect.y,rect.width,first},shade(color,dark?.65f:1.35f)); fill(out,{rect.x,rect.y+first,rect.width,rect.height-first},shade(color,dark?1.35f:.65f)); }
        else { fill(out,{rect.x,rect.y,first,rect.height},shade(color,dark?.65f:1.35f)); fill(out,{rect.x+first,rect.y,rect.width-first,rect.height},shade(color,dark?1.35f:.65f)); }
        return;
    }
    fill(out, rect, color);
}

static void emit_side(std::vector<aether_paint_command_t>& out, aether_rectf outer,
                      aether_corner_radii outer_radii, aether_rectf inner,
                      aether_corner_radii inner_radii, aether_border_side_t side,
                      int32_t style, aether_color_rgba color) {
    if (color.a <= 0 || !aether_border_style_occupies_space(style)) return;
    aether_paint_command_t command{};
    command.tag = AETHER_PAINT_FILL_BORDER_SIDE;
    command.data.fill_border_side = {outer, outer_radii, inner, inner_radii,
                                     static_cast<int32_t>(side), style, color};
    out.push_back(command);
}

void aether_paint_border(const aether_computed_style_t& style, aether_rectf box, aether_corner_radii radii,
                         const aether_thicknessf& used_border, std::vector<aether_paint_command_t>& out) {
    const float top = used_border.top, right = used_border.right;
    const float bottom = used_border.bottom, left = used_border.left;
    const auto opacity = [&](aether_color_rgba c) { c.a *= style.opacity; return c; };
    const auto tc=opacity(style.border_colors.top), rc=opacity(style.border_colors.right), bc=opacity(style.border_colors.bottom), lc=opacity(style.border_colors.left);
    if ((top<=0 && right<=0 && bottom<=0 && left<=0) || (tc.a<=0 && rc.a<=0 && bc.a<=0 && lc.a<=0)) return;
    const bool uniform = style.border_styles.top == AETHER_BORDER_STYLE_SOLID && style.border_styles.right == AETHER_BORDER_STYLE_SOLID && style.border_styles.bottom == AETHER_BORDER_STYLE_SOLID && style.border_styles.left == AETHER_BORDER_STYLE_SOLID &&
        std::abs(top-right)<.001f && std::abs(top-bottom)<.001f && std::abs(top-left)<.001f && same(tc,rc) && same(tc,bc) && same(tc,lc);
    if (uniform) {
        const float inset = top / 2; aether_paint_command_t command{}; command.tag=AETHER_PAINT_STROKE_ROUNDED_RECT;
        command.data.stroke_rounded_rect={{box.x+inset,box.y+inset,std::max(0.f,box.width-top),std::max(0.f,box.height-top)}, aether_corner_radii_inset(radii,inset,inset,inset,inset), top, tc}; out.push_back(command); return;
    }
    const aether_rectf inner = {box.x + left, box.y + top,
                                std::max(0.f, box.width - left - right),
                                std::max(0.f, box.height - top - bottom)};
    const auto inner_radii = aether_corner_radii_inset(radii, left, top, right, bottom);
    emit_side(out, box, radii, inner, inner_radii, aether_border_side_t::top, style.border_styles.top, tc);
    emit_side(out, box, radii, inner, inner_radii, aether_border_side_t::right, style.border_styles.right, rc);
    emit_side(out, box, radii, inner, inner_radii, aether_border_side_t::bottom, style.border_styles.bottom, bc);
    emit_side(out, box, radii, inner, inner_radii, aether_border_side_t::left, style.border_styles.left, lc);
}

void aether_paint_border(const aether_computed_style_t& style, aether_rectf box, aether_corner_radii radii,
                         std::vector<aether_paint_command_t>& out) {
    const float top = aether_border_style_occupies_space(style.border_styles.top) ? width(style.border_width.top, box.width, style.font_size) : 0;
    const float right = aether_border_style_occupies_space(style.border_styles.right) ? width(style.border_width.right, box.width, style.font_size) : 0;
    const float bottom = aether_border_style_occupies_space(style.border_styles.bottom) ? width(style.border_width.bottom, box.width, style.font_size) : 0;
    const float left = aether_border_style_occupies_space(style.border_styles.left) ? width(style.border_width.left, box.width, style.font_size) : 0;
    aether_paint_border(style, box, radii, aether_thicknessf{left, top, right, bottom}, out);
}
