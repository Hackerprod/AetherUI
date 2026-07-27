#include "../src/rendering/background_image_geometry.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool equal(float left, float right) { return std::fabs(left - right) < .001f; }
static aether_css_length px(float value) { return {value, AETHER_CSS_UNIT_PX, 0, 0, 0, 0, 0}; }
static aether_css_length percent(float value) { return {value, AETHER_CSS_UNIT_PERCENT, 0, 0, 0, 0, 0}; }

static aether_background_image_geometry_context_s base_context() {
    aether_background_image_geometry_context_s context{};
    context.paint_rect = {0, 0, 400, 100};
    context.image_size = {100, 100};
    context.position_x = percent(50);
    context.position_y = percent(50);
    context.size_kind = AETHER_BACKGROUND_SIZE_AUTO;
    context.size_x = aether_css_length_auto();
    context.size_y = aether_css_length_auto();
    context.repeat_x = AETHER_BACKGROUND_NO_REPEAT;
    context.repeat_y = AETHER_BACKGROUND_NO_REPEAT;
    return context;
}

int main() {
    std::vector<aether_background_image_tile_s> tiles;

    {
        auto context = base_context();
        context.size_kind = AETHER_BACKGROUND_SIZE_COVER;
        require(aether_background_image_resolve_geometry(context, tiles), "cover geometry must resolve");
        require(tiles.size() == 1, "cover with no-repeat must emit exactly one clipped tile");
        const auto& tile = tiles[0];
        require(equal(tile.destination.x, 0) && equal(tile.destination.y, 0) &&
                    equal(tile.destination.width, 400) && equal(tile.destination.height, 100),
            "cover destination must fill the painting area");
        require(equal(tile.source.x, 0) && equal(tile.source.y, 37.5f) &&
                    equal(tile.source.width, 100) && equal(tile.source.height, 25),
            "cover must crop the intrinsic source around the positioned center");
    }

    {
        auto context = base_context();
        context.size_kind = AETHER_BACKGROUND_SIZE_CONTAIN;
        require(aether_background_image_resolve_geometry(context, tiles), "contain geometry must resolve");
        require(tiles.size() == 1, "contain with no-repeat must emit one tile");
        const auto& tile = tiles[0];
        require(equal(tile.destination.x, 150) && equal(tile.destination.y, 0) &&
                    equal(tile.destination.width, 100) && equal(tile.destination.height, 100),
            "contain must preserve aspect ratio and apply background-position");
        require(equal(tile.source.x, 0) && equal(tile.source.y, 0) &&
                    equal(tile.source.width, 100) && equal(tile.source.height, 100),
            "contain must consume the full intrinsic source");
    }

    {
        auto context = base_context();
        context.paint_rect = {0, 0, 250, 100};
        context.position_x = px(0);
        context.position_y = px(0);
        context.repeat_x = AETHER_BACKGROUND_REPEAT;
        require(aether_background_image_resolve_geometry(context, tiles), "repeat geometry must resolve");
        require(tiles.size() == 3, "repeat-x must cover the area with clipped final tile");
        require(equal(tiles[2].destination.x, 200) && equal(tiles[2].destination.width, 50) &&
                    equal(tiles[2].source.width, 50),
            "repeat clipping must crop source and destination proportionally");
    }

    {
        auto context = base_context();
        context.paint_rect = {0, 0, 250, 100};
        context.position_x = px(0);
        context.position_y = px(0);
        context.repeat_x = AETHER_BACKGROUND_SPACE;
        require(aether_background_image_resolve_geometry(context, tiles), "space geometry must resolve");
        require(tiles.size() == 2 && equal(tiles[0].destination.x, 0) && equal(tiles[1].destination.x, 150),
            "space repeat must distribute leftover width between full tiles");

        context.repeat_x = AETHER_BACKGROUND_ROUND;
        require(aether_background_image_resolve_geometry(context, tiles), "round geometry must resolve");
        require(tiles.size() == 3 && equal(tiles[0].destination.width, 250.f / 3.f) &&
                    equal(tiles[2].destination.x + tiles[2].destination.width, 250),
            "round repeat must scale tiles to an integral fit");
    }

    {
        auto context = base_context();
        context.image_size = {0, 100};
        require(!aether_background_image_resolve_geometry(context, tiles) && tiles.empty(),
            "unknown intrinsic dimensions must not fabricate image geometry");
    }
    return 0;
}
