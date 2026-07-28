#pragma once

#include "../css/css_types.h"
#include "../dom/dom_types.h"

#include <string>

enum aether_form_control_content_model_e {
    AETHER_FORM_CONTROL_ORDINARY_CONTENT,
    AETHER_FORM_CONTROL_OPAQUE_SINGLE_LINE,
    AETHER_FORM_CONTROL_FLOW_CONTENT,
    // Textarea needs value, rows/cols, preserved line breaks, and scrolling.
    // Do not accidentally apply input or button semantics to it.
    AETHER_FORM_CONTROL_MULTILINE_EDITABLE,
};

aether_form_control_content_model_e aether_form_control_content_model(const aether_node_s* element);
bool aether_form_control_is_opaque_single_line(const aether_node_s* element);
std::string aether_opaque_control_display_text(
    const aether_node_s* element,
    const aether_computed_style_t& style);
