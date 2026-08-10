#include "form_controls.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool same(float actual, float expected) { return std::fabs(actual - expected) < .001f; }

int main() {
    // Single-line minimum content height centralizes the 1.1 font-size floor.
    require(same(aether_control_min_content_height(16.f, 1.25f), 20.f),
        "min content height must honor a normal line-height (16 * 1.25)");
    require(same(aether_control_min_content_height(16.f, .5f), 17.6f),
        "min content height must floor a small line-height at 1.1 * font-size");

    // Opaque-control content height: measured wins when it clears the minimum.
    require(same(aether_control_opaque_content_height(30.f, 16.f, 1.25f,
            false, 0.f, AETHER_BOX_SIZING_CONTENT_BOX, 0.f, 0.f), 30.f),
        "measured opaque content height must win when above the minimum");
    require(same(aether_control_opaque_content_height(10.f, 16.f, 1.25f,
            false, 0.f, AETHER_BOX_SIZING_CONTENT_BOX, 0.f, 0.f), 20.f),
        "measured opaque content height must rise to the single-line minimum");
    // Explicit height: content-box is used as-is, border-box subtracts padding and border.
    require(same(aether_control_opaque_content_height(0.f, 16.f, 1.25f,
            true, 40.f, AETHER_BOX_SIZING_CONTENT_BOX, 0.f, 0.f), 40.f),
        "explicit content-box height must pass through unchanged");
    require(same(aether_control_opaque_content_height(0.f, 16.f, 1.25f,
            true, 40.f, AETHER_BOX_SIZING_BORDER_BOX, 10.f, 2.f), 28.f),
        "explicit border-box height must subtract vertical padding and border");
    require(same(aether_control_opaque_content_height(0.f, 16.f, 1.25f,
            true, 5.f, AETHER_BOX_SIZING_CONTENT_BOX, 0.f, 0.f), 17.6f),
        "an explicit height below the 1.1 floor must still be floored");
    require(same(aether_control_opaque_content_height(0.f, 16.f, 1.25f,
            true, 40.f, AETHER_BOX_SIZING_BORDER_BOX, 60.f, 4.f), 17.6f),
        "border-box padding/border cannot drive the content height below the floor");

    // Flow-content centering: symmetric delta, negative on overflow.
    require(same(aether_control_flow_centering_offset(32.f, 30.f), 1.f),
        "shorter content must shift down into a definite height");
    require(same(aether_control_flow_centering_offset(27.f, 30.f), -1.5f),
        "taller content must shift up symmetrically on overflow");
    require(same(aether_control_flow_centering_offset(30.f, 30.f), 0.f),
        "content exactly filling a definite height must not shift");
    return 0;
}
