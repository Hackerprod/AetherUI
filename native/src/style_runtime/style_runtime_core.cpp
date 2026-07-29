#include <aether/aether.h>
#include "../abi/aether_abi.h"
#include "../css/border_utils.h"
#include "../css/style_map_types.h"
#include "../dom/dom_types.h"
#include "style_property_registry_internal.h"
#include "style_runtime_types.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

struct aether_computed_style_handle_s {
    aether_computed_style_t style;
};

struct runtime_node_state_s {
    aether_computed_style_handle_s style{};
    custom_property_scope_s custom_properties{};
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

static bool style_equal(const aether_computed_style_t& a, const aether_computed_style_t& b) {
    aether_computed_style_t left = a;
    aether_computed_style_t right = b;
    left.background_layers.items = nullptr;
    right.background_layers.items = nullptr;
    left.backdrop_filters.items = nullptr;
    right.backdrop_filters.items = nullptr;
    left.transforms.items = nullptr;
    right.transforms.items = nullptr;
    left.text_gradient.stops = nullptr;
    right.text_gradient.stops = nullptr;
    left.text_shadows.items = nullptr;
    right.text_shadows.items = nullptr;
    left.box_shadows.items = nullptr;
    right.box_shadows.items = nullptr;
    if (std::memcmp(&left, &right, sizeof(left)) != 0) return false;
    auto gradients_equal = [](const aether_linear_gradient& x, const aether_linear_gradient& y) {
        if (x.stop_count != y.stop_count) return false;
        if (x.stop_count == 0) return true;
        return x.stops && y.stops &&
            std::memcmp(x.stops, y.stops, sizeof(aether_gradient_stop) * x.stop_count) == 0;
    };
    return aether_background_layer_list_equal(&a.background_layers, &b.background_layers) == AETHER_TRUE &&
           aether_backdrop_filter_list_equal(&a.backdrop_filters, &b.backdrop_filters) == AETHER_TRUE &&
           aether_transform_list_equal(&a.transforms, &b.transforms) == AETHER_TRUE &&
           gradients_equal(a.text_gradient, b.text_gradient) &&
           aether_shadow_list_equal(&a.text_shadows, &b.text_shadows) == AETHER_TRUE &&
           aether_shadow_list_equal(&a.box_shadows, &b.box_shadows) == AETHER_TRUE;
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

static void normalize_overflow_axes(aether_computed_style_t& style) {
    // CSS Overflow: a visible axis becomes auto when the opposite axis clips
    // or scrolls. Normalizing after the cascade keeps all layout and paint
    // consumers on the same computed values.
    if (overflow_is_visible(style.overflow_x) && !overflow_is_visible(style.overflow_y))
        style.overflow_x = AETHER_OVERFLOW_AUTO;
    if (overflow_is_visible(style.overflow_y) && !overflow_is_visible(style.overflow_x))
        style.overflow_y = AETHER_OVERFLOW_AUTO;
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
                                          runtime_node_state_s* parent, uint32_t& impact,
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

    if (generated_content) {
        generated_content->reset();
        if (content_winner.is_set) {
            std::string content_source = content_winner.source;
            if (str_to_lower(content_source).find("var(") != std::string::npos &&
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

    color_resolution_state_s color_references;
    property_apply_context_s apply_context{&next, &color_references};
    auto apply_definition = [&](size_t i) {
        if (!cascade[i].is_set) return;
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
            return;
        case compiled_value_mode::unset:
            if (definition.inherited && parent) return;
            value = definition.initial_value;
            break;
        case compiled_value_mode::deferred:
            if (!variable_resolve(compiled.deferred_source.c_str(), &scope,
                                  runtime->options.max_variable_depth,
                                  runtime->options.max_variable_expansion_length, value)) {
                report(runtime, "AETHER-CSS-VAR", definition.name.c_str(),
                       compiled.deferred_source.c_str(), "Unable to resolve CSS variable.");
                return;
            }
            if (!definition.parse(value.c_str(), value)) {
                report(runtime,
                       is_calc_expression(value.c_str()) ? "AETHER-CSS-CALC" : "AETHER-CSS-VALUE",
                       definition.name.c_str(), value.c_str(),
                       is_calc_expression(value.c_str())
                           ? "The calc() expression is syntactically or dimensionally invalid."
                           : "Resolved CSS value is invalid.");
                return;
            }
            break;
        }
        property_apply_result_s apply_result{};
        if (definition.apply_with_context_checked) {
            apply_result = definition.apply_with_context_checked(apply_context, value);
        } else if (definition.apply_with_context) {
            definition.apply_with_context(apply_context, value);
        } else if (definition.apply_checked) {
            apply_result = definition.apply_checked(&next, value);
        } else if (definition.apply) {
            definition.apply(&next, value);
        }
        if (!apply_result.success || apply_result.diagnostic_code) {
            report(runtime,
                   apply_result.diagnostic_code ? apply_result.diagnostic_code : "AETHER-CSS-APPLY",
                   definition.name.c_str(), value.c_str(),
                   apply_result.diagnostic_message ? apply_result.diagnostic_message : "CSS value could not be applied.");
        }
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
        if (display && !cascade[static_cast<size_t>(display->id)].is_set)
            next.display = AETHER_DISPLAY_LINE_BREAK;
    }

    auto found = runtime->states.find(node);
    impact = found == runtime->states.end() ? 0x7fu :
        (style_equal(found->second->style.style, next) ? STYLE_IMPACT_NONE : 0x7fu);
    auto state = std::make_unique<runtime_node_state_s>();
    state->style.style = next;
    state->custom_properties = std::move(scope);
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
    std::optional<std::string> content;
    auto* state = compute_node(runtime, generated, host_state, impact, &content);
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
                         runtime_node_state_s* parent, std::vector<aether_style_update_s*>& updates) {
    if (!node) return;
    runtime_node_state_s* current_parent = parent;
    if (node->type == AETHER_NODE_ELEMENT) {
        if (node->is_generated) {
            // add_generated_pseudo already computed and published this node in
            // the current traversal.  Reusing that state avoids replacing it
            // while its update is being consumed below.
            auto existing = runtime->states.find(node);
            if (existing != runtime->states.end()) {
                current_parent = existing->second.get();
            } else {
                uint32_t impact = STYLE_IMPACT_NONE;
                auto* current = compute_node(runtime, node, parent, impact);
                current_parent = current;
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
            auto* current = compute_node(runtime, node, parent, impact);
            current_parent = current;
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
    for (auto* child : node->children) resolve_tree(runtime, child, current_parent, updates);
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
    resolve_tree(runtime, root, nullptr, updates);
    if (updates.empty()) { *out_updates = nullptr; return 0; }
    auto* array = new aether_style_update_t[updates.size()];
    std::copy(updates.begin(), updates.end(), array);
    *out_updates = array;
    return static_cast<int32_t>(updates.size());
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

    std::function<void(aether_node_s*)> visit = [&](aether_node_s* node) {
        if (!node) return;
        if (node->type == AETHER_NODE_ELEMENT) {
            auto state = runtime->states.find(node);
            if (state != runtime->states.end()) {
                // Gradient buffers remain owned by runtime->states for this frame.
                map->styles.emplace(node, state->second->style.style);
            }
        }
        for (auto* child : node->children) visit(child);
    };
    visit(doc);
    *out_map = map;
    return AETHER_OK;
}

AETHER_API void aether_style_runtime_clear_cache(aether_style_runtime_t runtime) {
    if (runtime) runtime->states.clear();
}
