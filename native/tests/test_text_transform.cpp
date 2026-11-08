#include "../src/css/text_transform.h"
#include "../src/primitives/presentation_text.h"

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
    const std::string lower = "abcdefghijklmnopqrstuvwxyz";
    const std::string upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const std::string spaces = " \t\n\r\f\v";
    unsigned byte_cases = 0;
    for (unsigned value = 0; value < 256; ++value) {
        const char byte = static_cast<char>(value);
        const std::string source(1, byte);
        const auto low_index = lower.find(byte);
        const auto up_index = upper.find(byte);
        const std::string expected_upper(1, low_index == std::string::npos ? byte : upper[low_index]);
        const std::string expected_lower(1, up_index == std::string::npos ? byte : lower[up_index]);
        const bool is_space = spaces.find(byte) != std::string::npos;
        require(aether::presentation_space_byte(static_cast<unsigned char>(byte)) == is_space,
                "independent six-byte presentation whitespace oracle");
        require(aether::presentation_upper_byte(static_cast<unsigned char>(byte)) ==
                    static_cast<unsigned char>(expected_upper[0]), "independent uppercase alphabet oracle");
        require(aether::ascii_lower_byte(static_cast<unsigned char>(byte)) ==
                    static_cast<unsigned char>(expected_lower[0]), "independent lowercase alphabet oracle");
        for (bool seed : {false, true}) {
            require(aether_apply_text_transform(source, AETHER_TEXT_TRANSFORM_NONE, seed) == source, "all-byte NONE");
            require(aether_apply_text_transform(source, AETHER_TEXT_TRANSFORM_UPPERCASE, seed) == expected_upper, "all-byte upper");
            require(aether_apply_text_transform(source, AETHER_TEXT_TRANSFORM_LOWERCASE, seed) == expected_lower, "all-byte lower");
            require(aether_apply_text_transform(source, AETHER_TEXT_TRANSFORM_CAPITALIZE, seed) ==
                    (seed ? expected_upper : source), "all-byte capitalize seed");
        }
        const std::string sequence = std::string("a") + byte + "z";
        const std::string expected = std::string("A") + byte + (is_space ? "Z" : "z");
        require(aether_apply_text_transform(sequence, AETHER_TEXT_TRANSFORM_CAPITALIZE) == expected,
                "only six spaces restart capitalization");
        ++byte_cases;
    }
    require(byte_cases == 256, "complete byte domain");
    require(aether_apply_text_transform(std::string("a\0z", 3), AETHER_TEXT_TRANSFORM_CAPITALIZE) ==
            std::string("A\0z", 3), "private embedded NUL preserves bytes and seed");
    require(aether_apply_text_transform("1abc -abc a-b", AETHER_TEXT_TRANSFORM_CAPITALIZE) ==
            "1abc -abc A-b", "digits and punctuation consume seed");
    require(aether_apply_text_transform("A\xC3\x89", AETHER_TEXT_TRANSFORM_LOWERCASE) == "a\xC3\x89", "UTF-8 opaque lower");
    require(aether_apply_text_transform("i I", AETHER_TEXT_TRANSFORM_UPPERCASE) == "I I", "ASCII upper i/I");
    require(aether_apply_text_transform("i I", AETHER_TEXT_TRANSFORM_LOWERCASE) == "i i", "ASCII lower i/I");
    require(aether_apply_text_transform("\xC3\xA9" "clair", AETHER_TEXT_TRANSFORM_CAPITALIZE) ==
            "\xC3\xA9" "clair", "non-ASCII prefix consumes seed");
    require(aether_apply_text_transform("a\xC2\xA0" "b", AETHER_TEXT_TRANSFORM_CAPITALIZE) ==
            "A\xC2\xA0" "b", "NBSP does not restart seed");
    require(aether_apply_text_transform("a\r\nz", AETHER_TEXT_TRANSFORM_CAPITALIZE) == "A\r\nZ", "CRLF retained");
    require(aether_apply_text_transform("a\x80\xA0\xC3\xD0\xDD\xE9\xFF" "z", AETHER_TEXT_TRANSFORM_UPPERCASE) ==
            "A\x80\xA0\xC3\xD0\xDD\xE9\xFF" "Z", "adjacent high bytes unchanged");
    require(aether_apply_text_transform("a Z", -123, true) == "a Z", "unknown mode unchanged");
    std::printf("text-transform checks=%u failures=0\n", checks);
    return 0;
}
