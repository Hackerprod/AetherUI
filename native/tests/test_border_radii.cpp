#include "../src/css/border_radii.h"
#include "../src/rendering/border_painter.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    // CSS Backgrounds & Borders §4.1/§4.5: corner axes, shorthand expansion,
    // and the single scale factor used when neighbouring radii overlap.
    aether_border_radii radii{};
    require(aether_border_radii_try_parse("20px 30px 40px 50px / 10px 15px 20px 25px", &radii),
        "elliptical per-corner border-radius did not parse");
    auto resolved = aether_border_radii_resolve(radii, {0, 0, 100, 80}, 16);
    require(std::fabs(resolved.top_left_x - 20) < .01f && std::fabs(resolved.top_left_y - 10) < .01f &&
            std::fabs(resolved.bottom_left_x - 50) < .01f && std::fabs(resolved.bottom_left_y - 25) < .01f,
        "per-corner radii lost independent axes");
    require(aether_border_radii_try_parse("80px 80px", &radii), "two-value border-radius did not parse");
    resolved = aether_border_radii_resolve(radii, {0, 0, 100, 50}, 16);
    require(std::fabs(resolved.top_left_x - 25) < .01f && std::fabs(resolved.top_left_y - 25) < .01f,
        "overlapping corner radii were not reduced by the common scale factor");

    // A painter conformance case: asymmetric solid borders must carry a single
    // outer/inner ring geometry per side, never four overlapping rectangular fills.
    aether_computed_style_t style = aether_computed_style_default();
    style.opacity = 1;
    style.border_width = { {16, AETHER_CSS_UNIT_PX}, {4, AETHER_CSS_UNIT_PX}, {8, AETHER_CSS_UNIT_PX}, {12, AETHER_CSS_UNIT_PX} };
    style.border_styles = {AETHER_BORDER_STYLE_SOLID, AETHER_BORDER_STYLE_SOLID, AETHER_BORDER_STYLE_SOLID, AETHER_BORDER_STYLE_SOLID};
    style.border_colors = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,0,1}};
    require(aether_border_radii_try_parse("24px 12px 18px 30px / 10px 20px 8px 16px", &style.border_radii), "painter radii did not parse");
    const auto outer = aether_border_radii_resolve(style.border_radii, {0,0,200,100}, 16);
    std::vector<aether_paint_command_t> commands;
    aether_paint_border(style, {0,0,200,100}, outer, commands);
    require(commands.size() == 4, "asymmetric border must emit one ring-side command per side");
    for (int i = 0; i < 4; ++i) {
        require(commands[i].tag == AETHER_PAINT_FILL_BORDER_SIDE, "asymmetric border emitted a rectangular fill");
        require(commands[i].data.fill_border_side.side == i, "border side command order is unstable");
        require(std::fabs(commands[i].data.fill_border_side.inner_rect.x - 16) < .01f &&
                std::fabs(commands[i].data.fill_border_side.inner_rect.y - 4) < .01f &&
                std::fabs(commands[i].data.fill_border_side.inner_rect.width - 176) < .01f &&
                std::fabs(commands[i].data.fill_border_side.inner_rect.height - 84) < .01f,
                "inner border ring box is incorrect");
    }
    const auto& top = commands[0].data.fill_border_side;
    require(std::fabs(top.inner_radii.top_left_x - 8) < .01f && std::fabs(top.inner_radii.top_left_y - 6) < .01f &&
            std::fabs(top.inner_radii.top_right_x - 4) < .01f && std::fabs(top.inner_radii.top_right_y - 16) < .01f,
        "inner elliptical corner radii are incorrect");
    // The top-left diagonal partition is the segment outer(top-left)->inner(top-left).
    require(std::fabs(top.outer_rect.x) < .01f && std::fabs(top.outer_rect.y) < .01f &&
            std::fabs(top.inner_rect.x - 16) < .01f && std::fabs(top.inner_rect.y - 4) < .01f,
        "top-left corner transition endpoints are incorrect");
    return 0;
}
