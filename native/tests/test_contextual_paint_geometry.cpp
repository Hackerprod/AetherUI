#include <aether/aether.h>
#include "css/border_radii.h"
#include "rendering/background_image_geometry.h"
#include "rendering/border_painter.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <vector>
#define REQUIRE(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); std::abort(); } } while (false)
static void equal(float a, float b) { REQUIRE(std::fabs(a-b)<.01f); }
int main() {
    for (const char* expression : {"50%", "calc(50%)", "calc(0px + 50%)"}) {
        aether_background_image_geometry_context_s c{};
        c.paint_rect={0,0,400,100}; c.image_size={100,100};
        REQUIRE(aether_css_length_try_parse(expression,&c.position_x));
        c.position_y=aether_css_length_zero(); c.size_x=c.size_y=aether_css_length_auto();
        c.repeat_x=c.repeat_y=AETHER_BACKGROUND_NO_REPEAT;
        std::vector<aether_background_image_tile_s> tiles;
        REQUIRE(aether_background_image_resolve_geometry(c,tiles)); equal(tiles[0].tile_rect.x,150);
    }
    aether_border_radii specified{};
    REQUIRE(aether_border_radii_try_parse("1vw",&specified));
    equal(aether_border_radii_resolve(specified,{0,0,100,100},10,32,800,600).top_left_x,8);
    REQUIRE(aether_border_radii_try_parse("1rem",&specified));
    equal(aether_border_radii_resolve(specified,{0,0,100,100},10,32,800,600).top_left_x,32);
    auto style=aether_computed_style_default();
    style.border_styles={AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID};
    const aether_color_rgba color{1,0,0,1}; style.border_colors={color,color,color,color};
    std::vector<aether_paint_command_t> commands;
    aether_paint_border(style,{0,0,100,100},{},{8,8,8,8},commands);
    REQUIRE(commands.size()==1); REQUIRE(commands[0].tag==AETHER_PAINT_STROKE_ROUNDED_RECT);
    equal(commands[0].data.stroke_rounded_rect.stroke_width,8);
    aether_computed_style_free(&style);
    std::puts("contextual paint geometry PASS");
}
