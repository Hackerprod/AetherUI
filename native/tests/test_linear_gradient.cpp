#include "../src/css/linear_gradient.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool same(float left, float right) { return std::fabs(left - right) < .001f; }

static void release(aether_linear_gradient& gradient) { aether_linear_gradient_free(&gradient); }

int main() {
    aether_linear_gradient gradient{};
    aether_linear_gradient_parse_info_s info{};
    require(aether_linear_gradient_try_parse("linear-gradient(to right, red, blue)", &gradient, &info),
        "keyword direction gradient must parse");
    require(same(gradient.direction_x, 1) && same(gradient.direction_y, 0) && gradient.stop_count == 2,
        "to right must produce a rightward two-stop gradient");
    require(same(gradient.stops[0].offset, 0) && same(gradient.stops[1].offset, 1),
        "implicit endpoint stops must resolve to zero and one");
    require(!info.uses_fixed_corner_direction, "cardinal keyword direction must not use a corner approximation");
    release(gradient);

    require(aether_linear_gradient_try_parse("linear-gradient(90deg, red 20%, green, blue 80%)", &gradient),
        "numeric angle gradient with percentage stops must parse");
    require(same(gradient.direction_x, 1) && same(gradient.direction_y, 0),
        "90deg must point right under the CSS angle convention");
    require(same(gradient.stops[0].offset, .2f) && same(gradient.stops[1].offset, .5f) &&
            same(gradient.stops[2].offset, .8f),
        "implicit stop runs must distribute uniformly between percentage stops");
    release(gradient);

    require(aether_linear_gradient_try_parse("linear-gradient(to bottom left, red 70%, green 20%, blue)", &gradient, &info),
        "diagonal gradient with descending positions must parse");
    require(same(gradient.direction_x, -.70710678f) && same(gradient.direction_y, .70710678f),
        "two-keyword directions must normalize diagonally");
    require(same(gradient.stops[0].offset, .7f) && same(gradient.stops[1].offset, .7f) &&
            same(gradient.stops[2].offset, 1.f),
        "descending explicit stops must clamp to the preceding used position");
    require(info.uses_fixed_corner_direction, "corner keyword direction must surface its fixed-vector approximation");
    release(gradient);

    require(aether_linear_gradient_try_parse("linear-gradient(red 70%, #00ff00, green 20%, blue)", &gradient),
        "implicit stops between descending explicit positions must parse");
    require(same(gradient.stops[0].offset, .7f) && same(gradient.stops[1].offset, .7f) &&
            same(gradient.stops[2].offset, .7f) && same(gradient.stops[3].offset, 1.f),
        "implicit stops must interpolate after explicit positions have been clamped monotonically");
    release(gradient);

    require(!aether_linear_gradient_try_parse("linear-gradient(to sideways, red, blue)", &gradient),
        "invalid directions must not be mistaken for color stops");
    return 0;
}
