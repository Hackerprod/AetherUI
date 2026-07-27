#include "../src/css/background_layers.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    aether_background_layer_list layers{};
    require(aether_background_image_list_try_parse(
        "url('asset://mountain'), linear-gradient(90deg, red, blue)", &layers),
        "image and gradient list should parse");
    require(layers.count == 2 && layers.items[0].kind == AETHER_BACKGROUND_LAYER_IMAGE_URL &&
                layers.items[1].kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT,
        "image list must retain declaration order and kinds");
    require(std::strcmp(layers.items[0].image_source, "asset://mountain") == 0,
        "url() must retain its unquoted UTF-8 source");
    require(std::fabs(layers.items[1].gradient.direction_x - 1.f) < .001f,
        "gradient layer must preserve its parsed direction");
    aether_background_layer_list_free(&layers);

    std::vector<aether_background_position_s> positions;
    require(aether_background_position_list_try_parse("center, right bottom", positions) && positions.size() == 2,
        "position list should parse center and keyword pairs");
    require(positions[0].x.value == 50.f && positions[0].y.value == 50.f &&
                positions[1].x.value == 100.f && positions[1].y.value == 100.f,
        "position keywords must retain their percentage semantics");

    std::vector<aether_background_size_s> sizes;
    require(aether_background_size_list_try_parse("cover, 20px auto", sizes) && sizes.size() == 2,
        "size list should parse cover and explicit dimensions");
    require(sizes[0].kind == AETHER_BACKGROUND_SIZE_COVER && sizes[1].kind == AETHER_BACKGROUND_SIZE_EXPLICIT &&
                sizes[1].x.value == 20.f && sizes[1].y.unit == AETHER_CSS_UNIT_AUTO,
        "background-size must preserve cover and auto dimensions");

    std::vector<aether_background_repeat_s> repeats;
    require(aether_background_repeat_list_try_parse("no-repeat, repeat-x", repeats) && repeats.size() == 2,
        "repeat list should parse axis shorthands");
    require(repeats[0].x == AETHER_BACKGROUND_NO_REPEAT && repeats[0].y == AETHER_BACKGROUND_NO_REPEAT &&
                repeats[1].x == AETHER_BACKGROUND_REPEAT && repeats[1].y == AETHER_BACKGROUND_NO_REPEAT,
        "repeat-x must expand to its two-axis semantic values");

    aether_background_shorthand_s shorthand{};
    require(aether_background_shorthand_try_parse("url('hero.png') center / cover no-repeat", shorthand),
        "single image background shorthand should parse");
    require(aether_background_shorthand_try_parse("linear-gradient(90deg, red, blue) left top / 20px auto repeat-x red", shorthand),
        "single gradient background shorthand should parse");
    require(aether_background_shorthand_try_parse(
        "url('hero.png') center / cover no-repeat, linear-gradient(90deg, red, blue) left top / 20px auto repeat-x red",
        shorthand), "background shorthand should decompose into longhands");
    require(shorthand.image == "url('hero.png'), linear-gradient(90deg, red, blue)" &&
                shorthand.position == "center, left top" && shorthand.size == "cover, 20px auto" &&
                shorthand.repeat == "no-repeat, repeat-x" && shorthand.has_color && shorthand.color == "red",
        "background shorthand must preserve each layer's independent values");

    require(!aether_background_image_list_try_parse("url(", &layers),
        "malformed url() must be rejected instead of becoming a resource reference");
    return 0;
}
