#include "../src/css/text_transform.h"

#include <cstdio>
#include <cstdlib>

static unsigned checks = 0;
static void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    require(aether_apply_text_transform("heart rate", AETHER_TEXT_TRANSFORM_UPPERCASE) == "HEART RATE",
        "uppercase must use the canonical transformed text");
    require(aether_apply_text_transform("HEART RATE", AETHER_TEXT_TRANSFORM_LOWERCASE) == "heart rate",
        "lowercase must use the canonical transformed text");
    require(aether_apply_text_transform("heart rate\tBPM", AETHER_TEXT_TRANSFORM_CAPITALIZE) == "Heart Rate\tBPM",
        "capitalize must restart at every whitespace boundary");
    require(aether_apply_text_transform("same", AETHER_TEXT_TRANSFORM_NONE) == "same",
        "none must preserve source text");
    require(aether_apply_text_transform("lo", AETHER_TEXT_TRANSFORM_CAPITALIZE, false) == "lo", "continuation seed");
    require(aether_apply_text_transform("lo next", AETHER_TEXT_TRANSFORM_CAPITALIZE, false) == "lo Next", "internal whitespace");
    require(aether_apply_text_transform("lo", AETHER_TEXT_TRANSFORM_CAPITALIZE) == "Lo", "isolated wrapper remains fresh");
    require(aether_apply_text_transform("", AETHER_TEXT_TRANSFORM_CAPITALIZE, false).empty(), "empty seed");
    require(aether_apply_text_transform("LO", AETHER_TEXT_TRANSFORM_LOWERCASE, false) == "lo", "lowercase ignores seed");
    require(aether_apply_text_transform("lo", AETHER_TEXT_TRANSFORM_UPPERCASE, false) == "LO", "uppercase ignores seed");
    require(aether_apply_text_transform("lo", AETHER_TEXT_TRANSFORM_NONE, true) == "lo", "none ignores seed");
    std::printf("text-transform checks=%u failures=0\n", checks);
    return 0;
}
