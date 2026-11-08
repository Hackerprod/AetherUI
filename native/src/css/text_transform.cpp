#include "text_transform.h"

#include "../primitives/presentation_text.h"

std::string aether_apply_text_transform(const std::string& value, int32_t text_transform) {
    return aether_apply_text_transform(value, text_transform, true);
}

std::string aether_apply_text_transform(const std::string& value, int32_t text_transform, bool starts_word) {
    if (text_transform == AETHER_TEXT_TRANSFORM_NONE) return value;

    std::string result = value;
    bool word_start = starts_word;
    for (char& character : result) {
        const auto byte = static_cast<unsigned char>(character);
        if (text_transform == AETHER_TEXT_TRANSFORM_UPPERCASE) {
            character = static_cast<char>(aether::presentation_upper_byte(byte));
        } else if (text_transform == AETHER_TEXT_TRANSFORM_LOWERCASE) {
            character = static_cast<char>(aether::ascii_lower_byte(byte));
        } else if (text_transform == AETHER_TEXT_TRANSFORM_CAPITALIZE && word_start) {
            character = static_cast<char>(aether::presentation_upper_byte(byte));
        }
        word_start = aether::presentation_space_byte(byte) != 0;
    }
    return result;
}
