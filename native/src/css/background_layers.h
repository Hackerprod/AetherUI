#pragma once

#include <aether/aether.h>

#include <string>
#include <vector>

struct aether_background_position_s {
    aether_css_length x;
    aether_css_length y;
};

struct aether_background_size_s {
    int32_t kind = AETHER_BACKGROUND_SIZE_AUTO;
    aether_css_length x{};
    aether_css_length y{};
};

struct aether_background_repeat_s {
    int32_t x = AETHER_BACKGROUND_REPEAT;
    int32_t y = AETHER_BACKGROUND_REPEAT;
};

struct aether_background_shorthand_s {
    std::string image;
    std::string position;
    std::string size;
    std::string repeat;
    std::string color;
    bool has_color = false;
};

bool aether_background_image_list_try_parse(const std::string& source,
                                             aether_background_layer_list* out_layers);
bool aether_background_position_list_try_parse(const std::string& source,
                                               std::vector<aether_background_position_s>& out_positions);
bool aether_background_size_list_try_parse(const std::string& source,
                                           std::vector<aether_background_size_s>& out_sizes);
bool aether_background_repeat_list_try_parse(const std::string& source,
                                             std::vector<aether_background_repeat_s>& out_repeats);
bool aether_background_shorthand_try_parse(const std::string& source,
                                           aether_background_shorthand_s& out);
