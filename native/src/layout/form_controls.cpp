#include "form_controls.h"

#include "../css/text_transform.h"

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
    const aether_computed_style_t& style) {
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
