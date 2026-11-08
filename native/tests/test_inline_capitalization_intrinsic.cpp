// Private sizing supplement reuses the public fixture and authentic core objects.
// The independently built public regression still includes only installed headers.
#include "../src/layout/layout_internal.h"
#define main public_capitalization_regression_main
#include "test_inline_capitalization.cpp"
#undef main

static float intrinsic(Fixture& fixture, bool height, aether_intrinsic_size_mode_e mode,
                       float available_width = 200.f) {
    aether_layout_result_s provisional;
    provisional.build_overlay = std::make_unique<aether::layout::MutableLayoutOverlay>();
    const aether::layout::StyleLookup lookup(fixture.map->current,
        provisional.build_overlay.get(), fixture.result->assets->default_style);
    const BoxStyleSnapshot style(lookup.lookup_node_owner(fixture.node()));
    fixture.engine->measure_styles = &lookup;
    fixture.engine->measure_document = fixture.doc->identity;
    const float value = height
        ? measure_intrinsic_height(fixture.engine, &lookup, &provisional, fixture.node(),
            style, {}, {}, available_width)
        : measure_intrinsic_width(fixture.engine, &lookup, fixture.node(), style,
            {}, {}, available_width, &provisional, mode);
    fixture.engine->measure_styles = nullptr;
    fixture.engine->measure_document.reset();
    CHECK(provisional.primary_boxes.empty());
    CHECK(provisional.paint_order.empty());
    return value;
}

int main() {
    using Mode = aether_intrinsic_size_mode_e;
    scenario = "intrinsic split";
    Fixture split("hel<span>lo</span>");
    CHECK(near(intrinsic(split, false, Mode::max_content), 41));
    CHECK(near(intrinsic(split, false, Mode::min_content), 27));
    CHECK(near(intrinsic(split, true, Mode::max_content, 44), 20));
    CHECK(std::find(split.meter.calls.begin(), split.meter.calls.end(), "lo") != split.meter.calls.end());
    CHECK(std::find(split.meter.calls.begin(), split.meter.calls.end(), "Lo") == split.meter.calls.end());
    scenario = "intrinsic unsplit";
    Fixture unsplit("hello");
    CHECK(near(intrinsic(unsplit, false, Mode::max_content), 41));
    CHECK(near(intrinsic(unsplit, false, Mode::min_content), 41));
    scenario = "intrinsic mixed";
    Fixture mixed("<span style='text-transform:none'>hel</span><span>lo</span>");
    CHECK(near(intrinsic(mixed, false, Mode::max_content), 35));
    CHECK(near(intrinsic(mixed, false, Mode::min_content), 21));
    scenario = "intrinsic word boundary";
    Fixture boundary("hel <span>lo</span>");
    Fixture joined("hel lo");
    for (auto mode : {Mode::min_content, Mode::max_content})
        CHECK(near(intrinsic(boundary, false, mode), intrinsic(joined, false, mode)));
    std::printf("intrinsic fixtures=%u checks=%u failures=%u\n", fixtures, checks, failures);
    return failures ? 1 : 0;
}
