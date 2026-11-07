#include "form_controls.h"

#include "../css/text_transform.h"

#include <algorithm>
#include <cstdint>

namespace {
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
