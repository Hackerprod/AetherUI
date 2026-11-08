#include "../src/css/border_radii.h"
#include "../src/rendering/border_painter.h"
#include <cmath>
#include <array>
#include <cfenv>
#include <limits>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}


using Axes = std::array<float, 8>;
static Axes axes(aether_corner_radii r) {
    return {r.top_left_x, r.top_left_y, r.top_right_x, r.top_right_y,
            r.bottom_right_x, r.bottom_right_y, r.bottom_left_x, r.bottom_left_y};
}
static aether_border_radii typed(Axes v) {
    return {{{v[0], AETHER_CSS_UNIT_PX}, {v[1], AETHER_CSS_UNIT_PX}},
            {{v[2], AETHER_CSS_UNIT_PX}, {v[3], AETHER_CSS_UNIT_PX}},
            {{v[4], AETHER_CSS_UNIT_PX}, {v[5], AETHER_CSS_UNIT_PX}},
            {{v[6], AETHER_CSS_UNIT_PX}, {v[7], AETHER_CSS_UNIT_PX}}};
}
static void exact(aether_corner_radii actual, Axes expected, const char* message) {
    const auto a = axes(actual);
    for (size_t i = 0; i < a.size(); ++i) {
        if (!std::isfinite(a[i]) || a[i] != expected[i])
            std::fprintf(stderr, "%s axis %zu: actual %a expected %a\n", message, i, a[i], expected[i]);
        require(std::isfinite(a[i]) && std::isfinite(expected[i]) && a[i] == expected[i], message);
    }
}
static void normalization_range() {
    struct Case { const char* name; float width, height; Axes input, expected; };
    // All expectations are literal vectors, independent of production arithmetic.
    const Case cases[] = {
        {"ordinary large",100,50,{9999,9999,9999,9999,9999,9999,9999,9999},{25,25,25,25,25,25,25,25}},
        {"finite overflow pairs",100,50,{3e38f,3e38f,3e38f,3e38f,3e38f,3e38f,3e38f,3e38f},{25,25,25,25,25,25,25,25}},
        {"top limits",64,64,{48,8,80,12,8,16,12,20},{24,4,40,6,4,8,6,10}},
        {"bottom limits",64,64,{8,8,12,12,80,16,48,20},{4,4,6,6,40,8,24,10}},
        {"left limits",64,64,{8,48,12,8,16,12,20,80},{4,24,6,4,8,6,10,40}},
        {"right limits",64,64,{8,8,12,48,16,80,20,12},{4,4,6,24,8,40,10,6}},
        {"four-way tie",100,50,{80,40,80,40,80,40,80,40},{50,25,50,25,50,25,50,25}},
        {"huge horizontal",128,64,{0x1p127f,8,0x1p127f,16,0x1p127f,24,0x1p127f,32},
            {64,0x1p-118f,64,0x1p-117f,64,0x1.8p-117f,64,0x1p-116f}},
        {"huge top only",128,64,{0x1p127f,8,0x1p127f,16,24,24,32,32},
            {64,0x1p-118f,64,0x1p-117f,0x1.8p-117f,0x1.8p-117f,0x1p-116f,0x1p-116f}},
        {"huge bottom only",128,64,{24,8,32,16,0x1p127f,24,0x1p127f,32},
            {0x1.8p-117f,0x1p-118f,0x1p-116f,0x1p-117f,64,0x1.8p-117f,64,0x1p-116f}},
        {"huge left only",64,128,{8,0x1p127f,16,24,24,32,32,0x1p127f},
            {0x1p-118f,64,0x1p-117f,0x1.8p-117f,0x1.8p-117f,0x1p-116f,0x1p-116f,64}},
        {"huge right only",64,128,{8,24,16,0x1p127f,24,0x1p127f,32,32},
            {0x1p-118f,0x1.8p-117f,0x1p-117f,64,0x1.8p-117f,64,0x1p-116f,0x1p-116f}},
        {"tiny no overlap",0x1p-20f,0x1p-20f,
            {0x1p-24f,0x1p-24f,0x1p-24f,0x1p-24f,0x1p-24f,0x1p-24f,0x1p-24f,0x1p-24f},
            {0x1p-24f,0x1p-24f,0x1p-24f,0x1p-24f,0x1p-24f,0x1p-24f,0x1p-24f,0x1p-24f}},
        {"tiny x zero",0x1p-20f,0x1p-20f,{0,0x1p-24f,0,0x1p-24f,0,0x1p-24f,0,0x1p-24f},
            {0,0x1p-24f,0,0x1p-24f,0,0x1p-24f,0,0x1p-24f}},
        {"tiny y zero",0x1p-20f,0x1p-20f,{0x1p-24f,0,0x1p-24f,0,0x1p-24f,0,0x1p-24f,0},
            {0x1p-24f,0,0x1p-24f,0,0x1p-24f,0,0x1p-24f,0}},
        {"tiny overlap",0x1p-20f,0x1p-20f,
            {0x1p-20f,0x1p-20f,0x1p-20f,0x1p-20f,0x1p-20f,0x1p-20f,0x1p-20f,0x1p-20f},
            {0x1p-21f,0x1p-21f,0x1p-21f,0x1p-21f,0x1p-21f,0x1p-21f,0x1p-21f,0x1p-21f}},
        {"exact touching",64,32,{32,16,32,16,32,16,32,16},{32,16,32,16,32,16,32,16}},
        {"factor underflow",0x1p-120f,0x1p-120f,
            {0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f},
            {0x1p-121f,0x1p-121f,0x1p-121f,0x1p-121f,0x1p-121f,0x1p-121f,0x1p-121f,0x1p-121f}},
        {"factor underflow asymmetric",0x1p-120f,0x1p-120f,
            {0x1p120f,0x1p119f,0x1p120f,0x1p119f,0x1p119f,0x1p120f,0x1p119f,0x1p120f},
            {0x1p-121f,0x1p-122f,0x1p-121f,0x1p-122f,0x1p-122f,0x1p-121f,0x1p-122f,0x1p-121f}},
        {"legitimate final underflow",0x1p-120f,0x1p-120f,
            {0x1p120f,0x1p-149f,0x1p120f,0x1p119f,0x1p119f,0x1p120f,0x1p119f,0x1p120f},
            {0x1p-121f,0,0x1p-121f,0x1p-122f,0x1p-122f,0x1p-121f,0x1p-122f,0x1p-121f}},
        {"subnormal output",0x1p-148f,0x1p-148f,
            {0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f},
            {0x1p-149f,0x1p-149f,0x1p-149f,0x1p-149f,0x1p-149f,0x1p-149f,0x1p-149f,0x1p-149f}},
        {"half minimum ties even",0x1p-149f,0x1p-149f,
            {0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f,0x1p120f},{}},
        {"zero radii tiny box",0x1p-120f,0x1p-120f,{},{}},
        {"zero width positive sum",0,64,{4,4,8,8,12,12,16,16},{}},
        {"zero height positive sum",64,0,{4,4,8,8,12,12,16,16},{}},
        {"zero width zero sum",0,64,{0,4,0,8,0,12,0,16},{0,4,0,8,0,12,0,16}},
        {"zero height zero sum",64,0,{4,0,8,0,12,0,16,0},{4,0,8,0,12,0,16,0}},
        {"negative zero width",-0.f,64,{0,4,0,8,0,12,0,16},{0,4,0,8,0,12,0,16}},
        {"both dimensions zero",0,0,{0,4,0,8,0,12,0,16},{}},
        {"independent corner axes",100,50,{0,20,0,0,0,0,0,0},{0,20,0,0,0,0,0,0}}
    };
    require(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
            "normalization vectors require IEEE floats");
    require(std::fegetround() == FE_TONEAREST, "subnormal oracle requires existing nearest rounding");
    for (const auto& c : cases) {
        const auto result = aether_border_radii_resolve(typed(c.input), {0,0,c.width,c.height}, 16);
        exact(result, c.expected, c.name);
        const auto a = axes(result);
        for (size_t i = 0; i < a.size(); ++i)
            require(a[i] >= 0 && a[i] <= c.input[i], "normalization never grows a nonnegative axis");
        const double allowance_x = 4 * std::numeric_limits<float>::epsilon() * double(c.width) + 2 * double(0x1p-149f);
        const double allowance_y = 4 * std::numeric_limits<float>::epsilon() * double(c.height) + 2 * double(0x1p-149f);
        require(double(a[0])+a[2] <= c.width+allowance_x && double(a[6])+a[4] <= c.width+allowance_x &&
                double(a[1])+a[7] <= c.height+allowance_y && double(a[3])+a[5] <= c.height+allowance_y,
                "normalized pairs fit to final float rounding");
    }
    const float max = std::numeric_limits<float>::max();
    exact(aether_border_radii_resolve(typed({max,max,max,max,max,max,max,max}), {0,0,100,50},16),
          {25,25,25,25,25,25,25,25}, "maximum finite PX");
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (float invalid : {-1.f,nan,inf,-inf}) {
        exact(aether_border_radii_resolve(typed({4,4,8,8,12,12,16,16}),{0,0,invalid,64},16),{},"invalid width");
        exact(aether_border_radii_resolve(typed({4,4,8,8,12,12,16,16}),{0,0,64,invalid},16),{},"invalid height");
    }
    exact(aether_border_radii_resolve(typed({4,4,8,8,12,12,16,16}),{nan,inf,64,64},16),
          {4,4,8,8,12,12,16,16},"position does not constrain radii");
    for (size_t slot = 0; slot < 8; ++slot) {
        for (aether_css_length invalid : {aether_css_length{-1,AETHER_CSS_UNIT_PX},
             aether_css_length{nan,AETHER_CSS_UNIT_PX},aether_css_length{inf,AETHER_CSS_UNIT_PX},
             aether_css_length{-inf,AETHER_CSS_UNIT_PX},aether_css_length{0,AETHER_CSS_UNIT_AUTO},
             aether_css_length{max,AETHER_CSS_UNIT_EM}}) {
            auto r = typed({4,8,12,16,20,24,28,32});
            aether_css_length* fields[] = {&r.top_left.horizontal,&r.top_left.vertical,&r.top_right.horizontal,&r.top_right.vertical,
                &r.bottom_right.horizontal,&r.bottom_right.vertical,&r.bottom_left.horizontal,&r.bottom_left.vertical};
            *fields[slot] = invalid;
            Axes expected{4,8,12,16,20,24,28,32}; expected[slot] = 0;
            exact(aether_border_radii_resolve(r,{0,0,100,100},16),expected,"only invalid resolved axis becomes zero");
        }
    }
    aether_border_radii r{};
    require(aether_border_radii_try_parse("50% / 50%",&r),"percentage parse");
    exact(aether_border_radii_resolve(r,{0,0,100,50},16),{50,25,50,25,50,25,50,25},"percentage axis reference");
    require(aether_border_radii_try_parse("2em / 1rem",&r),"raw contextual parse");
    exact(aether_border_radii_resolve(r,{0,0,100,50},10,12,300,200),{20,12,20,12,20,12,20,12},"raw contextual axes");
    require(aether_border_radii_try_parse("3e38px",&r),"finite huge parse");
    const auto outer = aether_border_radii_resolve(r,{0,0,100,50},16);
    exact(aether_corner_radii_inset(outer,3,5,7,11),{22,20,18,20,18,14,22,14},"normalize before inset");
    exact(aether_corner_radii_inset(outer,30,5,7,40),{0,20,18,20,18,0,0,0},"inset clamps individual axes");
    exact(aether_corner_radii_inset(outer,0,0,0,0),{25,25,25,25,25,25,25,25},"zero inset");
}

int main() {
    normalization_range();
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
