#include "text_transform.h"

#include <cctype>

std::string aether_apply_text_transform(const std::string& value, int32_t text_transform) {
    if (text_transform == AETHER_TEXT_TRANSFORM_NONE) return value;

    std::string result = value;
    bool word_start = true;
    for (char& character : result) {
        const auto byte = static_cast<unsigned char>(character);
        if (text_transform == AETHER_TEXT_TRANSFORM_UPPERCASE) {
            character = static_cast<char>(std::toupper(byte));
        } else if (text_transform == AETHER_TEXT_TRANSFORM_LOWERCASE) {
            character = static_cast<char>(std::tolower(byte));
        } else if (text_transform == AETHER_TEXT_TRANSFORM_CAPITALIZE && word_start) {
            character = static_cast<char>(std::toupper(byte));
        }
        word_start = std::isspace(byte) != 0;
    }
    return result;
}
