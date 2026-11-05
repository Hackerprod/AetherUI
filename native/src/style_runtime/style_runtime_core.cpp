#include <aether/aether.h>
#include "../abi/aether_abi.h"
#include "../css/border_utils.h"
#include "../css/computed_style_internal.h"
#include "../css/style_map_types.h"
#include "../dom/dom_types.h"
#include "style_property_registry_internal.h"
#include "style_runtime_types.h"
#include "css_variable_tokens.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct aether_computed_style_handle_s {
    aether_computed_style_t style;
};

struct runtime_node_state_s {
    aether_computed_style_handle_s style{};
    custom_property_scope_s custom_properties{};
    color_resolution_state_s color_references{};
    uint32_t declared_impact = STYLE_IMPACT_NONE;
    uint32_t declared_inherited_impact = STYLE_IMPACT_NONE;
    uint32_t inherited_change_impact = STYLE_IMPACT_NONE;
    ~runtime_node_state_s() { aether_computed_style_free(&style.style); }
};

struct aether_style_update_s {
    aether_node_s* node = nullptr;
    // An update crosses the runtime/binding boundary after the style tree may
    // have been rebuilt.  It therefore owns a deep snapshot rather than
    // borrowing storage from runtime_node_state_s.
    aether_computed_style_handle_s current{};
    uint32_t impact = STYLE_IMPACT_NONE;
    ~aether_style_update_s() { aether_computed_style_free(&current.style); }
};

struct aether_style_runtime_s {
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{};
    aether_style_diagnostic_fn diag_cb = nullptr;
    void* diag_user_data = nullptr;
    int64_t next_sheet_handle = 1;
    int32_t next_rule_id = 1;
    std::unordered_map<int64_t, std::unique_ptr<compiled_style_sheet_s>> sheets;
    std::unordered_map<aether_node_s*, std::unique_ptr<runtime_node_state_s>> states;
    rule_index_s index;
};

static aether_style_update_s* make_style_update(aether_node_s* node,
                                                 aether_computed_style_handle_s& current,
                                                 uint32_t impact) {
    auto* update = new (std::nothrow) aether_style_update_s();
    if (!update) return nullptr;
    update->node = node;
    update->impact = impact;
    if (aether_computed_style_copy(&current, &update->current.style) != AETHER_OK) {
        delete update;
        return nullptr;
    }
    return update;
}

static constexpr uint32_t STYLE_IMPACT_ALL_BITS =
    static_cast<uint32_t>(STYLE_IMPACT_LAYOUT) |
    static_cast<uint32_t>(STYLE_IMPACT_PAINT) |
    static_cast<uint32_t>(STYLE_IMPACT_HITTEST) |
    static_cast<uint32_t>(STYLE_IMPACT_COMPOSITE) |
    static_cast<uint32_t>(STYLE_IMPACT_TEXT_LAYOUT) |
    static_cast<uint32_t>(STYLE_IMPACT_CURSOR) |
    static_cast<uint32_t>(STYLE_IMPACT_INHERITED);

static bool is_calc_expression(const char* value) {
    if (!value) return false;
    while (*value && std::isspace(static_cast<unsigned char>(*value))) ++value;
    static constexpr char prefix[] = "calc(";
    for (size_t index = 0; index < sizeof(prefix) - 1; ++index) {
        const char current = value[index];
        if (!current) return false;
        const char lowered = current >= 'A' && current <= 'Z'
            ? static_cast<char>(current - 'A' + 'a') : current;
        if (lowered != prefix[index]) return false;
    }
    return true;
}

static node_style_info_s get_node_info(aether_node_s* node) {
    node_style_info_s info;
    info.type_name = node->tag_name;
    if (auto it = node->attributes.find("id"); it != node->attributes.end())
        info.id = it->second;
    if (auto it = node->attributes.find("class"); it != node->attributes.end()) {
        std::string item;
        for (char c : it->second) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!item.empty()) { info.classes.push_back(item); item.clear(); }
            } else item += c;
        }
        if (!item.empty()) info.classes.push_back(item);
    }
    return info;
}

static bool color_equal(const aether_color_rgba& a, const aether_color_rgba& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static bool length_equal(const aether_css_length& a, const aether_css_length& b) {
    return a.value == b.value &&
           a.unit == b.unit &&
           a.linear_percent == b.linear_percent &&
           a.linear_em == b.linear_em &&
           a.linear_rem == b.linear_rem &&
           a.linear_vw == b.linear_vw &&
           a.linear_vh == b.linear_vh;
}

static bool edges_equal(const aether_css_edges& a, const aether_css_edges& b) {
    return length_equal(a.left, b.left) &&
           length_equal(a.top, b.top) &&
           length_equal(a.right, b.right) &&
           length_equal(a.bottom, b.bottom);
}

static bool color_edges_equal(const aether_color_edges& a, const aether_color_edges& b) {
    return color_equal(a.left, b.left) &&
           color_equal(a.top, b.top) &&
           color_equal(a.right, b.right) &&
           color_equal(a.bottom, b.bottom);
}

static bool border_style_edges_equal(const aether_border_style_edges& a, const aether_border_style_edges& b) {
    return a.left == b.left &&
           a.top == b.top &&
           a.right == b.right &&
           a.bottom == b.bottom;
}

static bool flex_basis_equal(const aether_flex_basis& a, const aether_flex_basis& b) {
    return a.kind == b.kind && length_equal(a.length, b.length);
}

static bool corner_radius_equal(const aether_corner_radius& a, const aether_corner_radius& b) {
    return length_equal(a.horizontal, b.horizontal) &&
           length_equal(a.vertical, b.vertical);
}

static bool border_radii_equal(const aether_border_radii& a, const aether_border_radii& b) {
    return corner_radius_equal(a.top_left, b.top_left) &&
           corner_radius_equal(a.top_right, b.top_right) &&
           corner_radius_equal(a.bottom_right, b.bottom_right) &&
           corner_radius_equal(a.bottom_left, b.bottom_left);
}

static bool transform_origin_equal(const aether_transform_origin& a, const aether_transform_origin& b) {
    return length_equal(a.x, b.x) && length_equal(a.y, b.y);
}

static bool gradient_stop_equal(const aether_gradient_stop& a, const aether_gradient_stop& b) {
    return color_equal(a.color, b.color) &&
           length_equal(a.position, b.position) &&
           a.offset == b.offset &&
           a.has_position == b.has_position;
}

static bool gradient_equal(const aether_linear_gradient& a, const aether_linear_gradient& b) {
    if (a.direction_x != b.direction_x ||
        a.direction_y != b.direction_y ||
        a.stop_count != b.stop_count)
        return false;
    if (a.stop_count == 0) return true;
    if (!a.stops || !b.stops) return false;
    for (int32_t index = 0; index < a.stop_count; ++index)
        if (!gradient_stop_equal(a.stops[index], b.stops[index])) return false;
    return true;
}

static bool style_equal(const aether_computed_style_t& a, const aether_computed_style_t& b) {
    return a.display == b.display &&
           a.flex_direction == b.flex_direction &&
           a.justify_content == b.justify_content &&
           a.align_items == b.align_items &&
           a.align_self == b.align_self &&
           a.flex_grow == b.flex_grow &&
           a.flex_shrink == b.flex_shrink &&
           flex_basis_equal(a.flex_basis, b.flex_basis) &&
           a.flex_wrap == b.flex_wrap &&
           a.gap == b.gap &&
           a.position == b.position &&
           edges_equal(a.inset, b.inset) &&
           length_equal(a.width, b.width) &&
           length_equal(a.height, b.height) &&
           length_equal(a.min_width, b.min_width) &&
           length_equal(a.min_height, b.min_height) &&
           length_equal(a.max_width, b.max_width) &&
           length_equal(a.max_height, b.max_height) &&
           edges_equal(a.margin, b.margin) &&
           edges_equal(a.padding, b.padding) &&
           edges_equal(a.border_width, b.border_width) &&
           a.box_sizing == b.box_sizing &&
           color_equal(a.color, b.color) &&
           color_equal(a.background_color, b.background_color) &&
           aether_background_layer_list_equal(&a.background_layers, &b.background_layers) == AETHER_TRUE &&
           aether_backdrop_filter_list_equal(&a.backdrop_filters, &b.backdrop_filters) == AETHER_TRUE &&
           color_equal(a.border_color, b.border_color) &&
           color_edges_equal(a.border_colors, b.border_colors) &&
           border_style_edges_equal(a.border_styles, b.border_styles) &&
           a.has_text_gradient == b.has_text_gradient &&
           gradient_equal(a.text_gradient, b.text_gradient) &&
           aether_transform_list_equal(&a.transforms, &b.transforms) == AETHER_TRUE &&
           transform_origin_equal(a.transform_origin, b.transform_origin) &&
           aether_shadow_list_equal(&a.text_shadows, &b.text_shadows) == AETHER_TRUE &&
           aether_shadow_list_equal(&a.box_shadows, &b.box_shadows) == AETHER_TRUE &&
           border_radii_equal(a.border_radii, b.border_radii) &&
           a.opacity == b.opacity &&
           std::strncmp(a.font_family, b.font_family, sizeof(a.font_family)) == 0 &&
           a.font_size == b.font_size &&
           a.font_weight == b.font_weight &&
           a.line_height == b.line_height &&
           a.text_align == b.text_align &&
           a.text_transform == b.text_transform &&
           a.white_space == b.white_space &&
           a.overflow_x == b.overflow_x &&
           a.overflow_y == b.overflow_y &&
           a.cursor == b.cursor &&
           a.pointer_events == b.pointer_events &&
            a.visible == b.visible &&
            a.z_index == b.z_index &&
            a.z_index_kind == b.z_index_kind &&
            a.transition_properties == b.transition_properties &&
            a.transition_duration == b.transition_duration &&
            a.transition_delay == b.transition_delay &&
            a.transition_timing == b.transition_timing;
}

static void rebuild_index(aether_style_runtime_s* runtime) {
    std::vector<const compiled_style_sheet_s*> sheets;
    sheets.reserve(runtime->sheets.size());
    for (const auto& pair : runtime->sheets) sheets.push_back(pair.second.get());
    std::sort(sheets.begin(), sheets.end(), [](const auto* a, const auto* b) {
        return a->handle < b->handle;
    });
    rule_index_rebuild(&runtime->index, sheets);
}

static void report(aether_style_runtime_s* runtime, const char* code,
                   const char* property, const char* value, const char* message) {
    if (runtime->diag_cb)
        runtime->diag_cb(code, property, value, message, runtime->diag_user_data);
}

static bool is_color_producer(const style_property_definition_s& definition) {
    // The current model maps text-fill-color onto the same concrete text color.
    // Resolve every such producer before references to currentColor are fixed.
    return definition.name == "color" ||
           definition.name == "-webkit-text-fill-color" ||
           definition.name == "text-fill-color";
}

static void resolve_symbolic_colors(aether_computed_style_t& style,
                                    const color_resolution_state_s& references) {
    if (references.border_uses_current_color[static_cast<int>(aether_border_side_t::top)])
        style.border_colors.top = style.color;
    if (references.border_uses_current_color[static_cast<int>(aether_border_side_t::right)])
        style.border_colors.right = style.color;
    if (references.border_uses_current_color[static_cast<int>(aether_border_side_t::bottom)])
        style.border_colors.bottom = style.color;
    if (references.border_uses_current_color[static_cast<int>(aether_border_side_t::left)])
        style.border_colors.left = style.color;

    for (int32_t index = 0; index < style.box_shadows.count; ++index) {
        if (index < static_cast<int32_t>(references.box_shadow_uses_current_color.size()) &&
            references.box_shadow_uses_current_color[static_cast<size_t>(index)]) {
            style.box_shadows.items[index].color = style.color;
        }
    }
    for (int32_t index = 0; index < style.text_shadows.count; ++index) {
        if (index < static_cast<int32_t>(references.text_shadow_uses_current_color.size()) &&
            references.text_shadow_uses_current_color[static_cast<size_t>(index)]) {
            style.text_shadows.items[index].color = style.color;
        }
    }

    // Legacy bindings expose this representative in addition to the per-edge
    // colors. Define it deterministically rather than leaking apply order.
    style.border_color = style.border_colors.left;
}

static bool overflow_is_visible(int32_t value) {
    return value == AETHER_OVERFLOW_VISIBLE;
}

static bool overflow_is_clip(int32_t value) {
    return value == AETHER_OVERFLOW_CLIP;
}

static void normalize_overflow_axes(aether_computed_style_t& style) {
    // CSS Overflow 3.1 computed-value rule. An axis cannot be visible (or clip)
    // while the opposite axis clips or scrolls. Visible -> auto when the other
    // axis is neither visible nor clip; clip -> hidden when the other axis is
    // neither visible nor clip. Normalizing after the cascade keeps all layout
    // and paint consumers on the same computed values.
    if (overflow_is_visible(style.overflow_x) && !overflow_is_visible(style.overflow_y) &&
        !overflow_is_clip(style.overflow_y))
        style.overflow_x = AETHER_OVERFLOW_AUTO;
    if (overflow_is_visible(style.overflow_y) && !overflow_is_visible(style.overflow_x) &&
        !overflow_is_clip(style.overflow_x))
        style.overflow_y = AETHER_OVERFLOW_AUTO;
    if (overflow_is_clip(style.overflow_x) && !overflow_is_visible(style.overflow_y) &&
        !overflow_is_clip(style.overflow_y))
        style.overflow_x = AETHER_OVERFLOW_HIDDEN;
    if (overflow_is_clip(style.overflow_y) && !overflow_is_visible(style.overflow_x) &&
        !overflow_is_clip(style.overflow_x))
        style.overflow_y = AETHER_OVERFLOW_HIDDEN;
}

static bool try_parse_generated_content(const std::string& source, std::string& output) {
    std::string value = str_trim(source);
    if (value.empty() || str_iequals(value, "none") || str_iequals(value, "normal")) return false;

    output.clear();
    size_t index = 0;
    bool found_string = false;
    while (index < value.size()) {
        while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index]))) ++index;
        if (index >= value.size()) break;
        const char quote = value[index];
        if (quote != '\'' && quote != '"') return false;
        ++index;
        found_string = true;
        while (index < value.size() && value[index] != quote) {
            if (value[index] == '\\' && index + 1 < value.size()) ++index;
            output += value[index++];
        }
        if (index >= value.size()) return false;
        ++index;
    }
    return found_string;
}

static runtime_node_state_s* compute_node(aether_style_runtime_s* runtime, aether_node_s* node,
                                          runtime_node_state_s* parent, uint32_t inherited_parent_impact,
                                          uint32_t& impact, uint32_t& inherited_change_impact,
                                          std::optional<std::string>* generated_content = nullptr) {
    auto* registry = style_property_registry_get_global();
    auto next = aether_computed_style_inherit_from(parent ? &parent->style.style : nullptr);
    std::vector<cascade_entry_s> cascade(static_cast<size_t>(registry->property_count()));
    std::unordered_map<std::string, cascade_entry_custom_s> custom_winners;
    cascade_entry_custom_s content_winner;

    std::vector<const compiled_rule_s*> candidates;
    auto info = get_node_info(node);
    rule_index_get_candidates(&runtime->index, &info, candidates);
    for (const auto* rule : candidates) {
        if (rule->selector.pseudo_element != node->pseudo_element) continue;
        if (!media_condition_matches(rule->condition, &runtime->environment) ||
            !selector_match(rule->selector, node)) continue;
        for (const auto& declaration : rule->declarations) {
            cascade_priority_t priority{
                declaration.important, false, rule->layer, rule->selector.specificity,
                rule->source_order, declaration.declaration_order
            };
            if (declaration.property_id >= 0 &&
                declaration.property_id < registry->property_count())
                cascade[static_cast<size_t>(declaration.property_id)].offer(priority, declaration.value);
        }
        for (const auto& declaration : rule->custom_declarations) {
            cascade_priority_t priority{
                declaration.important, false, rule->layer, rule->selector.specificity,
                rule->source_order, declaration.declaration_order
            };
            custom_winners[declaration.name].offer(priority, declaration.source);
        }
        for (const auto& declaration : rule->content_declarations) {
            cascade_priority_t priority{
                declaration.important, false, rule->layer, rule->selector.specificity,
                rule->source_order, declaration.declaration_order
            };
            content_winner.offer(priority, declaration.source);
        }
    }

    std::vector<compiled_property_declaration_s> inline_properties;
    std::vector<custom_property_declaration_s> inline_custom;
    // Generated pseudo-elements retain host attributes for selector matching, but
    // the host's inline declaration block does not cascade onto ::before/::after.
    if (!node->is_generated) if (auto it = node->attributes.find("style"); it != node->attributes.end()) {
        style_source_compile_inline(it->second.c_str(), inline_properties, inline_custom,
                                    runtime->diag_cb, runtime->diag_user_data);
        for (const auto& declaration : inline_properties) {
            cascade_priority_t priority{
                declaration.important, true, style_sheet_layer::author,
                specificity_s{1, 0, 0}, INT32_MAX, declaration.declaration_order
            };
            if (declaration.property_id >= 0 &&
                declaration.property_id < registry->property_count())
                cascade[static_cast<size_t>(declaration.property_id)].offer(priority, declaration.value);
        }
        for (const auto& declaration : inline_custom) {
            cascade_priority_t priority{
                declaration.important, true, style_sheet_layer::author,
                specificity_s{1, 0, 0}, INT32_MAX, declaration.declaration_order
            };
            custom_winners[declaration.name].offer(priority, declaration.source);
        }
    }

    custom_property_scope_s scope;
    scope.parent = parent ? &parent->custom_properties : nullptr;
    for (const auto& pair : custom_winners) {
        auto value = str_trim(pair.second.source);
        if (str_iequals(value, "inherit") || str_iequals(value, "unset")) continue;
        scope.local[pair.first] = str_iequals(value, "initial") || str_iequals(value, "revert")
            ? custom_property_invalid_value() : pair.second.source;
    }

    // Deferred shorthands: their values reference custom properties, so they were kept
    // raw at compile time. Resolve each against this node's variable scope and expand it
    // into the cascade with the rule's own priority, exactly as if the expanded longhands
    // had been authored inline (e.g. `background: var(--bg-gradient)` on a body).
    for (const auto* rule : candidates) {
        if (rule->selector.pseudo_element != node->pseudo_element) continue;
        if (!media_condition_matches(rule->condition, &runtime->environment) ||
            !selector_match(rule->selector, node)) continue;
        if (rule->shorthand_declarations.empty()) continue;
        for (const auto& declaration : rule->shorthand_declarations) {
            std::string resolved;
            if (!variable_resolve(declaration.source.c_str(), &scope,
                                  runtime->options.max_variable_depth,
                                  runtime->options.max_variable_expansion_length, resolved)) {
                report(runtime, "AETHER-CSS-VAR", declaration.name.c_str(),
                       declaration.source.c_str(), "Unable to resolve CSS variable.");
                continue;
            }
            const auto* shorthand = registry->try_get_shorthand(declaration.name.c_str());
            if (!shorthand) continue;
            style_shorthand_writer_s writer;
            if (!shorthand->expand(resolved.c_str(), writer)) continue;
            for (const auto& item : writer.items) {
                const auto* def = registry->try_get(item.name.c_str());
                if (!def || def->id < 0 || def->id >= registry->property_count()) continue;
                std::string parsed;
                if (!def->parse(item.value.c_str(), parsed)) continue;
                cascade_priority_t priority{
                    declaration.important, false, rule->layer,
                    rule->selector.specificity, rule->source_order, declaration.declaration_order
                };
                compiled_style_value_s value{compiled_value_mode::value, parsed, {}};
                cascade[static_cast<size_t>(def->id)].offer(priority, value);
            }
        }
    }

    if (generated_content) {
        generated_content->reset();
        if (content_winner.is_set) {
            std::string content_source = content_winner.source;
            if (variable_has_reference(content_source) &&
                !variable_resolve(content_source.c_str(), &scope,
                                  runtime->options.max_variable_depth,
                                  runtime->options.max_variable_expansion_length, content_source)) {
                report(runtime, "AETHER-CSS-VAR", "content", content_winner.source.c_str(),
                       "Unable to resolve CSS variable.");
            } else {
                std::string parsed_content;
                if (try_parse_generated_content(content_source, parsed_content)) {
                    *generated_content = std::move(parsed_content);
                }
            }
        }
    }

    auto found = runtime->states.find(node);
    const aether_computed_style_t* previous_style =
        found == runtime->states.end() ? nullptr : &found->second->style.style;

    auto apply_property_value = [&](const style_property_definition_s& definition,
                                    aether_computed_style_t& style,
                                    color_resolution_state_s& references,
                                    const std::string& value,
                                    bool emit_diagnostics) {
        property_apply_context_s context{&style, &references};
        property_apply_result_s apply_result{};
        if (definition.apply_with_context_checked) {
            apply_result = definition.apply_with_context_checked(context, value);
        } else if (definition.apply_with_context) {
            definition.apply_with_context(context, value);
        } else if (definition.apply_checked) {
            apply_result = definition.apply_checked(&style, value);
        } else if (definition.apply) {
            definition.apply(&style, value);
        }
        if (!apply_result.success || apply_result.diagnostic_code) {
            if (emit_diagnostics) {
                report(runtime,
                       apply_result.diagnostic_code ? apply_result.diagnostic_code : "AETHER-CSS-APPLY",
                       definition.name.c_str(), value.c_str(),
                       apply_result.diagnostic_message ? apply_result.diagnostic_message : "CSS value could not be applied.");
            }
            if (!apply_result.success) return false;
        }
        return true;
    };

    color_resolution_state_s color_references;
    if (parent) {
        // text-shadow is inherited, and currentColor inside an inherited
        // shadow must remain symbolic until this node's own color is known.
        // Borders and box-shadow are not inherited, so their symbolic state
        // starts from the CSS initial values for this node.
        color_references.text_shadow_uses_current_color =
            parent->color_references.text_shadow_uses_current_color;
    }
    uint32_t declared_impact = STYLE_IMPACT_NONE;
    uint32_t declared_inherited_impact = STYLE_IMPACT_NONE;
    auto apply_definition = [&](size_t i) {
        if (!cascade[i].is_set) return false;
        const auto& definition = registry->definitions[i];
        const auto& compiled = cascade[i].value;
        std::string value;
        switch (compiled.mode) {
        case compiled_value_mode::value:
            value = compiled.typed_value;
            break;
        case compiled_value_mode::initial:
            value = definition.initial_value;
            break;
        case compiled_value_mode::inherit:
            // The style starts with inherited values. Non-inherited explicit
            // inherit is uncommon in Aether markup and remains unchanged here.
            return false;
        case compiled_value_mode::unset:
            if (definition.inherited && parent) return false;
            value = definition.initial_value;
            break;
        case compiled_value_mode::deferred:
            if (!variable_resolve(compiled.deferred_source.c_str(), &scope,
                                  runtime->options.max_variable_depth,
                                  runtime->options.max_variable_expansion_length, value)) {
                report(runtime, "AETHER-CSS-VAR", definition.name.c_str(),
                       compiled.deferred_source.c_str(), "Unable to resolve CSS variable.");
                return false;
            }
            if (!definition.parse(value.c_str(), value)) {
                report(runtime,
                       is_calc_expression(value.c_str()) ? "AETHER-CSS-CALC" : "AETHER-CSS-VALUE",
                       definition.name.c_str(), value.c_str(),
                       is_calc_expression(value.c_str())
                           ? "The calc() expression is syntactically or dimensionally invalid."
                           : "Resolved CSS value is invalid.");
                return false;
            }
            break;
        }
        if (!apply_property_value(definition, next, color_references, value, true))
            return false;
        bool property_changed = previous_style == nullptr;
        if (previous_style) {
            aether_computed_style_handle_s previous_handle{};
            previous_handle.style = *previous_style;
            aether_computed_style_t probe{};
            if (aether_computed_style_copy(&previous_handle, &probe) == AETHER_OK) {
                color_resolution_state_s probe_references = found->second->color_references;
                if (apply_property_value(definition, probe, probe_references, value, false)) {
                    resolve_symbolic_colors(probe, probe_references);
                    normalize_overflow_axes(probe);
                    property_changed = !style_equal(*previous_style, probe);
                } else {
                    property_changed = true;
                }
                aether_computed_style_free(&probe);
            } else {
                property_changed = true;
            }
        }
        if (property_changed) {
            declared_impact |= definition.impact;
            if ((definition.impact & STYLE_IMPACT_INHERITED) != 0)
                declared_inherited_impact |= definition.impact;
        }
        return true;
    };

    // The cascade has already selected winners. Resolve the node's final
    // concrete text color first, then apply properties that may carry symbolic
    // currentColor references. This makes declaration/registry order immaterial.
    for (size_t i = 0; i < cascade.size(); ++i) {
        if (cascade[i].is_set && is_color_producer(registry->definitions[i]))
            apply_definition(i);
    }
    for (size_t i = 0; i < cascade.size(); ++i) {
        if (cascade[i].is_set && !is_color_producer(registry->definitions[i]))
            apply_definition(i);
    }
    resolve_symbolic_colors(next, color_references);
    normalize_overflow_axes(next);

    // <br> has an HTML display semantic, not merely a managed bridge convention.
    // Preserve an author-specified display value, but make an otherwise unstyled
    // line break participate in native inline layout as a line-break item.
    if (node->tag_name == "br") {
        const auto* display = registry->try_get("display");
        if (display && !cascade[static_cast<size_t>(display->id)].is_set) {
            next.display = AETHER_DISPLAY_LINE_BREAK;
            declared_impact |= display->impact;
        }
    }

    const bool is_new_node = found == runtime->states.end();
    const bool changed = is_new_node || !style_equal(found->second->style.style, next);
    if (!changed) {
        impact = STYLE_IMPACT_NONE;
        inherited_change_impact = STYLE_IMPACT_NONE;
        if (!is_new_node) {
            // Retain the state, its style buffers and its scope address. A
            // concrete no-op can still change custom values or replace the
            // parent's state, so refresh both local keys and the parent link.
            // Descendants are visited even when this node emits no update.
            aether_computed_style_free(&next);
            found->second->custom_properties = std::move(scope);
            found->second->inherited_change_impact = inherited_change_impact;
            return found->second.get();
        }
    } else if (is_new_node) {
        impact = STYLE_IMPACT_ALL_BITS;
        inherited_change_impact = declared_inherited_impact | inherited_parent_impact;
    } else if (found->second->style.style.visible != next.visible) {
        // Compare the final computed delta, not the declarations still present.
        // Removing visibility can otherwise reuse historical geometric impacts;
        // removing width alongside visibility can miss those same impacts.
        // This shallow copy only compares borrowed buffers. Never free it or
        // store it as an owning style, and only change its local scalar.
        auto comparison = found->second->style.style;
        comparison.visible = next.visible;
        const auto* visibility = registry->try_get("visibility");
        impact = visibility && style_equal(comparison, next)
            ? visibility->impact
            : STYLE_IMPACT_ALL_BITS;
        inherited_change_impact = impact;
    } else {
        impact = declared_impact | inherited_parent_impact;
        if (impact == STYLE_IMPACT_NONE) {
            impact = found->second->declared_impact;
        }
        if (impact == STYLE_IMPACT_NONE) {
            // Safety net for computed-style changes produced outside a concrete
            // property applicator. This should be rare; known CSS properties
            // should flow through their registry impact instead of this path.
            impact = STYLE_IMPACT_ALL_BITS;
        }
        inherited_change_impact = declared_inherited_impact | inherited_parent_impact;
        if (inherited_change_impact == STYLE_IMPACT_NONE)
            inherited_change_impact = found->second->declared_inherited_impact;
    }
    if (!is_new_node) {
        const auto& previous = found->second->style.style;
        const bool before_context = previous.transforms.count > 0 || previous.backdrop_filters.count > 0;
        const bool after_context = next.transforms.count > 0 || next.backdrop_filters.count > 0;
        if (before_context != after_context) impact |= STYLE_IMPACT_LAYOUT;
    }
    auto state = std::make_unique<runtime_node_state_s>();
    state->style.style = next;
    state->custom_properties = std::move(scope);
    state->color_references = std::move(color_references);
    state->declared_impact = declared_impact;
    state->declared_inherited_impact = declared_inherited_impact;
    state->inherited_change_impact = inherited_change_impact;
    auto* result = state.get();
    runtime->states[node] = std::move(state);
    return result;
}

static void discard_generated_subtree(aether_style_runtime_s* runtime, aether_node_s* node) {
    for (auto* child : node->children) discard_generated_subtree(runtime, child);
    runtime->states.erase(node);
    delete node;
}

static void clear_generated_children(aether_style_runtime_s* runtime, aether_node_s* parent) {
    auto& children = parent->children;
    auto next = std::remove_if(children.begin(), children.end(), [&](aether_node_s* child) {
        if (!child->is_generated) return false;
        discard_generated_subtree(runtime, child);
        return true;
    });
    children.erase(next, children.end());
}

static void add_generated_pseudo(
    aether_style_runtime_s* runtime,
    aether_node_s* host,
    runtime_node_state_s* host_state,
    aether_pseudo_element_t pseudo,
    bool insert_before,
    std::vector<aether_style_update_s*>& updates)
{
    auto* generated = new aether_node_s();
    generated->type = AETHER_NODE_ELEMENT;
    generated->parent = host;
    generated->tag_name = host->tag_name;
    generated->attributes = host->attributes;
    generated->state = host->state;
    generated->is_generated = true;
    generated->pseudo_element = pseudo;
    generated->style_origin = host;

    uint32_t impact = STYLE_IMPACT_NONE;
    uint32_t inherited_change_impact = STYLE_IMPACT_NONE;
    std::optional<std::string> content;
    auto* state = compute_node(runtime, generated, host_state,
                               host_state ? host_state->inherited_change_impact : STYLE_IMPACT_NONE,
                               impact, inherited_change_impact, &content);
    if (!content) {
        runtime->states.erase(generated);
        delete generated;
        return;
    }

    if (!content->empty()) {
        auto* text = new aether_node_s();
        text->type = AETHER_NODE_TEXT;
        text->parent = generated;
        text->text = std::move(*content);
        generated->children.push_back(text);
    }
    if (insert_before) host->children.insert(host->children.begin(), generated);
    else host->children.push_back(generated);
    if (impact != STYLE_IMPACT_NONE) {
        if (auto* update = make_style_update(generated, state->style, impact))
            updates.push_back(update);
        else
            report(runtime, "AETHER-STYLE-UPDATE", "::after", "",
                   "Unable to snapshot the generated pseudo-element style update.");
    }
}

static void resolve_tree(aether_style_runtime_s* runtime, aether_node_s* node,
                         runtime_node_state_s* parent, uint32_t inherited_parent_impact,
                         std::vector<aether_style_update_s*>& updates) {
    if (!node) return;
    runtime_node_state_s* current_parent = parent;
    uint32_t current_inherited_impact = inherited_parent_impact;
    if (node->type == AETHER_NODE_ELEMENT) {
        if (node->is_generated) {
            // add_generated_pseudo already computed and published this node in
            // the current traversal.  Reusing that state avoids replacing it
            // while its update is being consumed below.
            auto existing = runtime->states.find(node);
            if (existing != runtime->states.end()) {
                current_parent = existing->second.get();
                current_inherited_impact = existing->second->inherited_change_impact;
            } else {
                uint32_t impact = STYLE_IMPACT_NONE;
                uint32_t inherited_change_impact = STYLE_IMPACT_NONE;
                auto* current = compute_node(runtime, node, parent, inherited_parent_impact,
                                             impact, inherited_change_impact);
                current_parent = current;
                current_inherited_impact = inherited_change_impact;
                if (impact != STYLE_IMPACT_NONE) {
                    if (auto* update = make_style_update(node, current->style, impact))
                        updates.push_back(update);
                    else
                        report(runtime, "AETHER-STYLE-UPDATE", "::generated", "",
                               "Unable to snapshot the generated pseudo-element style update.");
                }
            }
        } else {
            clear_generated_children(runtime, node);
            uint32_t impact = STYLE_IMPACT_NONE;
            uint32_t inherited_change_impact = STYLE_IMPACT_NONE;
            auto* current = compute_node(runtime, node, parent, inherited_parent_impact,
                                         impact, inherited_change_impact);
            current_parent = current;
            current_inherited_impact = inherited_change_impact;
            if (impact != STYLE_IMPACT_NONE) {
                if (auto* update = make_style_update(node, current->style, impact))
                    updates.push_back(update);
                else
                    report(runtime, "AETHER-STYLE-UPDATE", "element", "",
                           "Unable to snapshot the style update.");
            }
            add_generated_pseudo(runtime, node, current, aether_pseudo_element_t::before, true, updates);
            add_generated_pseudo(runtime, node, current, aether_pseudo_element_t::after, false, updates);
        }
    }
    for (auto* child : node->children)
        resolve_tree(runtime, child, current_parent, current_inherited_impact, updates);
}

static bool pseudo_contains_state(const pseudo_selector_s& pseudo) {
    if (pseudo.kind == PSEUDO_REQUIRED_STATE || pseudo.kind == PSEUDO_FORBIDDEN_STATE)
        return true;
    if (pseudo.nested) {
        for (const auto& selector : *pseudo.nested) {
            for (const auto& step : selector.steps) {
                for (const auto& nested_pseudo : step.compound.pseudos) {
                    if (pseudo_contains_state(nested_pseudo)) return true;
                }
            }
        }
    }
    return false;
}

static bool pseudo_is_direct_state(const pseudo_selector_s& pseudo) {
    return pseudo.kind == PSEUDO_REQUIRED_STATE || pseudo.kind == PSEUDO_FORBIDDEN_STATE;
}

static bool selector_state_dependency_is_subject_local(const compiled_selector_s& selector) {
    if (selector.steps.empty())
        return true;

    for (size_t step_index = 0; step_index < selector.steps.size(); ++step_index) {
        for (const auto& pseudo : selector.steps[step_index].compound.pseudos) {
            if (!pseudo_contains_state(pseudo)) continue;
            // Only direct state selectors on the subject compound can be
            // scoped to the elements whose state changed.  Ancestor/sibling
            // state dependencies and nested forms like :not(:hover) or
            // :has(:hover) can affect other subjects, so they require the
            // full traversal for correctness.
            if (step_index != 0 || !pseudo_is_direct_state(pseudo))
                return false;
        }
    }
    return true;
}

static bool runtime_state_changes_are_incremental_safe(const aether_style_runtime_s* runtime) {
    for (const auto& sheet_pair : runtime->sheets) {
        for (const auto& rule : sheet_pair.second->rules) {
            if (!selector_state_dependency_is_subject_local(rule.selector))
                return false;
        }
    }
    return true;
}

static bool node_has_ancestor_in_set(aether_node_s* node,
                                     const std::unordered_set<aether_node_s*>& roots) {
    for (auto* parent = node ? node->parent : nullptr; parent; parent = parent->parent) {
        if (roots.find(parent) != roots.end()) return true;
    }
    return false;
}

static runtime_node_state_s* parent_state_for(aether_style_runtime_s* runtime, aether_node_s* node) {
    auto* parent = node ? node->parent : nullptr;
    if (!parent) return nullptr;
    auto found = runtime->states.find(parent);
    return found == runtime->states.end() ? nullptr : found->second.get();
}

static int32_t publish_updates(std::vector<aether_style_update_s*>& updates,
                               aether_style_update_t** out_updates) {
    if (updates.empty()) {
        *out_updates = nullptr;
        return 0;
    }
    auto* array = new aether_style_update_t[updates.size()];
    std::copy(updates.begin(), updates.end(), array);
    *out_updates = array;
    return static_cast<int32_t>(updates.size());
}

AETHER_API aether_status_t aether_style_runtime_create(
    const aether_style_runtime_options_t* options,
    const aether_style_environment_t* environment,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data,
    aether_style_runtime_t* out_runtime) {
    if (!out_runtime) return AETHER_ERROR_NULL_ARG;
    style_property_registry_init();
    auto* runtime = new (std::nothrow) aether_style_runtime_s();
    if (!runtime) return AETHER_ERROR_OUT_OF_MEMORY;
    runtime->options = options ? *options : aether_style_runtime_options_t{};
    if (runtime->options.max_variable_depth <= 0) runtime->options.max_variable_depth = 32;
    if (runtime->options.max_variable_expansion_length <= 0)
        runtime->options.max_variable_expansion_length = 65536;
    runtime->environment = environment ? *environment :
        aether_style_environment_t{1280, 720, 1, 16, 0, 0, 0};
    runtime->diag_cb = diagnostics_callback;
    runtime->diag_user_data = diagnostics_user_data;
    *out_runtime = runtime;
    return AETHER_OK;
}

AETHER_API void aether_style_runtime_set_diagnostics_callback(
    aether_style_runtime_t runtime, aether_style_diagnostic_fn callback, void* user_data) {
    if (runtime) { runtime->diag_cb = callback; runtime->diag_user_data = user_data; }
}

AETHER_API void aether_style_runtime_destroy(aether_style_runtime_t runtime) { delete runtime; }

AETHER_API aether_status_t aether_style_runtime_add_stylesheet(
    aether_style_runtime_t runtime, const char* source, int32_t layer, const char* source_name,
    int64_t* out_handle, int32_t* out_rule_count) {
    if (!runtime || !source) return AETHER_ERROR_NULL_ARG;
    const auto handle = runtime->next_sheet_handle++;
    auto sheet = std::make_unique<compiled_style_sheet_s>();
    sheet->handle = handle;
    sheet->layer = static_cast<style_sheet_layer>(layer);
    sheet->source_name = source_name ? source_name : "";
    auto status = style_source_compile(source, handle, layer, sheet->rules, runtime->next_rule_id,
                                       runtime->diag_cb, runtime->diag_user_data);
    if (status != AETHER_OK) return status;
    for (size_t i = 0; i < sheet->rules.size(); ++i)
        sheet->rules[i].source_order = static_cast<int32_t>(i);
    if (out_rule_count) *out_rule_count = static_cast<int32_t>(sheet->rules.size());
    runtime->sheets[handle] = std::move(sheet);
    rebuild_index(runtime);
    runtime->states.clear();
    if (out_handle) *out_handle = handle;
    return AETHER_OK;
}

AETHER_API aether_bool_t aether_style_runtime_remove_stylesheet(
    aether_style_runtime_t runtime, int64_t sheet_handle) {
    if (!runtime || runtime->sheets.erase(sheet_handle) == 0) return AETHER_FALSE;
    rebuild_index(runtime);
    runtime->states.clear();
    return AETHER_TRUE;
}

AETHER_API int32_t aether_style_runtime_resolve_subtree(
    aether_style_runtime_t runtime, aether_node_t root, aether_style_update_t** out_updates) {
    if (!runtime || !root || !out_updates) return 0;
    std::vector<aether_style_update_s*> updates;
    resolve_tree(runtime, root, nullptr, STYLE_IMPACT_NONE, updates);
    return publish_updates(updates, out_updates);
}

AETHER_API int32_t aether_style_runtime_resolve_state_changes(
    aether_style_runtime_t runtime,
    aether_node_t root,
    const aether_node_t* changed_nodes,
    int32_t changed_count,
    aether_style_update_t** out_updates) {
    if (!runtime || !root || !out_updates) return 0;
    if (!changed_nodes || changed_count <= 0) {
        *out_updates = nullptr;
        return 0;
    }

    std::vector<aether_style_update_s*> updates;
    if (!runtime_state_changes_are_incremental_safe(runtime)) {
        resolve_tree(runtime, root, nullptr, STYLE_IMPACT_NONE, updates);
        return publish_updates(updates, out_updates);
    }

    std::vector<aether_node_s*> roots;
    roots.reserve(static_cast<size_t>(changed_count));
    std::unordered_set<aether_node_s*> root_set;
    for (int32_t index = 0; index < changed_count; ++index) {
        auto* node = changed_nodes[index];
        if (!node || node->type != AETHER_NODE_ELEMENT) continue;
        if (!root_set.insert(node).second) continue;
        roots.push_back(node);
    }

    std::vector<aether_node_s*> minimal_roots;
    minimal_roots.reserve(roots.size());
    for (auto* node : roots) {
        if (!node_has_ancestor_in_set(node, root_set))
            minimal_roots.push_back(node);
    }

    for (auto* node : minimal_roots) {
        auto* parent = parent_state_for(runtime, node);
        if (node->parent && !parent) {
            // The requested node is not connected to previously resolved state
            // (for example after a stylesheet or DOM rebuild).  Preserve
            // correctness by falling back to the full tree.
            for (auto* update : updates) delete update;
            updates.clear();
            resolve_tree(runtime, root, nullptr, STYLE_IMPACT_NONE, updates);
            return publish_updates(updates, out_updates);
        }
        // The parent was not recomputed in this incremental pass; its previous
        // inherited_change_impact is historical state from an older traversal,
        // not a fresh change to propagate now.  The changed node itself will
        // compute and propagate any inherited impact caused by its own state.
        resolve_tree(runtime, node, parent, STYLE_IMPACT_NONE, updates);
    }
    return publish_updates(updates, out_updates);
}

AETHER_API aether_status_t aether_style_runtime_get_update(
    aether_style_update_t update, aether_node_t* out_node,
    aether_computed_style_handle_t* out_current, uint32_t* out_impact) {
    if (!update) return AETHER_ERROR_NULL_ARG;
    if (out_node) *out_node = update->node;
    if (out_current) {
        auto* copy = new (std::nothrow) aether_computed_style_handle_s();
        if (!copy) return AETHER_ERROR_OUT_OF_MEMORY;
        if (aether_computed_style_copy(&update->current, &copy->style) != AETHER_OK) {
            delete copy;
            return AETHER_ERROR_OUT_OF_MEMORY;
        }
        *out_current = copy;
    }
    if (out_impact) *out_impact = update->impact;
    return AETHER_OK;
}

AETHER_API void aether_style_updates_free(aether_style_update_t* updates, int32_t count) {
    if (!updates) return;
    for (int32_t i = 0; i < count; ++i) delete updates[i];
    delete[] updates;
}

AETHER_API int32_t aether_style_runtime_set_environment(
    aether_style_runtime_t runtime, const aether_style_environment_t* env,
    aether_node_t root, aether_style_update_t** out_updates) {
    if (!runtime || !env) return 0;
    runtime->environment = *env;
    return aether_style_runtime_resolve_subtree(runtime, root, out_updates);
}

AETHER_API aether_status_t aether_style_runtime_create_resolved_style_map(
    aether_style_runtime_t runtime, aether_doc_t doc, aether_style_map_t* out_map) {
    if (!runtime || !doc || !out_map) return AETHER_ERROR_NULL_ARG;
    auto* map = new (std::nothrow) aether_style_map_s();
    if (!map) return AETHER_ERROR_OUT_OF_MEMORY;

    std::function<bool(aether_node_s*)> visit = [&](aether_node_s* node) {
        if (!node) return true;
        if (node->type == AETHER_NODE_ELEMENT) {
            auto state = runtime->states.find(node);
            if (state != runtime->states.end()) {
                // The map owns a deep copy of every stored style so it stays
                // valid across later state recomputation. Styles without owned
                // buffers copy as a plain struct; only elements with
                // backgrounds, gradients, filters, or shadows pay list copies.
                aether_computed_style_t copy{};
                if (computed_style_deep_copy(state->second->style.style, &copy) != AETHER_OK)
                    return false;
                map->styles.emplace(node, std::move(copy));
            }
        }
        for (auto* child : node->children) {
            if (!visit(child)) return false;
        }
        return true;
    };
    if (!visit(doc)) {
        delete map;
        return AETHER_ERROR_OUT_OF_MEMORY;
    }
    *out_map = map;
    return AETHER_OK;
}

AETHER_API aether_status_t aether_style_runtime_update_resolved_style_map(
    aether_style_runtime_t runtime,
    aether_style_map_t map,
    const aether_node_t* nodes,
    int32_t node_count) {
    if (!runtime || !map || (!nodes && node_count > 0)) return AETHER_ERROR_NULL_ARG;
    if (node_count < 0) return AETHER_ERROR_INVALID_STATE;
    for (int32_t index = 0; index < node_count; ++index) {
        auto* node = nodes[index];
        if (!node || node->type != AETHER_NODE_ELEMENT) continue;
        auto state = runtime->states.find(node);
        if (state != runtime->states.end()) {
            aether_computed_style_t copy{};
            if (computed_style_deep_copy(state->second->style.style, &copy) != AETHER_OK)
                return AETHER_ERROR_OUT_OF_MEMORY;
            auto existing = map->styles.find(node);
            if (existing != map->styles.end()) {
                // Release the previous owned copy before overwriting it.
                aether_computed_style_free(&existing->second);
                existing->second = std::move(copy);
            } else {
                map->styles.emplace(node, std::move(copy));
            }
        } else {
            auto existing = map->styles.find(node);
            if (existing != map->styles.end()) {
                aether_computed_style_free(&existing->second);
                map->styles.erase(existing);
            }
        }
    }
    return AETHER_OK;
}

AETHER_API void aether_style_runtime_clear_cache(aether_style_runtime_t runtime) {
    if (runtime) runtime->states.clear();
}
