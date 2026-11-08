#include "form_controls.h"

#include "../css/text_transform.h"
#include "../primitives/ascii_text.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace {
// Consume one strict UTF-8 scalar, otherwise exactly one byte. All reads are
// bounded by the remaining input; invalid bytes never enter the output.
std::size_t password_unit_bytes(const unsigned char* bytes, std::size_t remaining) noexcept {
    const auto continuation = [](unsigned char byte) { return byte >= 0x80 && byte <= 0xBF; };
    const auto first = bytes[0]; // Caller always supplies at least one byte.
    if (first <= 0x7F) return 1;
    if (first >= 0xC2 && first <= 0xDF) {
        if (remaining >= 2 && continuation(bytes[1])) return 2;
    } else if (first >= 0xE0 && first <= 0xEF) {
        if (remaining >= 3 && continuation(bytes[1]) && continuation(bytes[2]) &&
            (first != 0xE0 || bytes[1] >= 0xA0) &&
            (first != 0xED || bytes[1] <= 0x9F)) return 3;
    } else if (first >= 0xF0 && first <= 0xF4) {
        if (remaining >= 4 && continuation(bytes[1]) && continuation(bytes[2]) &&
            continuation(bytes[3]) && (first != 0xF0 || bytes[1] >= 0x90) &&
            (first != 0xF4 || bytes[1] <= 0x8F)) return 4;
    }
    return 1;
}

std::string password_display_text(const std::string& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t units = 0;
    for (std::size_t offset = 0; offset < value.size(); ++units)
        offset += password_unit_bytes(bytes + offset, value.size() - offset);

    std::string result;
    const auto byte_count = aether_password_mask_byte_count(units, result.max_size());
    try {
        result.reserve(byte_count);
        for (std::size_t unit = 0; unit < units; ++unit)
            result.append("\xE2\x80\xA2", 3);
    } catch (const std::length_error&) {
        // ABI boundaries already translate bad_alloc to OUT_OF_MEMORY. Never
        // publish a partial mask or fall back to the original value on failure.
        throw std::bad_alloc();
    }
    return result;
}

struct form_control_descriptor_s {
    const char* tag_name;
    aether_form_control_content_model_e content_model;
};

constexpr form_control_descriptor_s form_control_descriptors[] = {
    {"input", AETHER_FORM_CONTROL_OPAQUE_SINGLE_LINE},
    {"button", AETHER_FORM_CONTROL_FLOW_CONTENT},
    {"textarea", AETHER_FORM_CONTROL_MULTILINE_EDITABLE},
};
}

aether_form_control_content_model_e aether_form_control_content_model(const aether_node_s* element) {
    if (!element) return AETHER_FORM_CONTROL_ORDINARY_CONTENT;
    for (const auto& descriptor : form_control_descriptors) {
        if (element->tag_name == descriptor.tag_name) return descriptor.content_model;
    }
    return AETHER_FORM_CONTROL_ORDINARY_CONTENT;
}

bool aether_form_control_is_opaque_single_line(const aether_node_s* element) {
    return aether_form_control_content_model(element) == AETHER_FORM_CONTROL_OPAQUE_SINGLE_LINE;
}

std::string aether_opaque_control_display_text(
    const aether_node_s* element,
    const aether::style::StyleScalars& style) {
    if (!aether_form_control_is_opaque_single_line(element)) return {};
    const auto value = element->attributes.find("value");
    if (value != element->attributes.end() && !value->second.empty()) {
        const auto type = element->attributes.find("type");
        if (type != element->attributes.end() && aether::ascii_iequals(type->second, "password"))
            return password_display_text(value->second);
        return aether_apply_text_transform(value->second, style.text_transform);
    }
    const auto placeholder = element->attributes.find("placeholder");
    return placeholder != element->attributes.end()
        ? aether_apply_text_transform(placeholder->second, style.text_transform)
        : std::string();
}

float aether_control_min_content_height(float font_size, float line_height) {
    return std::max(font_size * line_height, font_size * 1.1f);
}

float aether_control_opaque_content_height(
    float measured_height,
    float font_size,
    float line_height,
    bool has_explicit_height,
    float explicit_height,
    int32_t box_sizing,
    float padding_v,
    float border_v)
{
    float natural_height;
    if (has_explicit_height) {
        natural_height = (box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? explicit_height
            : std::max(0.f, explicit_height - padding_v - border_v);
    } else {
        natural_height = std::max(measured_height, aether_control_min_content_height(font_size, line_height));
    }
    return std::max(natural_height, font_size * 1.1f);
}

float aether_control_flow_centering_offset(float definite_content_height, float used_content_height) {
    return (definite_content_height - used_content_height) * 0.5f;
}
float aether_control_opaque_content_height_used(
    float measured_height,
    float font_size,
    float line_height,
    bool has_explicit_height,
    float explicit_height,
    int32_t box_sizing,
    float padding_v,
    float border_v)
{
    float natural_height;
    if (has_explicit_height) {
        natural_height = (box_sizing == AETHER_BOX_SIZING_CONTENT_BOX)
            ? explicit_height
            : std::max(0.f, explicit_height - padding_v - border_v);
    } else {
        natural_height = std::max(measured_height, std::max(line_height, font_size * 1.1f));
    }
    return std::max(natural_height, font_size * 1.1f);
}

std::string aether_opaque_control_display_text(const aether_node_s* element, const aether_computed_style_t& style) {
    return aether_opaque_control_display_text(element, aether::style::StyleScalars::copy_from(style));
}
