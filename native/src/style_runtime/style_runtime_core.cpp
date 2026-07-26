#include <aether/aether.h>
#include "../abi/aether_abi.h"
#include "../css/style_map_types.h"
#include "../dom/dom_types.h"
#include "style_property_registry_internal.h"
#include "style_runtime_types.h"

#include <algorithm>
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
    aether_computed_style_handle_s* current = nullptr;
    uint32_t impact = STYLE_IMPACT_NONE;
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
    left.background_gradient.stops = nullptr;
    right.background_gradient.stops = nullptr;
    left.text_gradient.stops = nullptr;
    right.text_gradient.stops = nullptr;
    if (std::memcmp(&left, &right, sizeof(left)) != 0) return false;
    auto gradients_equal = [](const aether_linear_gradient& x, const aether_linear_gradient& y) {
        if (x.stop_count != y.stop_count) return false;
        if (x.stop_count == 0) return true;
        return x.stops && y.stops &&
            std::memcmp(x.stops, y.stops, sizeof(aether_gradient_stop) * x.stop_count) == 0;
    };
    return gradients_equal(a.background_gradient, b.background_gradient) &&
           gradients_equal(a.text_gradient, b.text_gradient);
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
    if (auto it = node->attributes.find("style"); it != node->attributes.end()) {
        style_source_compile_inline(it->second.c_str(), inline_properties, inline_custom);
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

    for (size_t i = 0; i < cascade.size(); ++i) {
        if (!cascade[i].is_set) continue;
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
            continue;
        case compiled_value_mode::unset:
            if (definition.inherited && parent) continue;
            value = definition.initial_value;
            break;
        case compiled_value_mode::deferred:
            if (!variable_resolve(compiled.deferred_source.c_str(), &scope,
                                  runtime->options.max_variable_depth,
                                  runtime->options.max_variable_expansion_length, value)) {
                report(runtime, "AETHER-CSS-VAR", definition.name.c_str(),
                       compiled.deferred_source.c_str(), "Unable to resolve CSS variable.");
                continue;
            }
            if (!definition.parse(value.c_str(), value)) {
                report(runtime, "AETHER-CSS-VALUE", definition.name.c_str(), value.c_str(),
                       "Resolved CSS value is invalid.");
                continue;
            }
            break;
        }
        definition.apply(&next, value);
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
    if (impact != STYLE_IMPACT_NONE)
        updates.push_back(new aether_style_update_s{generated, &state->style, impact});
}

static void resolve_tree(aether_style_runtime_s* runtime, aether_node_s* node,
                         runtime_node_state_s* parent, std::vector<aether_style_update_s*>& updates) {
    if (!node) return;
    runtime_node_state_s* current_parent = parent;
    if (node->type == AETHER_NODE_ELEMENT) {
        if (!node->is_generated) clear_generated_children(runtime, node);
        uint32_t impact = STYLE_IMPACT_NONE;
        auto* current = compute_node(runtime, node, parent, impact);
        current_parent = current;
        if (impact != STYLE_IMPACT_NONE)
            updates.push_back(new aether_style_update_s{node, &current->style, impact});
        if (!node->is_generated) {
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
    auto status = style_source_compile(source, handle, layer, sheet->rules, runtime->next_rule_id);
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
        copy->style = update->current->style;
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
