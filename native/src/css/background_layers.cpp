#include <new>
#include "../style_runtime/css_component_values.h"
#include "background_layers.h"

#include "linear_gradient.h"
#include "../style_runtime/style_runtime_types.h"

#include <cstdlib>
#include <cstring>

namespace {

std::vector<std::string> split_trimmed(const std::string& source, char separator) {
    std::vector<std::string> result;
    for (const auto& part : css_split_component_delimited(source, separator)) result.push_back(str_trim(part));
    return result;
}

std::vector<std::string> split_words(const std::string& source) { return css_split_components(source); }

bool parse_url(const std::string& source, std::string& out) {
    const auto text = str_trim(source);
    if (!str_starts_with_i(text, "url(") || text.size() < 6 || text.back() != ')') return false;
    auto body = str_trim(text.substr(4, text.size() - 5));
    if (body.empty()) return false;
    if (body.front() == '\'' || body.front() == '"') {
        const char quote = body.front();
        if (body.size() < 2 || body.back() != quote) return false;
        body = body.substr(1, body.size() - 2);
    } else if (body.find_first_of(" \t\r\n\"'") != std::string::npos) return false;
    if (body.empty()) return false;
    out = body;
    return true;
}

bool parse_image(const std::string& source, aether_background_layer& out) {
    const auto text = str_trim(source);
    aether_linear_gradient gradient{};
    if (aether_linear_gradient_try_parse(text.c_str(), &gradient)) {
        out.kind = AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT;
        out.gradient = gradient;
        return true;
    }
    std::string url;
    if (!parse_url(text, url)) return false;
    out.kind = AETHER_BACKGROUND_LAYER_IMAGE_URL;
    out.image_source = static_cast<char*>(std::malloc(url.size() + 1));
    if (!out.image_source) throw std::bad_alloc();
    std::memcpy(out.image_source, url.c_str(), url.size() + 1);
    return true;
}

struct background_list_guard_s {
    aether_background_layer_list value{};
    ~background_list_guard_s() { aether_background_layer_list_free(&value); }
    aether_background_layer_list release() noexcept { auto result = value; value = {}; return result; }
};

bool parse_length(const std::string& source, aether_css_length& out, bool allow_auto) {
    if (!aether_css_length_try_parse(source.c_str(), &out)) return false;
    return allow_auto || out.unit != AETHER_CSS_UNIT_AUTO;
}

bool parse_position(const std::string& source, aether_background_position_s& out) {
    auto words = split_words(source);
    if (words.empty() || words.size() > 2) return false;
    out.x = aether_css_length_zero();
    out.y = aether_css_length_zero();
    out.x.unit = AETHER_CSS_UNIT_PERCENT;
    out.y.unit = AETHER_CSS_UNIT_PERCENT;
    bool x_set = false, y_set = false;
    for (const auto& word : words) {
        const auto value = str_to_lower(word);
        if ((value == "left" || value == "right") && !x_set) {
            out.x.value = value == "right" ? 100.f : 0.f; x_set = true; continue;
        }
        if ((value == "top" || value == "bottom") && !y_set) {
            out.y.value = value == "bottom" ? 100.f : 0.f; y_set = true; continue;
        }
        if (value == "center") {
            if (!x_set) { out.x.value = 50.f; x_set = true; continue; }
            if (!y_set) { out.y.value = 50.f; y_set = true; continue; }
            return false;
        }
        aether_css_length length{};
        if (!parse_length(value, length, false)) return false;
        if (!x_set) { out.x = length; x_set = true; }
        else if (!y_set) { out.y = length; y_set = true; }
        else return false;
    }
    if (!x_set && y_set) out.x.value = 50.f;
    if (x_set && !y_set) out.y.value = 50.f;
    return true;
}

bool parse_size(const std::string& source, aether_background_size_s& out) {
    const auto text = str_to_lower(str_trim(source));
    if (text == "cover" || text == "contain") {
        out.kind = text == "cover" ? AETHER_BACKGROUND_SIZE_COVER : AETHER_BACKGROUND_SIZE_CONTAIN;
        out.x = aether_css_length_auto(); out.y = aether_css_length_auto();
        return true;
    }
    auto words = split_words(text);
    if (words.empty() || words.size() > 2) return false;
    out.kind = AETHER_BACKGROUND_SIZE_EXPLICIT;
    if (!parse_length(words[0], out.x, true)) return false;
    out.y = aether_css_length_auto();
    return words.size() == 1 || parse_length(words[1], out.y, true);
}

bool parse_repeat(const std::string& source, aether_background_repeat_s& out) {
    auto words = split_words(str_to_lower(str_trim(source)));
    if (words.empty() || words.size() > 2) return false;
    const auto decode = [](const std::string& value, int32_t& result) {
        if (value == "repeat") result = AETHER_BACKGROUND_REPEAT;
        else if (value == "no-repeat") result = AETHER_BACKGROUND_NO_REPEAT;
        else if (value == "space") result = AETHER_BACKGROUND_SPACE;
        else if (value == "round") result = AETHER_BACKGROUND_ROUND;
        else return false;
        return true;
    };
    if (words.size() == 1 && words[0] == "repeat-x") { out = {AETHER_BACKGROUND_REPEAT, AETHER_BACKGROUND_NO_REPEAT}; return true; }
    if (words.size() == 1 && words[0] == "repeat-y") { out = {AETHER_BACKGROUND_NO_REPEAT, AETHER_BACKGROUND_REPEAT}; return true; }
    if (!decode(words[0], out.x)) return false;
    out.y = out.x;
    return words.size() == 1 || decode(words[1], out.y);
}

std::string join(const std::vector<std::string>& values) {
    std::string result;
    for (size_t i = 0; i < values.size(); ++i) { if (i) result += ", "; result += values[i]; }
    return result;
}

bool is_repeat_word(const std::string& word) {
    const auto lower = str_to_lower(word);
    return lower == "repeat" || lower == "no-repeat" || lower == "repeat-x" || lower == "repeat-y" || lower == "space" || lower == "round";
}

} // namespace

bool aether_background_image_list_try_parse(const std::string& source, aether_background_layer_list* out_layers) {
    if (!out_layers) return false;
    background_list_guard_s guard;
    auto& replacement = guard.value;
    const auto entries = split_trimmed(source, ',');
    if (entries.empty()) return false;
    if (entries.size() == 1 && str_iequals(entries[0], "none")) { aether_background_layer_list_free(out_layers); return true; }
    replacement.items = static_cast<aether_background_layer*>(std::calloc(entries.size(), sizeof(aether_background_layer)));
    if (!replacement.items) throw std::bad_alloc();
    replacement.count = static_cast<int32_t>(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        if (str_iequals(entries[i], "none") || !parse_image(entries[i], replacement.items[i])) {
            return false;
        }
        auto& layer = replacement.items[i];
        layer.position_x = aether_css_length_zero(); layer.position_y = aether_css_length_zero();
        layer.size_kind = AETHER_BACKGROUND_SIZE_AUTO;
        layer.size_x = aether_css_length_auto(); layer.size_y = aether_css_length_auto();
        layer.repeat_x = AETHER_BACKGROUND_REPEAT; layer.repeat_y = AETHER_BACKGROUND_REPEAT;
    }
    aether_background_layer_list_free(out_layers);
    *out_layers = guard.release();
    return true;
}

bool aether_background_position_list_try_parse(const std::string& source, std::vector<aether_background_position_s>& out_positions) {
    std::vector<aether_background_position_s> result;
    for (const auto& entry : split_trimmed(source, ',')) { aether_background_position_s value{}; if (!parse_position(entry, value)) return false; result.push_back(value); }
    if (result.empty()) return false; out_positions = std::move(result); return true;
}

bool aether_background_size_list_try_parse(const std::string& source, std::vector<aether_background_size_s>& out_sizes) {
    std::vector<aether_background_size_s> result;
    for (const auto& entry : split_trimmed(source, ',')) { aether_background_size_s value{}; if (!parse_size(entry, value)) return false; result.push_back(value); }
    if (result.empty()) return false; out_sizes = std::move(result); return true;
}

bool aether_background_repeat_list_try_parse(const std::string& source, std::vector<aether_background_repeat_s>& out_repeats) {
    std::vector<aether_background_repeat_s> result;
    for (const auto& entry : split_trimmed(source, ',')) { aether_background_repeat_s value{}; if (!parse_repeat(entry, value)) return false; result.push_back(value); }
    if (result.empty()) return false; out_repeats = std::move(result); return true;
}

bool aether_background_shorthand_try_parse(const std::string& source, aether_background_shorthand_s& out) {
    out = {};
    std::vector<std::string> images, positions, sizes, repeats;
    const auto layers = split_trimmed(source, ',');
    if (layers.empty()) return false;
    for (size_t index = 0; index < layers.size(); ++index) {
        const auto slash = css_find_component_delimiter(layers[index], '/');
        const auto before_size = str_trim(slash < 0 ? layers[index] : layers[index].substr(0, slash));
        const auto after_slash = slash < 0 ? std::string{} : str_trim(layers[index].substr(slash + 1));
        if (slash >= 0 && after_slash.empty()) return false;
        std::string image = "none", position, repeat;
        std::vector<std::string> position_words;
        for (const auto& word : split_words(before_size)) {
            if (str_starts_with_i(word, "url(") || str_starts_with_i(word, "linear-gradient")) {
                if (image != "none") return false;
                image = word;
            } else if (is_repeat_word(word)) {
                if (!repeat.empty()) repeat += " ";
                repeat += word;
            } else {
                aether_color_rgba color{};
                if (aether_color_rgba_try_parse(word.c_str(), &color)) {
                    if (index + 1 != layers.size() || out.has_color) return false;
                    out.color = word; out.has_color = true;
                } else position_words.push_back(word);
            }
        }
        std::string size_text;
        if (!after_slash.empty()) {
            const auto words = split_words(after_slash);
            if (words.empty()) return false;
            size_t consumed = 0;
            const auto first = str_to_lower(words[0]);
            if (first == "cover" || first == "contain") {
                size_text = words[0];
                consumed = 1;
            } else {
                aether_css_length first_length{};
                if (!parse_length(first, first_length, true)) return false;
                size_text = words[0];
                consumed = 1;
                if (words.size() > 1 && !is_repeat_word(words[1])) {
                    aether_css_length second_length{};
                    if (!parse_length(words[1], second_length, true)) return false;
                    size_text += " " + words[1];
                    consumed = 2;
                }
            }
            for (; consumed < words.size(); ++consumed) {
                if (is_repeat_word(words[consumed]) && repeat.empty()) {
                    repeat = words[consumed];
                    continue;
                }
                aether_color_rgba color{};
                if (index + 1 == layers.size() && !out.has_color &&
                    aether_color_rgba_try_parse(words[consumed].c_str(), &color)) {
                    out.color = words[consumed];
                    out.has_color = true;
                    continue;
                }
                return false;
            }
        }
        for (size_t word_index = 0; word_index < position_words.size(); ++word_index) {
            if (word_index) position += " ";
            position += position_words[word_index];
        }
        aether_background_position_s checked_position{};
        aether_background_size_s checked_size{};
        aether_background_repeat_s checked_repeat{};
        if (!parse_position(position.empty() ? "0% 0%" : position, checked_position) ||
            !parse_size(size_text.empty() ? "auto auto" : size_text, checked_size) ||
            !parse_repeat(repeat.empty() ? "repeat repeat" : repeat, checked_repeat)) return false;
        aether_background_layer_list parsed{};
        if (!aether_background_image_list_try_parse(image, &parsed)) return false;
        aether_background_layer_list_free(&parsed);
        images.push_back(image); positions.push_back(position.empty() ? "0% 0%" : position);
        sizes.push_back(size_text.empty() ? "auto auto" : size_text); repeats.push_back(repeat.empty() ? "repeat repeat" : repeat);
    }
    out.image = join(images); out.position = join(positions); out.size = join(sizes); out.repeat = join(repeats);
    return true;
}
