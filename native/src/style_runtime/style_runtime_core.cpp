#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../css/computed_style_handle.h"
#include "../css/measure_state.h"
#include <stdexcept>
#include <thread>
#include "../abi/aether_abi.h"
#include "../css/border_utils.h"
#include "../css/computed_style_internal.h"
#include "../css/style_map_types.h"
#include "../dom/dom_types.h"
#include "../primitives/html_space_tokens.h"
#include "style_property_registry_internal.h"
#include "style_runtime_types.h"
#include "style_source_limits.h"
#include "css_variable_tokens.h"
#include "custom_property_scope_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>


struct runtime_node_state_s {
    aether_computed_style_handle_s style{};
    computed_custom_property_scope_ptr custom_properties;
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
    std::unordered_map<aether_node_s*, std::shared_ptr<runtime_node_state_s>> states;
    std::shared_ptr<document_measure_context_s> document_measures;
    std::shared_ptr<const struct style_revision_s> revision;
    uint64_t generation = 0;
    uint64_t sheet_epoch = 0;
    bool preparing = false;
    aether_node_s* legacy_inheritance_root = nullptr; // Candidate-only compatibility boundary.
    std::thread::id owner_thread = std::this_thread::get_id();
    rule_index_s index;
};

// States are immutable once referenced from a revision. The working runtime
// used by preparation clones them before compute_node can mutate a no-op.
struct style_revision_entry_s {
    aether_node_s* node = nullptr;
    std::shared_ptr<const runtime_node_state_s> state;
    uint32_t impact = STYLE_IMPACT_NONE;
};
struct style_revision_s {
    bool contextual = true;
    uint64_t generation = 0, document_identity = 0, input_epoch = 0, structure_epoch = 0, style_tree_epoch = 0;
    aether_style_environment_t environment{};
    std::shared_ptr<const document_measure_context_s> document;
    std::shared_ptr<node_identity_s> document_lifetime;
    std::vector<style_revision_entry_s> entries;
    std::vector<style_revision_entry_s> changes;
    std::vector<std::shared_ptr<aether_node_s>> generated;
};
struct aether_style_revision_handle_s { std::shared_ptr<const style_revision_s> value; };

// An add must finish its transaction before invoking consumer code. Each call
// owns its batch and captures its recipient, so callbacks may add/remove sheets
// without invalidating candidate handles/IDs or redirecting an existing batch.
struct stylesheet_diagnostics_s {
    struct message_s {
        std::optional<std::string> code, property, value, message;
    };
    aether_style_diagnostic_fn callback;
    void* user_data;
    std::vector<message_s> messages;

    stylesheet_diagnostics_s(aether_style_diagnostic_fn fn, void* data)
        : callback(fn), user_data(data) {}

    static std::optional<std::string> copy(const char* text) {
        return text ? std::optional<std::string>(text) : std::nullopt;
    }
    static void collect(const char* code, const char* property, const char* value,
                        const char* message, void* data) {
        auto& batch = *static_cast<stylesheet_diagnostics_s*>(data);
        batch.messages.push_back({copy(code), copy(property), copy(value), copy(message)});
    }
    aether_style_diagnostic_fn collector() const noexcept {
        return callback ? collect : nullptr;
    }
    void replay() const {
        if (!callback) return;
        for (const auto& entry : messages) {
            callback(entry.code ? entry.code->c_str() : nullptr,
                     entry.property ? entry.property->c_str() : nullptr,
                     entry.value ? entry.value->c_str() : nullptr,
                     entry.message ? entry.message->c_str() : nullptr, user_data);
        }
    }
};

static aether_style_update_s* make_style_update(aether_node_s* node,
                                                 aether_computed_style_handle_s& current,
                                                 uint32_t impact) {
    auto* update = new (std::nothrow) aether_style_update_s();
    if (!update) return nullptr;
    update->node = node;
    update->impact = impact;
    update->current.measures = current.measures;
    update->current.revision = current.revision;
    update->current.document_identity = current.document_identity;
    update->current.node_identity = current.node_identity;
    update->current.node_lifetime = current.node_lifetime;
    update->current.document_lifetime = current.document_lifetime;
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
        aether::html_space_token_scanner scanner(it->second);
        std::string_view token;
        while (scanner.next(token))
            info.classes.emplace_back(token); // Own the bytes beyond this scan.
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

static rule_index_s prepare_rule_index(const aether_style_runtime_s* runtime,
    const compiled_style_sheet_s* extra = nullptr, int64_t omit = 0) {
    std::vector<const compiled_style_sheet_s*> sheets;
    sheets.reserve(runtime->sheets.size() + (extra ? 1 : 0));
    for (const auto& pair : runtime->sheets) if (pair.first != omit) sheets.push_back(pair.second.get());
    if (extra) sheets.push_back(extra);
    rule_index_s prepared;
    rule_index_rebuild(&prepared, sheets);
    return prepared;
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
    // The legacy subtree entry point starts an isolated inheritance scope.
    // Contextual preparation never installs this compatibility boundary.
    if (node == runtime->legacy_inheritance_root) { parent = nullptr; inherited_parent_impact = 0; }
    auto* registry = style_property_registry_get_global();
    aether_computed_style_t next{};
    if (computed_style_inherit_checked(parent ? &parent->style.style : nullptr, &next) != AETHER_OK)
        throw std::bad_alloc();
    struct next_style_guard_s {
        aether_computed_style_t* value;
        ~next_style_guard_s() { if (value) aether_computed_style_free(value); }
    } next_guard{&next};
    auto measures = std::make_shared<measure_state_s>();
    measures->document = runtime->document_measures;
    measures->parent_font_size = parent ? parent->style.style.font_size : measures->document->initial_font_size;
    measures->self_font_size = measures->parent_font_size;
    next.font_size = measures->self_font_size;
    if (parent) measure_set_property_dependencies(*measures, "font-size", MEASURE_PARENT);
    if (parent && parent->style.measures) {
        measures->line_height = parent->style.measures->line_height;
        measure_set_property_dependencies(*measures, "line-height", MEASURE_PARENT);
    }
    const bool document_element = !node->is_generated && node->type == AETHER_NODE_ELEMENT &&
        ((!node->parent) || (node->parent->type == AETHER_NODE_DOCUMENT &&
          aether_doc_get_document_element(static_cast<aether_doc_s*>(node->parent)) == node));
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
    std::vector<compiled_shorthand_declaration_s> inline_shorthands;
    // Generated pseudo-elements retain host attributes for selector matching, but
    // the host's inline declaration block does not cascade onto ::before/::after.
    if (!node->is_generated) if (auto it = node->attributes.find("style"); it != node->attributes.end()) {
        style_source_compile_inline(it->second.c_str(), inline_properties, inline_custom, inline_shorthands,
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

    custom_property_sources_s sources;
    for (const auto& pair : custom_winners) {
        auto value = str_trim(pair.second.source);
        if (str_iequals(value, "inherit") || str_iequals(value, "unset")) continue;
        sources.emplace(pair.first, custom_property_source_s{pair.second.source,
            str_iequals(value, "initial") || str_iequals(value, "revert")});
    }

    auto scope = compute_custom_property_scope(sources, parent ? parent->custom_properties : nullptr,
        runtime->options.max_variable_depth, runtime->options.max_variable_expansion_length);

    // Custom properties have no dependency on the final ordinary style in this
    // runtime. Compile each pending group once per node, then offer either all
    // valid siblings or unset at the ORIGINAL priority. A computed failure must
    // not resurrect lower-priority declarations; other longhand winners survive.
    auto offer_shorthand = [&](const compiled_shorthand_declaration_s& declaration,
                               const cascade_priority_t& priority) {
        const auto* shorthand = registry->try_get_shorthand(declaration.name.c_str());
        if (!shorthand) return;
        std::string resolved;
        std::vector<compiled_property_declaration_s> staged;
        const bool substituted = resolve_computed_value(declaration.source.c_str(), scope.get(),
            runtime->options.max_variable_depth, runtime->options.max_variable_expansion_length, resolved);
        const bool valid = substituted && validate_pending_declaration_value(resolved) &&
            !variable_has_reference(resolved) &&
            style_source_compile_shorthand(*shorthand, resolved, declaration.important,
                declaration.declaration_order, registry, staged, runtime->diag_cb, runtime->diag_user_data);
        if (valid) {
            for (const auto& item : staged)
                cascade[static_cast<size_t>(item.property_id)].offer(priority, item.value);
        } else {
            report(runtime, substituted ? "AETHER-CSS-VALUE" : "AETHER-CSS-VAR", declaration.name.c_str(),
                   declaration.source.c_str(), substituted ? "Resolved shorthand is invalid." : "Unable to resolve CSS variable.");
            compiled_style_value_s unset;
            unset.mode = compiled_value_mode::unset;
            for (auto id : shorthand->target_ids)
                cascade[static_cast<size_t>(id)].offer(priority, unset);
        }
    };
    for (const auto* rule : candidates) {
        if (rule->selector.pseudo_element != node->pseudo_element) continue;
        if (!media_condition_matches(rule->condition, &runtime->environment) ||
            !selector_match(rule->selector, node)) continue;
        for (const auto& declaration : rule->shorthand_declarations) {
            offer_shorthand(declaration, cascade_priority_t{
                declaration.important, false, rule->layer, rule->selector.specificity,
                rule->source_order, declaration.declaration_order});
        }
    }
    for (const auto& declaration : inline_shorthands) {
        offer_shorthand(declaration, cascade_priority_t{
            declaration.important, true, style_sheet_layer::author,
            specificity_s{1, 0, 0}, INT32_MAX, declaration.declaration_order});
    }

    if (generated_content) {
        generated_content->reset();
        if (content_winner.is_set) {
            std::string content_source = content_winner.source;
            if (variable_has_reference(content_source) &&
                !resolve_computed_value(content_source.c_str(), scope.get(),
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
                                    bool emit_diagnostics,
                                    measure_apply_state_s& destination) {
        destination.set_property_dependencies(measure_source_dependencies(value));
        property_apply_context_s context{&style, &references, &destination};
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
        const std::string initial_value = definition.name == "font-size" ?
            std::to_string(measures->document->initial_font_size) + "px" : definition.initial_value;
        std::string value;
        bool explicit_inherit = false;
        switch (compiled.mode) {
        case compiled_value_mode::value:
            value = compiled.typed_value;
            break;
        case compiled_value_mode::initial:
            value = initial_value;
            break;
        case compiled_value_mode::inherit:
            if (!definition.inherit_explicit && !definition.inherit_context) return false;
            explicit_inherit = parent != nullptr;
            value = initial_value;
            break;
        case compiled_value_mode::unset:
            if (definition.inherited && parent) return false;
            value = initial_value;
            break;
        case compiled_value_mode::deferred: {
            if (!resolve_computed_value(compiled.deferred_source.c_str(), scope.get(),
                                  runtime->options.max_variable_depth,
                                  runtime->options.max_variable_expansion_length, value)) {
                report(runtime, "AETHER-CSS-VAR", definition.name.c_str(),
                       compiled.deferred_source.c_str(), "Unable to resolve CSS variable.");
                if (!definition.css_wide || (definition.inherited && parent)) return false;
                // This non-inherited opt-in property keeps the invalid winner.
                // Applying initial also accounts for its delta from old state.
                value = initial_value;
                break;
            }
            const auto css_wide = definition.css_wide ? definition.css_wide(value) : property_css_wide_e::none;
            if (css_wide != property_css_wide_e::none) {
                if (css_wide == property_css_wide_e::unset && definition.inherited && parent) return false;
                explicit_inherit = css_wide == property_css_wide_e::inherit &&
                    parent && (definition.inherit_explicit || definition.inherit_context);
                value = initial_value;
                break;
            }
            if (!definition.parse(value.c_str(), value)) {
                const auto failure = definition.parse_failure ? definition.parse_failure(value.c_str()) : property_apply_result_s{};
                report(runtime,
                       failure.diagnostic_code ? failure.diagnostic_code :
                           is_calc_expression(value.c_str()) ? "AETHER-CSS-CALC" : "AETHER-CSS-VALUE",
                       definition.name.c_str(), value.c_str(),
                       failure.diagnostic_message ? failure.diagnostic_message :
                           is_calc_expression(value.c_str())
                               ? "The calc() expression is syntactically or dimensionally invalid."
                               : "Resolved CSS value is invalid.");
                if (!definition.css_wide || (definition.inherited && parent)) return false;
                value = initial_value;
            }
            break;
        }
        }
        auto apply_selected = [&](aether_computed_style_t& style,
                                  color_resolution_state_s& references, bool emit_diagnostics,
                                  measure_apply_state_s& destination) {
            if (explicit_inherit) {
                if (definition.inherit_context) {
                    property_apply_context_s context{&style, &references, &destination, &parent->color_references};
                    definition.inherit_context(context, parent->style.style, parent->style.measures.get());
                } else definition.inherit_explicit(&style, parent->style.style);
                return true;
            }
            return apply_property_value(definition, style, references, value, emit_diagnostics, destination);
        };
        measure_apply_state_s selected_measures(*measures, definition.name);
        if (!apply_selected(next, color_references, true, selected_measures)) return false;
        bool property_changed = previous_style == nullptr;
        if (previous_style) {
            aether_computed_style_handle_s previous_handle{};
            previous_handle.style = *previous_style;
            aether_computed_style_t probe{};
            next_style_guard_s probe_guard{&probe};
            if (aether_computed_style_copy(&previous_handle, &probe) == AETHER_OK) {
                color_resolution_state_s probe_references = found->second->color_references;
                // Read the candidate AFTER the winning declaration, while the
                // flat and symbolic color references deliberately start at OLD.
                measure_property_delta_s delta;
                measure_apply_state_s probe_measures(*measures, delta, definition.name);
                if (apply_selected(probe, probe_references, false, probe_measures)) {
                    resolve_symbolic_colors(probe, probe_references);
                    normalize_overflow_axes(probe);
                    property_changed = !style_equal(*previous_style, probe);
                } else {
                    property_changed = true;
                }
                aether_computed_style_free(&probe);
                probe_guard.value = nullptr;
            } else {
                throw std::bad_alloc();
            }
            previous_handle.style = {};
        }
        if (property_changed) {
            declared_impact |= definition.impact;
            if ((definition.impact & STYLE_IMPACT_INHERITED) != 0)
                declared_inherited_impact |= definition.impact;
        }
        return true;
    };

    // Font-size has a semantic phase independent of registry/declaration order.
    for (size_t i = 0; i < cascade.size(); ++i)
        if (registry->definitions[i].name == "font-size") apply_definition(i);
    measures->self_font_size = next.font_size;
    if (document_element) runtime->document_measures->root_font_size = next.font_size;

    // The cascade has already selected winners. Resolve the node's final
    // concrete text color first, then apply properties that may carry symbolic
    // currentColor references. This makes declaration/registry order immaterial.
    for (size_t i = 0; i < cascade.size(); ++i) {
        if (cascade[i].is_set && is_color_producer(registry->definitions[i]))
            apply_definition(i);
    }
    for (size_t i = 0; i < cascade.size(); ++i) {
        if (cascade[i].is_set && !is_color_producer(registry->definitions[i]) && registry->definitions[i].name != "font-size")
            apply_definition(i);
    }
    if (!std::isfinite(measure_used_line_height(measures.get(), next)))
        throw std::overflow_error("Non-finite inherited line-height");
    measure_compute_paint_lengths(next, *measures);
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

    measure_recompute_dependencies(*measures);
    const bool is_new_node = found == runtime->states.end();
    const auto* old_measures = is_new_node ? nullptr : found->second->style.measures.get();
    const bool contextual_geometry_changed = old_measures &&
        (std::memcmp(&old_measures->row_gap, &measures->row_gap, sizeof(aether_css_length)) != 0 ||
         std::memcmp(&old_measures->column_gap, &measures->column_gap, sizeof(aether_css_length)) != 0 ||
         old_measures->line_height.kind != measures->line_height.kind ||
         old_measures->line_height.value != measures->line_height.value);
    if (contextual_geometry_changed) declared_impact |= STYLE_IMPACT_LAYOUT | STYLE_IMPACT_TEXT_LAYOUT;

    const uint32_t inherited_symbolic_impact = !is_new_node &&
        found->second->color_references.text_shadow_uses_current_color !=
            color_references.text_shadow_uses_current_color
        ? STYLE_IMPACT_PAINT | STYLE_IMPACT_INHERITED : STYLE_IMPACT_NONE;
    const bool changed = is_new_node || contextual_geometry_changed || !style_equal(found->second->style.style, next);
    if (!changed) {
        impact = STYLE_IMPACT_NONE;
        inherited_change_impact = inherited_symbolic_impact;
        if (!is_new_node) {
            // Retain style buffers, but always publish the new computed snapshot.
            // A concrete no-op can still change custom values or replace the
            // parent generation. Children own their previous snapshots safely.
            // Descendants are visited even when this node emits no update.
            aether_computed_style_free(&next);
            next_guard.value = nullptr;
            found->second->style.measures = measures;
            found->second->custom_properties = std::move(scope);
            // Equal concrete colors can still change inherited currentColor.
            // Keep storage stable, but publish metadata through intermediaries.
            found->second->color_references = std::move(color_references);
            found->second->inherited_change_impact = inherited_change_impact;
            return found->second.get();
        }
    } else if (is_new_node) {
        impact = STYLE_IMPACT_ALL_BITS;
        inherited_change_impact = declared_inherited_impact | inherited_parent_impact;
    } else if (!transform_origin_equal(found->second->style.style.transform_origin, next.transform_origin) &&
               [&] {
                   // Removal or computed-invalid origin can occur after any
                   // number of no-op resolves. Compare the final scalar delta,
                   // not historical declaration impacts. This copy only borrows.
                   auto comparison = found->second->style.style;
                   comparison.transform_origin = next.transform_origin;
                   return style_equal(comparison, next);
               }()) {
        const auto* origin = registry->try_get("transform-origin");
        impact = origin ? origin->impact : STYLE_IMPACT_ALL_BITS;
        inherited_change_impact = STYLE_IMPACT_NONE;
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
    inherited_change_impact |= inherited_symbolic_impact;
    if (!is_new_node) {
        const auto& previous = found->second->style.style;
        const bool before_context = previous.transforms.count > 0 || previous.backdrop_filters.count > 0;
        const bool after_context = next.transforms.count > 0 || next.backdrop_filters.count > 0;
        if (before_context != after_context) impact |= STYLE_IMPACT_LAYOUT;
    }
    auto state = std::make_unique<runtime_node_state_s>();
    state->style.style = next;
    next_guard.value = nullptr;
    state->style.measures = measures;
    state->custom_properties = std::move(scope);
    state->color_references = std::move(color_references);
    state->declared_impact = declared_impact;
    state->declared_inherited_impact = declared_inherited_impact;
    state->inherited_change_impact = inherited_change_impact;
    auto* result = state.get();
    runtime->states[node] = std::move(state);
    return result;
}

struct prepared_children_s {
    aether_node_s* host = nullptr;
    std::vector<aether_node_s*> children;
    std::vector<std::shared_ptr<aether_node_s>> storage;
};

static std::shared_ptr<aether_node_s> create_generated_node() {
    return std::shared_ptr<aether_node_s>(new aether_node_s(), [](aether_node_s* node) noexcept {
        for (auto* child : node->children) delete child;
        delete node;
    });
}
static std::shared_ptr<aether_node_s> prepare_generated_pseudo(
    aether_style_runtime_s* staged, aether_node_s* host, runtime_node_state_s* host_state,
    aether_pseudo_element_t pseudo, std::vector<std::unique_ptr<aether_style_update_s>>& updates) {
    auto generated = create_generated_node();
    generated->type = AETHER_NODE_ELEMENT; generated->parent = host;
    generated->tag_name = host->tag_name; generated->attributes = host->attributes;
    generated->state = host->state; generated->is_generated = true;
    generated->snapshot_owned = true; generated->identity->attached = false;
    generated->pseudo_element = pseudo; generated->style_origin = host;
    uint32_t impact = 0, inherited = 0;
    std::optional<std::string> content;
    auto* state = compute_node(staged, generated.get(), host_state,
        host_state ? host_state->inherited_change_impact : 0, impact, inherited, &content);
    if (!content) { staged->states.erase(generated.get()); return {}; }
    if (!content->empty()) {
        auto text = std::make_unique<aether_node_s>(); text->type = AETHER_NODE_TEXT;
        text->parent = generated.get(); text->text = std::move(*content);
        generated->children.push_back(text.get()); text.release();
    }
    if (impact) {
        std::unique_ptr<aether_style_update_s> update(make_style_update(generated.get(), state->style, impact));
        if (!update) throw std::bad_alloc();
        updates.push_back(std::move(update));
    }
    return generated;
}
static void prepare_tree(aether_style_runtime_s* staged, aether_node_s* node,
    runtime_node_state_s* parent, uint32_t inherited_parent,
    std::vector<std::unique_ptr<aether_style_update_s>>& updates,
    std::vector<prepared_children_s>& links, std::vector<aether_node_s*>& order) {
    if (!node || node->is_generated) return;
    auto* next_parent = parent;
    uint32_t next_inherited = inherited_parent;
    if (node->type == AETHER_NODE_ELEMENT) {
        uint32_t impact = 0;
        next_parent = compute_node(staged, node, parent, inherited_parent, impact, next_inherited);
        order.push_back(node);
        if (impact) {
            std::unique_ptr<aether_style_update_s> update(make_style_update(node, next_parent->style, impact));
            if (!update) throw std::bad_alloc();
            updates.push_back(std::move(update));
        }
        prepared_children_s link; link.host = node;
        link.children.reserve(node->children.size() + 2); link.storage.reserve(2);
        auto before = prepare_generated_pseudo(staged, node, next_parent, aether_pseudo_element_t::before, updates);
        auto after = prepare_generated_pseudo(staged, node, next_parent, aether_pseudo_element_t::after, updates);
        if (before) { link.children.push_back(before.get()); order.push_back(before.get()); link.storage.push_back(std::move(before)); }
        for (auto* child : node->children) if (!child->is_generated) link.children.push_back(child);
        if (after) { link.children.push_back(after.get()); order.push_back(after.get()); link.storage.push_back(std::move(after)); }
        links.push_back(std::move(link));
    }
    for (auto* child : node->children)
        if (!child->is_generated) prepare_tree(staged, child, next_parent, next_inherited, updates, links, order);
}

AETHER_API aether_status_t aether_style_runtime_create(
    const aether_style_runtime_options_t* options,
    const aether_style_environment_t* environment,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data,
    aether_style_runtime_t* out_runtime) {
    if (!out_runtime) return AETHER_ERROR_NULL_ARG;
    *out_runtime = nullptr;
    try {
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
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
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
    if (runtime->preparing || runtime->owner_thread != std::this_thread::get_id()) return AETHER_ERROR_INVALID_STATE;
    try {
    stylesheet_diagnostics_s diagnostics(runtime->diag_cb, runtime->diag_user_data);
    const auto collector = diagnostics.collector();
    const auto status = [&]() -> aether_status_t {
        if (!style_sheet_handle_available(runtime->next_sheet_handle)) {
            if (collector) collector("AETHER-CSS-LIMIT", "", "",
                "The cumulative stylesheet handle capacity is exhausted.", &diagnostics);
            return AETHER_ERROR_INVALID_STATE;
        }
        const auto handle = runtime->next_sheet_handle;
        auto next_rule_id = runtime->next_rule_id;
        auto sheet = std::make_unique<compiled_style_sheet_s>();
        sheet->handle = handle;
        sheet->layer = static_cast<style_sheet_layer>(layer);
        sheet->source_name = source_name ? source_name : "";
        const auto result = style_source_compile(source, handle, layer, sheet->rules, next_rule_id,
                                                collector, &diagnostics);
        if (result != AETHER_OK) return result;
        // Preserve the compiler's global positions, including the shared position
        // of selectors in one list. ID capacity already bounds this narrowing.
        const auto rule_count = static_cast<int32_t>(sheet->rules.size());
        auto index = prepare_rule_index(runtime, sheet.get());
        runtime->sheets.emplace(handle, std::move(sheet)); // Last fallible allocation.
        using std::swap;
        swap(runtime->index, index);
        runtime->states.clear();
        ++runtime->sheet_epoch;
        runtime->next_sheet_handle = handle + 1;
        runtime->next_rule_id = next_rule_id;
        if (out_handle) *out_handle = handle;
        if (out_rule_count) *out_rule_count = rule_count;
        return AETHER_OK;
    }(); // Failed staging is destroyed before any user callback can run.
    try { diagnostics.replay(); } catch (...) { /* Callback exceptions do not roll back committed input. */ }
    // Callbacks may have changed sheets, counters, or resolved state. Never read
    // or write runtime here, and never replay from a destructor during unwind.
    return status;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}

AETHER_API aether_bool_t aether_style_runtime_remove_stylesheet(
    aether_style_runtime_t runtime, int64_t sheet_handle) {
    if (!runtime || runtime->preparing || runtime->owner_thread != std::this_thread::get_id() ||
        !runtime->sheets.count(sheet_handle)) return AETHER_FALSE;
    try {
        auto index = prepare_rule_index(runtime, nullptr, sheet_handle);
        using std::swap;
        swap(runtime->index, index);
        runtime->sheets.erase(sheet_handle);
        runtime->states.clear(); ++runtime->sheet_epoch;
        return AETHER_TRUE;
    } catch (...) { return AETHER_FALSE; }
}

static int32_t resolve_legacy_atomic(aether_style_runtime_t runtime, aether_node_t root,
    const aether_style_environment_t* environment, aether_style_update_t** out_updates, bool isolate_legacy_root = false);
AETHER_API int32_t aether_style_runtime_resolve_subtree(
    aether_style_runtime_t runtime, aether_node_t root, aether_style_update_t** out_updates) {
    return resolve_legacy_atomic(runtime, root, nullptr, out_updates, true);
}
AETHER_API int32_t aether_style_runtime_resolve_state_changes(
    aether_style_runtime_t runtime, aether_node_t root, const aether_node_t* changed_nodes,
    int32_t changed_count, aether_style_update_t** out_updates) {
    if (out_updates) *out_updates = nullptr;
    if (!changed_nodes || changed_count <= 0) return 0;
    // Conservatively visit the complete tree: font/viewport and metadata edges
    // cannot be inferred from a visual-only changed-node selection.
    return resolve_legacy_atomic(runtime, root, nullptr, out_updates);
}

AETHER_API aether_status_t aether_style_runtime_get_update(
    aether_style_update_t update, aether_node_t* out_node,
    aether_computed_style_handle_t* out_current, uint32_t* out_impact) {
    if (out_current) *out_current = nullptr;
    if (out_node) *out_node = nullptr;
    if (out_impact) *out_impact = 0;
    if (!update) return AETHER_ERROR_NULL_ARG;
    if (out_current) {
        auto* copy = new (std::nothrow) aether_computed_style_handle_s();
        if (!copy) return AETHER_ERROR_OUT_OF_MEMORY;
        if (aether_computed_style_copy(&update->current, &copy->style) != AETHER_OK) {
            delete copy;
            return AETHER_ERROR_OUT_OF_MEMORY;
        }
        copy->measures = update->current.measures; copy->revision = update->current.revision;
        copy->document_identity = update->current.document_identity; copy->node_identity = update->current.node_identity;
        copy->node_lifetime = update->current.node_lifetime; copy->document_lifetime = update->current.document_lifetime;
        *out_current = copy;
    }
    if (out_node) *out_node = update->node;
    if (out_impact) *out_impact = update->impact;
    return AETHER_OK;
}

AETHER_API void aether_style_updates_free(aether_style_update_t* updates, int32_t count) {
    if (!updates) return;
    for (int32_t i = 0; i < count; ++i) delete updates[i];
    delete[] updates;
}

AETHER_API int32_t aether_style_runtime_set_environment(
    aether_style_runtime_t runtime, const aether_style_environment_t* environment,
    aether_node_t root, aether_style_update_t** out_updates) {
    if (!environment) { if (out_updates) *out_updates = nullptr; return 0; }
    return resolve_legacy_atomic(runtime, root, environment, out_updates);
}

struct aether_style_transaction_s {
    aether_style_runtime_s* owner = nullptr;
    uint64_t base_generation = 0, base_sheet_epoch = 0, input_epoch = 0, base_style_tree_epoch = 0;
    std::shared_ptr<node_identity_s> document_lifetime;
    std::unique_ptr<aether_style_runtime_s> candidate;
    std::vector<std::unique_ptr<aether_style_update_s>> updates;
    std::unique_ptr<aether_style_update_t[]> legacy_array;
    std::vector<prepared_children_s> links;
    std::shared_ptr<style_revision_s> revision;
    std::unique_ptr<aether_style_revision_handle_s> output;
    std::unique_ptr<stylesheet_diagnostics_s> diagnostics;
    bool committed = false;
};

static bool valid_measure_environment(const aether_style_environment_t& env) noexcept {
    return std::isfinite(env.viewport_width) && env.viewport_width > 0 &&
        std::isfinite(env.viewport_height) && env.viewport_height > 0 &&
        std::isfinite(env.root_font_size) && env.root_font_size >= 0 &&
        std::isfinite(env.dpi_scale) && env.dpi_scale > 0;
}
static std::shared_ptr<runtime_node_state_s> clone_runtime_state(const runtime_node_state_s& source) {
    auto target = std::make_shared<runtime_node_state_s>();
    if (computed_style_deep_copy(source.style.style, &target->style.style) != AETHER_OK) throw std::bad_alloc();
    target->style.measures = source.style.measures;
    target->style.document_identity = source.style.document_identity;
    target->style.node_identity = source.style.node_identity;
    target->style.node_lifetime = source.style.node_lifetime;
    target->style.document_lifetime = source.style.document_lifetime;
    target->custom_properties = source.custom_properties;
    target->color_references = source.color_references;
    target->declared_impact = source.declared_impact;
    target->declared_inherited_impact = source.declared_inherited_impact;
    target->inherited_change_impact = source.inherited_change_impact;
    return target;
}
AETHER_API uint32_t aether_contextual_style_version(void) { return 1; }
static aether_status_t prepare_revision_impl(
    aether_style_runtime_t runtime, aether_node_t root,
    const aether_style_environment_t* environment, aether_style_transaction_t* out_transaction, bool isolate_legacy_root) {
    if (out_transaction) *out_transaction = nullptr;
    if (!runtime || !root || !out_transaction) return AETHER_ERROR_NULL_ARG;
    if (runtime->preparing || runtime->owner_thread != std::this_thread::get_id()) return AETHER_ERROR_INVALID_STATE;
    const auto requested_environment = environment ? *environment : runtime->environment;
    if (!valid_measure_environment(requested_environment)) return AETHER_ERROR_INVALID_STATE;
    struct guard_s { bool& active; explicit guard_s(bool& v) : active(v) { active = true; } ~guard_s() { active = false; } } guard(runtime->preparing);
    try {
        auto transaction = std::make_unique<aether_style_transaction_s>();
        transaction->owner = runtime; transaction->base_generation = runtime->generation;
        transaction->base_sheet_epoch = runtime->sheet_epoch;
        auto* document = aether_dom_root(root);
        transaction->document_lifetime = document->identity;
        transaction->input_epoch = document->identity->mutation_epoch;
        transaction->base_style_tree_epoch = document->identity->style_tree_epoch;
        transaction->candidate = std::make_unique<aether_style_runtime_s>();
        auto& staged = *transaction->candidate;
        staged.options = runtime->options; staged.environment = requested_environment;
        if (isolate_legacy_root && root != document && root->parent && root->parent->type != AETHER_NODE_DOCUMENT)
            staged.legacy_inheritance_root = root;
        staged.index = runtime->index;
        staged.document_measures = std::make_shared<document_measure_context_s>();
        staged.document_measures->viewport_width = requested_environment.viewport_width;
        staged.document_measures->viewport_height = requested_environment.viewport_height;
        staged.document_measures->initial_font_size = requested_environment.root_font_size;
        staged.document_measures->root_font_size = requested_environment.root_font_size;
        staged.document_measures->generation = runtime->generation + 1;
        staged.document_measures->identity = aether_node_identity(document);
        transaction->diagnostics = std::make_unique<stylesheet_diagnostics_s>(runtime->diag_cb, runtime->diag_user_data);
        staged.diag_cb = transaction->diagnostics->collector(); staged.diag_user_data = transaction->diagnostics.get();
        // Retain comparable states only while their tokens are still live. Never
        // dereference a pointer from a retired document generation.
        for (const auto& entry : runtime->states) {
            const auto& token = entry.second->style.node_lifetime;
            if (!token || (token->alive && token->attached))
                staged.states.emplace(entry.first, clone_runtime_state(*entry.second));
        }
        std::vector<aether_node_s*> order;
        prepare_tree(&staged, document, nullptr, 0, transaction->updates, transaction->links, order);
        std::unordered_set<aether_node_s*> current_nodes(order.begin(), order.end());
        for (auto it = staged.states.begin(); it != staged.states.end();) {
            if (!current_nodes.count(it->first)) it = staged.states.erase(it); else ++it;
        }
        auto revision = std::make_shared<style_revision_s>();
        revision->contextual = staged.legacy_inheritance_root == nullptr;
        revision->generation = staged.document_measures->generation;
        revision->document_identity = staged.document_measures->identity;
        revision->input_epoch = transaction->input_epoch;
        revision->structure_epoch = document->identity->structure_epoch;
        revision->style_tree_epoch = document->identity->style_tree_epoch + 1;
        revision->document_lifetime = document->identity;
        revision->environment = requested_environment;
        revision->document = staged.document_measures;
        revision->entries.reserve(order.size());
        std::unordered_map<aether_node_s*, uint32_t> impacts;
        for (const auto& update : transaction->updates) impacts[update->node] |= update->impact;
        for (auto* node : order) {
            auto state = staged.states.at(node);
            state->style.document_identity = revision->document_identity;
            state->style.node_identity = aether_node_identity(node);
            state->style.node_lifetime = node->identity;
            state->style.document_lifetime = revision->document_lifetime;
            revision->entries.push_back({node, std::move(state), impacts[node]});
        }
        // The manifest is complete and conservative, including NONE entries.
        // It is deliberately independent of the legacy visual update list.
        revision->changes = revision->entries;
        if (runtime->revision) for (const auto& old : runtime->revision->entries)
            if (!current_nodes.count(old.node)) revision->changes.push_back({old.node, {}, STYLE_IMPACT_ALL_BITS});
        for (const auto& link : transaction->links)
            for (const auto& pseudo : link.storage) revision->generated.push_back(pseudo);
        for (auto& update : transaction->updates) {
            const auto& state = staged.states.at(update->node)->style;
            update->current.revision = revision;
            update->current.measures = state.measures;
            update->current.document_identity = state.document_identity;
            update->current.node_identity = state.node_identity;
            update->current.node_lifetime = state.node_lifetime;
            update->current.document_lifetime = state.document_lifetime;
        }
        if (transaction->updates.size() > static_cast<size_t>(INT32_MAX) || order.size() > static_cast<size_t>(INT32_MAX) ||
            revision->changes.size() > static_cast<size_t>(INT32_MAX)) return AETHER_ERROR_INVALID_STATE;
        // Reserve the last outward allocation before publishing any state.
        if (!transaction->updates.empty()) {
            transaction->legacy_array = std::make_unique<aether_style_update_t[]>(transaction->updates.size());
            for (size_t i = 0; i < transaction->updates.size(); ++i) transaction->legacy_array[i] = transaction->updates[i].get();
        }
        transaction->revision = revision;
        transaction->output = std::make_unique<aether_style_revision_handle_s>(); transaction->output->value = revision;
        if (!document->identity->alive || document->identity->mutation_epoch != transaction->input_epoch ||
            runtime->generation != transaction->base_generation || runtime->sheet_epoch != transaction->base_sheet_epoch ||
            document->identity->style_tree_epoch != transaction->base_style_tree_epoch)
            return AETHER_ERROR_INVALID_STATE;
        *out_transaction = transaction.release();
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
AETHER_API aether_status_t aether_style_runtime_prepare_revision(
    aether_style_runtime_t runtime, aether_node_t root,
    const aether_style_environment_t* environment, aether_style_transaction_t* out_transaction) {
    return prepare_revision_impl(runtime, root, environment, out_transaction, false);
}
AETHER_API aether_status_t aether_style_runtime_commit_revision(
    aether_style_runtime_t runtime, aether_style_transaction_t transaction, aether_style_revision_t* out_revision) {
    if (out_revision) *out_revision = nullptr;
    if (!runtime || !transaction || !out_revision) return AETHER_ERROR_NULL_ARG;
    if (transaction->owner != runtime || transaction->committed || runtime->preparing ||
        runtime->owner_thread != std::this_thread::get_id() || runtime->generation != transaction->base_generation ||
        runtime->sheet_epoch != transaction->base_sheet_epoch || !transaction->document_lifetime->alive ||
        transaction->document_lifetime->mutation_epoch != transaction->input_epoch ||
        transaction->document_lifetime->style_tree_epoch != transaction->base_style_tree_epoch) return AETHER_ERROR_INVALID_STATE;
    // No callbacks, allocation, comparison, diagnostics or fallible work in this
    // publication boundary. Old generated storage stays owned by retained frames.
    for (auto& link : transaction->links) {
        for (auto& old : link.host->generated_storage) aether_node_mark_attached(old.get(), false);
        link.host->children.swap(link.children); link.host->generated_storage.swap(link.storage);
        for (auto& current : link.host->generated_storage) aether_node_mark_attached(current.get(), true);
    }
    transaction->document_lifetime->style_tree_epoch = transaction->revision->style_tree_epoch;
    runtime->states.swap(transaction->candidate->states);
    runtime->document_measures.swap(transaction->candidate->document_measures);
    runtime->environment = transaction->revision->environment;
    runtime->generation = transaction->revision->generation;
    runtime->revision = transaction->revision;
    transaction->committed = true;
    *out_revision = transaction->output.release();
    // Consumer reentrancy may publish another revision. Outputs above remain
    // those prepared by this operation; never read/write runtime after replay.
    try { transaction->diagnostics->replay(); } catch (...) { /* C callback must not unwind through C ABI. */ }
    return AETHER_OK;
}
AETHER_API void aether_style_transaction_destroy(aether_style_transaction_t transaction) { delete transaction; }
static int32_t resolve_legacy_atomic(aether_style_runtime_t runtime, aether_node_t root,
    const aether_style_environment_t* environment, aether_style_update_t** out_updates, bool isolate_legacy_root) {
    if (out_updates) *out_updates = nullptr;
    if (!runtime || !root || !out_updates) return 0;
    aether_style_transaction_t raw = nullptr;
    if (prepare_revision_impl(runtime, root, environment, &raw, isolate_legacy_root) != AETHER_OK) return 0;
    std::unique_ptr<aether_style_transaction_s> transaction(raw);
    aether_style_revision_t revision = nullptr;
    if (aether_style_runtime_commit_revision(runtime, transaction.get(), &revision) != AETHER_OK) return 0;
    aether_style_revision_destroy(revision);
    const int32_t count = static_cast<int32_t>(transaction->updates.size());
    *out_updates = transaction->legacy_array.release();
    for (auto& update : transaction->updates) update.release();
    return count;
}
AETHER_API aether_status_t aether_style_runtime_retain_revision(aether_style_runtime_t runtime, aether_style_revision_t* out) {
    if (out) *out = nullptr;
    if (!runtime || !out) return AETHER_ERROR_NULL_ARG;
    if (!runtime->revision || !runtime->revision->contextual) return AETHER_ERROR_INVALID_STATE;
    try { auto handle = std::make_unique<aether_style_revision_handle_s>(); handle->value = runtime->revision; *out = handle.release(); return AETHER_OK; }
    catch (...) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
AETHER_API aether_status_t aether_style_revision_clone(aether_style_revision_t revision, aether_style_revision_t* out) {
    if (out) *out = nullptr;
    if (!revision || !out) return AETHER_ERROR_NULL_ARG;
    try { auto handle = std::make_unique<aether_style_revision_handle_s>(); handle->value = revision->value; *out = handle.release(); return AETHER_OK; }
    catch (...) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
AETHER_API void aether_style_revision_destroy(aether_style_revision_t revision) { delete revision; }
AETHER_API uint64_t aether_style_revision_generation(aether_style_revision_t revision) { return revision ? revision->value->generation : 0; }
AETHER_API uint64_t aether_style_revision_document_identity(aether_style_revision_t revision) { return revision ? revision->value->document_identity : 0; }
AETHER_API int32_t aether_style_revision_style_count(aether_style_revision_t revision) { return revision ? static_cast<int32_t>(revision->value->entries.size()) : 0; }
AETHER_API int32_t aether_style_revision_change_count(aether_style_revision_t revision) { return revision ? static_cast<int32_t>(revision->value->changes.size()) : 0; }
static aether_status_t revision_get_entry(aether_style_revision_t revision, const std::vector<style_revision_entry_s>* entries,
    int32_t index, aether_node_t* out_node, aether_computed_style_handle_t* out_style, uint32_t* out_impact) {
    if (out_node) *out_node = nullptr; if (out_style) *out_style = nullptr; if (out_impact) *out_impact = 0;
    if (!revision || !entries) return AETHER_ERROR_NULL_ARG;
    if (index < 0 || static_cast<size_t>(index) >= entries->size()) return AETHER_ERROR_INVALID_STATE;
    const auto& entry = (*entries)[static_cast<size_t>(index)];
    if (out_style && entry.state) {
        auto status = aether_computed_style_handle_clone(const_cast<aether_computed_style_handle_s*>(&entry.state->style), out_style);
        if (status != AETHER_OK) return status;
        (*out_style)->revision = revision->value;
    }
    if (out_node) *out_node = entry.node;
    if (out_impact) *out_impact = entry.impact;
    return AETHER_OK;
}
AETHER_API aether_status_t aether_style_revision_get_style(aether_style_revision_t revision, int32_t index,
    aether_node_t* node, aether_computed_style_handle_t* style, uint32_t* impact) {
    return revision_get_entry(revision, revision ? &revision->value->entries : nullptr, index, node, style, impact);
}
AETHER_API aether_status_t aether_style_revision_get_change(aether_style_revision_t revision, int32_t index,
    aether_node_t* node, aether_computed_style_handle_t* style, uint32_t* impact) {
    return revision_get_entry(revision, revision ? &revision->value->changes : nullptr, index, node, style, impact);
}
static aether_status_t create_revision_map(aether_style_revision_t revision, aether_style_map_t* out_map) {
    if (out_map) *out_map = nullptr;
    if (!revision || !out_map) return AETHER_ERROR_NULL_ARG;
    try {
        auto map = std::make_unique<aether_style_map_s>();
        map->styles.reserve(revision->value->entries.size()); map->measures.reserve(revision->value->entries.size());
        for (const auto& entry : revision->value->entries) {
            auto& copy = map->styles.emplace(entry.node, aether_computed_style_t{}).first->second;
            if (computed_style_deep_copy(entry.state->style.style, &copy) != AETHER_OK) return AETHER_ERROR_OUT_OF_MEMORY;
            map->measures.emplace(entry.node, entry.state->style.measures);
            map->node_lifetimes.emplace(entry.node, entry.state->style.node_lifetime);
        }
        map->revision = revision->value; map->contextual = revision->value->contextual;
        map->document_identity = revision->value->document_identity; map->generation = revision->value->generation;
        map->document_lifetime = revision->value->document_lifetime; map->structure_epoch = revision->value->structure_epoch;
        map->input_epoch = revision->value->input_epoch; map->style_tree_epoch = revision->value->style_tree_epoch;
        *out_map = map.release(); return AETHER_OK;
    } catch (...) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
AETHER_API aether_status_t aether_style_revision_create_map(aether_style_revision_t revision, aether_style_map_t* out_map) {
    if (out_map) *out_map = nullptr;
    if (!revision || !out_map) return AETHER_ERROR_NULL_ARG;
    if (!revision->value->contextual) return AETHER_ERROR_INVALID_STATE;
    return create_revision_map(revision, out_map);
}
AETHER_API aether_status_t aether_style_runtime_create_resolved_style_map(aether_style_runtime_t runtime, aether_doc_t doc, aether_style_map_t* out_map) {
    if (out_map) *out_map = nullptr;
    if (!runtime || !doc || !out_map) return AETHER_ERROR_NULL_ARG;
    if (!runtime->revision || runtime->revision->document_identity != aether_node_identity(doc)) return AETHER_ERROR_INVALID_STATE;
    aether_style_revision_handle_s handle{runtime->revision};
    const auto status = create_revision_map(&handle, out_map);
    if (status == AETHER_OK) (*out_map)->contextual = false; // Historical API: explicit legacy adapter.
    return status;
}
AETHER_API aether_status_t aether_style_runtime_update_resolved_style_map(aether_style_runtime_t runtime,
    aether_style_map_t map, const aether_node_t* nodes, int32_t node_count) {
    if (!runtime || !map || (!nodes && node_count > 0)) return AETHER_ERROR_NULL_ARG;
    if (node_count < 0 || map->contextual) return AETHER_ERROR_INVALID_STATE;
    try {
        auto staged = aether_style_map_snapshot(map);
        for (int32_t index = 0; index < node_count; ++index) {
            auto* node = nodes[index]; if (!node) continue;
            const auto state = runtime->states.find(node);
            if (state == runtime->states.end()) {
                auto existing = staged->styles.find(node);
                if (existing != staged->styles.end()) { aether_computed_style_free(&existing->second); staged->styles.erase(existing); }
                staged->measures.erase(node); staged->node_lifetimes.erase(node); continue;
            }
            // Use an owning temporary so a failed insertion cannot leak buffers.
            auto holder = std::make_unique<runtime_node_state_s>();
            if (computed_style_deep_copy(state->second->style.style, &holder->style.style) != AETHER_OK) return AETHER_ERROR_OUT_OF_MEMORY;
            auto& destination = staged->styles.try_emplace(node, aether_computed_style_t{}).first->second;
            aether_computed_style_free(&destination); destination = holder->style.style; holder->style.style = {};
            staged->measures[node] = state->second->style.measures;
            staged->node_lifetimes[node] = state->second->style.node_lifetime;
        }
        // Partial projection never adopts a global revision identity. Layout
        // already retained the previous complete backing independently.
        staged->revision.reset(); staged->generation = 0; staged->contextual = false;
        map->swap(*staged); return AETHER_OK;
    } catch (...) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
AETHER_API void aether_style_runtime_clear_cache(aether_style_runtime_t runtime) { if (runtime) { runtime->states.clear(); ++runtime->sheet_epoch; } }
