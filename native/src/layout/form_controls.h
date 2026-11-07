#pragma once
#include "../css/style_read_view.h"

#include "../css/css_types.h"
#include "../dom/dom_types.h"

#include <algorithm>
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
std::string aether_opaque_control_display_text(
    const aether_node_s* element,
    const aether::style::StyleScalars& style);

/* All form-control sizing/displacement rules live here; layout_engine only consults them. */
float aether_control_min_content_height(float font_size, float line_height);
float aether_control_opaque_content_height(
    float measured_height,
    float font_size,
    float line_height,
    bool has_explicit_height,
    float explicit_height,
    int32_t box_sizing,
    float padding_v,
    float border_v);
float aether_control_flow_centering_offset(float definite_content_height, float used_content_height);
float aether_control_opaque_content_height_used(
    float measured_height,
    float font_size,
    float used_line_height,
    bool has_explicit_height,
    float explicit_height,
    int32_t box_sizing,
    float padding_v,
    float border_v);
