#include "../src/layout/box_model.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) { if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
static bool same(float left, float right) { return std::fabs(left - right) < .001f; }

int main() {
    // CSS 2.1 §8.3.1: maximum positive plus most-negative adjoining margin.
    require(same(aether_collapse_adjoining_margins(20, 30), 30), "positive margins must collapse to their maximum");
    require(same(aether_collapse_adjoining_margins(20, -10), 10), "mixed margins must add the most-negative value");
    require(same(aether_collapse_adjoining_margins(-20, -10), -20), "negative margins must collapse to their minimum");
    require(same(aether_collapse_adjoining_margins(20, -50), -30), "larger negative margin must pull following block upward");
    // Previous margin-box ends at 120 with a 25px bottom margin; next top margin is 20.
    require(same(aether_next_block_margin_box_y(120, 25, 20), 100), "collapsed sibling margin must not sum both margins");
    const aether_css_edges edges = {{-5, AETHER_CSS_UNIT_PX}, {10, AETHER_CSS_UNIT_PERCENT},
                                    {2, AETHER_CSS_UNIT_EM}, {3, AETHER_CSS_UNIT_PX}};
    const auto margin = aether_resolve_box_edges(edges, 200, 12, 16, 1000, 800, true);
    const auto padding = aether_resolve_box_edges(edges, 200, 12, 16, 1000, 800, false);
    require(same(margin.left, -5) && same(margin.top, 20) && same(margin.right, 24) && same(margin.bottom, 3),
        "margin edge resolution must preserve negative values and resolve percentage/em values");
    require(same(padding.left, 0) && same(padding.top, 20) && same(padding.right, 24) && same(padding.bottom, 3),
        "padding and border edge resolution must clamp negative values");
    require(aether_box_has_before_decoration({0, 1, 0, 0}, {0, 0, 0, 0}),
        "top padding must structurally separate adjoining margins");
    require(aether_box_has_after_decoration({0, 0, 0, 0}, {0, 0, 0, 1}),
        "bottom border must structurally separate adjoining margins");
    require(!aether_box_has_before_decoration({0, 0, 0, 0}, {0, 0, 0, 0}) &&
            !aether_box_has_after_decoration({0, 0, 0, 0}, {0, 0, 0, 0}),
        "zero decorations must not inhibit adjoining margins");
    const auto centered = aether_resolve_auto_margin_pair(100, 40, 0, 0, true, true);
    require(same(centered.before, 30) && same(centered.after, 30),
        "two auto margins must split positive free space equally");
    const auto trailing = aether_resolve_auto_margin_pair(100, 40, 10, 0, false, true);
    require(same(trailing.before, 10) && same(trailing.after, 50),
        "one auto margin must absorb all positive free space");
    const auto overflow = aether_resolve_auto_margin_pair(40, 60, 0, 0, true, true);
    require(same(overflow.before, 0) && same(overflow.after, 0),
        "auto margins must not become negative when content overflows");
    const auto fixed = aether_resolve_auto_margin_pair(100, 40, 10, 20, false, false);
    require(same(fixed.before, 10) && same(fixed.after, 20),
        "non-auto margins must remain unchanged");
    const auto zero_edges = aether_css_edges_zero();
    require(zero_edges.left.unit == AETHER_CSS_UNIT_PX && zero_edges.top.unit == AETHER_CSS_UNIT_PX &&
            zero_edges.right.unit == AETHER_CSS_UNIT_PX && zero_edges.bottom.unit == AETHER_CSS_UNIT_PX &&
            same(zero_edges.left.value, 0) && same(zero_edges.top.value, 0) &&
            same(zero_edges.right.value, 0) && same(zero_edges.bottom.value, 0),
        "zero edge factory must produce CSS 0px values rather than auto values");
    return 0;
}
