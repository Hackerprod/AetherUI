#include "../src/css/text_transform.h"

#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) {
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
    return 0;
}
