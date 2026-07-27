#include "../src/layout/text_justify.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    require(std::fabs(aether_text_justify_extra_per_gap(100.f, 80.f, 2) - 10.f) < 0.001f,
        "two gaps must share available extra width");
    require(std::fabs(aether_text_justify_extra_per_gap(100.f, 76.f, 3) - 8.f) < 0.001f,
        "three gaps must share available extra width");
    require(aether_text_justify_extra_per_gap(100.f, 80.f, 0) == 0.f,
        "a line without gaps must not divide by zero");
    require(aether_text_justify_extra_per_gap(100.f, 100.f, 2) == 0.f,
        "an exactly filled line must not move its gaps");
    require(aether_text_justify_extra_per_gap(100.f, 120.f, 2) == 0.f,
        "an overflowing line must not compress its gaps");

    require(aether_text_line_should_justify(AETHER_TEXT_ALIGN_JUSTIFY, false, false, 2, 100.f, 80.f),
        "a non-terminal line with distributable spaces must justify");
    require(!aether_text_line_should_justify(AETHER_TEXT_ALIGN_JUSTIFY, true, false, 2, 100.f, 80.f),
        "the final line must not justify by default");
    require(!aether_text_line_should_justify(AETHER_TEXT_ALIGN_JUSTIFY, false, true, 2, 100.f, 80.f),
        "a line terminated by a forced break must not justify by default");
    require(!aether_text_line_should_justify(AETHER_TEXT_ALIGN_JUSTIFY, false, false, 0, 100.f, 80.f),
        "a line with no distributable space must not justify");
    require(!aether_text_line_should_justify(AETHER_TEXT_ALIGN_CENTER, false, false, 2, 100.f, 80.f),
        "other text alignment modes must not justify");
    return 0;
}
