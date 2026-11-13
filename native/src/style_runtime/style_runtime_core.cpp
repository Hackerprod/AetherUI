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
#include "selector_match_context.h"
#include <cassert>
#include "style_source_limits.h"
#include "style_diff.h"
#include "css_variable_tokens.h"
#include "custom_property_scope_internal.h"
#include "typography_scope_probe.h"
#include "typography_context.h"
#include "frozen_runtime_metadata.h"
#include "value_audit_internal.h"
#include "paused_keyframes.h"
#include "../css/style_revision.h"
#include "../css/frozen_measure_read.h"
#include "../css/style_map_version.h"
using namespace aether::style;

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>


// Private preparation data, never a public handle or final record.
struct PreparedNodeResult {
    TypographyEvidence typography;
    aether::rendering::text::ParagraphDiagnosticRef text_diagnostic;
    FrozenStyleRef style;
    std::shared_ptr<measure_state_s> measures;
    FrozenScopeRef custom_properties;
    // Transaction-local computed terminals; never exported or retained by snapshots.
    FrozenScopeRef backdrop_custom_properties;
    FrozenColorRef color_references;
    uint32_t declared_impact=0, declared_inherited_impact=0, inherited_change_impact=0, impact=0;
};
struct MutableStyleBuilder {
    aether_computed_style_t value{};
    MutableStyleBuilder() = default;
    MutableStyleBuilder(const MutableStyleBuilder&)=delete;
    MutableStyleBuilder& operator=(const MutableStyleBuilder&)=delete;
    MutableStyleBuilder(MutableStyleBuilder&& other) noexcept : value(other.value) { other.value={}; }
    MutableStyleBuilder& operator=(MutableStyleBuilder&& other) noexcept {
        if(this!=&other){aether_computed_style_free(&value);value=other.value;other.value={};}
        return *this;
    }
    ~MutableStyleBuilder(){aether_computed_style_free(&value);}
};
static void require_style_status(aether_status_t status) {
    if(status==AETHER_ERROR_OUT_OF_MEMORY)throw std::bad_alloc();
    if(status!=AETHER_OK)throw std::invalid_argument("invalid frozen style preparation");
}
static std::vector<bool> copy_text_color_flags(const FrozenColorMetadata& source) {
    std::vector<bool> out; out.reserve(source.text_shadow_count());
    for(size_t i=0;i<source.text_shadow_count();++i)out.push_back(source.text_shadow_at(i));
    return out;
}
struct aether_style_update_s {
    aether_node_s* node = nullptr;
    // An update crosses the runtime/binding boundary after the style tree may
    // have been rebuilt.  It therefore owns a deep snapshot rather than
    // borrowing storage from PreparedNodeResult.
    aether_computed_style_handle_s current{};
    uint32_t impact = STYLE_IMPACT_NONE;
    ~aether_style_update_s() { aether_computed_style_free(&current.style); }
};

struct aether_style_runtime_s {
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{};
    aether_style_diagnostic_fn diag_cb = nullptr;
    void* diag_user_data = nullptr;
    const style_value_audit_s* value_audit = nullptr; // borrowed synchronous observer
    int64_t next_sheet_handle = 1;
    int32_t next_rule_id = 1;
    std::unordered_map<int64_t, std::unique_ptr<compiled_style_sheet_s>> sheets;
    std::unordered_map<aether_node_s*, FrozenNodeRef> states;
    std::unordered_map<aether_node_s*, std::unique_ptr<PreparedNodeResult>> prepared;
    std::shared_ptr<document_measure_context_s> document_measures;
    std::shared_ptr<const style_revision_s> revision;
    uint64_t generation = 0;
    uint64_t sheet_epoch = 0;
    bool preparing = false;
    style_source_budget_s source_budget;
    style_source_accounting_s source_accounting; // transaction-local document aggregate
    aether_node_s* legacy_inheritance_root = nullptr; // Candidate-only compatibility boundary.
    std::thread::id owner_thread = std::this_thread::get_id();
    rule_index_s index;
    // Borrowed only by a synchronous preparation transaction, like index.
    // Final snapshots retain sampled scalar/measure values, never these pointers.
    std::vector<const compiled_rule_s*> paused_rules;
    void copy_index_for_preparation(const aether_style_runtime_s& source) {
        index = source.index;
        for (const auto& sheet : source.sheets) source_accounting.merge(sheet.second->accounting);
        source_budget.source_bytes=source_accounting.source_bytes;
        source_budget.declarations=source_accounting.declarations;
        source_budget.rules=source_accounting.rules;
        source_budget.compiled_rules=source_accounting.compiled_rules;
        source_budget.expanded_declarations=source_accounting.expanded_declarations;
        source_budget.compiled_bytes=source_accounting.compiled_bytes;
        for(const auto& sheet:source.sheets)for(const auto& rule:sheet.second->rules)
            if(rule.keyframes)paused_rules.push_back(&rule);
        // Unlike a generic copy, synchronous preparation borrows only stable,
        // runtime-owned sheets. Public diagnostics are deferred while preparing;
        // the optional internal audit is read-only. Mutations are
        // guarded, and sheet_epoch rejects stale commits before rule use.
        index.before_possible_ = source.index.before_possible_;
        index.after_possible_ = source.index.after_possible_;
        index.pseudo_capabilities_complete_ = source.index.pseudo_capabilities_complete_;
        index.disjoint_runtime_rules_ = source.index.disjoint_runtime_rules_;
    }
    static rule_index_s prepare_rule_index(const aether_style_runtime_s* runtime,
        const compiled_style_sheet_s* extra = nullptr, int64_t omit = 0);
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

rule_index_s aether_style_runtime_s::prepare_rule_index(const aether_style_runtime_s* runtime,
    const compiled_style_sheet_s* extra, int64_t omit) {
    std::vector<const compiled_style_sheet_s*> sheets;
    sheets.reserve(runtime->sheets.size() + (extra ? 1 : 0));
    for (const auto& pair : runtime->sheets) if (pair.first != omit) sheets.push_back(pair.second.get());
    if (extra) sheets.push_back(extra);
    rule_index_s prepared;
    rule_index_rebuild(&prepared, sheets);
    // No match/media/state/content filtering: even a content-free pseudo rule
    // can produce diagnostics. Unknown future kinds force the full path.
    bool complete = true;
    bool unique_ids = true;
    std::unordered_set<int32_t> rule_ids;
    for (const auto* sheet : sheets) for (const auto& rule : sheet->rules) {
        if (!rule_ids.insert(rule.rule_id).second) unique_ids = false;
        switch (rule.selector.pseudo_element) {
        case aether_pseudo_element_t::none: break;
        // Syntax is retained for applicability auditing. There is deliberately
        // no generated node or host-style application for first-letter.
        case aether_pseudo_element_t::first_letter: break;
        case aether_pseudo_element_t::before: prepared.before_possible_ = true; break;
        case aether_pseudo_element_t::after: prepared.after_possible_ = true; break;
        default: complete = false; break;
        }
    }
    // Rebuild inserts every record once into one private bucket. Ownership and
    // rule-vector stability are guaranteed here, not by generic rebuild callers.
    // Publish certificates only after all fallible validation has completed.
    prepared.pseudo_capabilities_complete_ = complete;
    prepared.disjoint_runtime_rules_ = unique_ids;
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
    if (references.background_uses_current_color)
        style.background_color = style.color;
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

static PreparedNodeResult* compute_node(aether_style_runtime_s* runtime, aether_node_s* node,
                                          PreparedNodeResult* parent, uint32_t /* inherited_parent_impact */,
                                          uint32_t& impact, uint32_t& inherited_change_impact,
                                          selector_match_context_s& selector_context,
                                          std::optional<std::string>* generated_content = nullptr) {
    // The legacy subtree entry point starts an isolated inheritance scope.
    // Contextual preparation never installs this compatibility boundary.
    if (node == runtime->legacy_inheritance_root) parent = nullptr;
    auto* registry = style_property_registry_get_global();
    const auto parent_view=parent ? std::optional<ComputedStyleReadView>(parent->style->view()) : std::nullopt;
    MutableStyleBuilder next_builder;
    auto& next=next_builder.value;
    if (computed_style_inherit_checked(parent_view ? &*parent_view : nullptr, &next) != AETHER_OK)
        throw std::bad_alloc();
    auto measures = std::make_shared<measure_state_s>();
    measures->document = runtime->document_measures;
    measures->parent_font_size = parent ? parent->style->view().scalars().font_size : measures->document->initial_font_size;
    measures->self_font_size = measures->parent_font_size;
    next.font_size = measures->self_font_size;
    if (parent) measure_set_property_dependencies(*measures, "font-size", MEASURE_PARENT);
    if (parent && parent->measures) {
        measures->line_height = parent->measures->line_height;
        measures->direction_rtl = parent->measures->direction_rtl;
        measures->writing_mode = parent->measures->writing_mode;
        measure_set_property_dependencies(*measures, "line-height", MEASURE_PARENT);
    }
    const bool document_element = !node->is_generated && node->type == AETHER_NODE_ELEMENT &&
        ((!node->parent) || (node->parent->type == AETHER_NODE_DOCUMENT &&
          aether_doc_get_document_element(static_cast<aether_doc_s*>(node->parent)) == node));
    TypographyEvidence typography = parent ? parent->typography : TypographyEvidence{};
    if (parent) for (auto& origin:typography.provenance) origin.inherited=true;
    bool typography_discarded = false;
    auto typography_set = [&](const std::string& name, const std::string& source) {
        if (name == "font-family") typography_parse_family(source, typography);
        else if (name == "font-style") typography_parse_style(source, typography);
        else if (name == "letter-spacing") typography_parse_spacing(source, typography);
        else if (name == "font-size") { typography.size_state=source.size()<=65536?TypographyFieldState::known:TypographyFieldState::unsupported; typography.provenance[3].resolved_source=source.size()<=65536?source:std::string{}; }
        else if (name == "font-weight") { typography.weight_state=source.size()<=65536?TypographyFieldState::known:TypographyFieldState::unsupported; typography.provenance[4].resolved_source=source.size()<=65536?source:std::string{}; }
        else if (name == "line-height") { typography.line_height_state=source.size()<=65536?TypographyFieldState::known:TypographyFieldState::unsupported; typography.provenance[5].resolved_source=source.size()<=65536?source:std::string{}; }
    };
    auto typography_reset = [&](const std::string& name) {
        if (name == "font-family") { typography.family_state=TypographyFieldState::unknown; typography.families.clear(); }
        else if (name == "font-style") { typography.style_state=TypographyFieldState::unknown; typography.font_style.clear(); }
        else if (name == "letter-spacing") { typography.spacing_state=TypographyFieldState::unknown; typography.spacing_normal=false; typography.spacing_px=0; }
        else if (name == "font-size") { typography.size_state=TypographyFieldState::unknown; typography.size_px=0; typography.size_dependencies=0; typography.provenance[3].resolved_source.clear(); }
        else if (name == "font-weight") { typography.weight_state=TypographyFieldState::unknown; typography.weight=0; typography.provenance[4].resolved_source.clear(); }
        else if (name == "line-height") { typography.line_height_state=TypographyFieldState::unknown; typography.line_height_value=0; typography.line_height_used_px=0; typography.line_height_dependencies=0; typography.provenance[5].resolved_source.clear(); }
    };
    std::vector<cascade_entry_s> cascade(static_cast<size_t>(registry->property_count()));
    struct custom_winner_s {
        cascade_priority_t priority{};
        // Compiled rules and the inline vector own these strings throughout preparation.
        const custom_property_declaration_s* declaration = nullptr;
        void offer(const cascade_priority_t& p, const custom_property_declaration_s& d) {
            if (!declaration || cascade_priority_compare(&p, &priority) >= 0) {
                priority = p;
                declaration = &d;
            }
        }
    };
    std::unordered_map<std::string, custom_winner_s> custom_winners;
    cascade_entry_custom_s content_winner;

    std::vector<const compiled_rule_s*> candidates;
    auto info = get_node_info(node);
    rule_index_get_candidates(&runtime->index, &info, candidates);
    for (const auto* rule : candidates) {
        if (rule->selector.pseudo_element != node->pseudo_element) continue;
        if (!media_condition_matches(rule->condition, &runtime->environment) ||
            !selector_match(rule->selector, node, selector_context)) continue;
        typography_discarded = typography_discarded || rule->typography_discarded;
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
            custom_winners[declaration.name].offer(priority, declaration);
        }
        for (const auto& declaration : rule->content_declarations) {
            cascade_priority_t priority{
                declaration.important, false, rule->layer, rule->selector.specificity,
                rule->source_order, declaration.declaration_order
            };
            content_winner.offer(priority, declaration.source);
        }
    }

    bool typography_discarded_inline=false;
    std::vector<compiled_property_declaration_s> inline_properties;
    std::vector<custom_property_declaration_s> inline_custom;
    std::vector<compiled_shorthand_declaration_s> inline_shorthands;
    // Generated pseudo-elements retain host attributes for selector matching, but
    // the host's inline declaration block does not cascade onto ::before/::after.
    if (!node->is_generated) if (auto it = node->attributes.find("style"); it != node->attributes.end()) {
        style_source_accounting_s inline_accounting;
        const auto inline_status=style_source_compile_inline(it->second.c_str(), inline_properties, inline_custom, inline_shorthands,
                                    runtime->diag_cb, runtime->diag_user_data, &typography_discarded_inline, &inline_accounting, &runtime->source_budget);
        require_style_status(inline_status);
        runtime->source_accounting.merge(inline_accounting);
        typography_discarded = typography_discarded || typography_discarded_inline;
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
            custom_winners[declaration.name].offer(priority, declaration);
        }
    }

    custom_property_sources_s sources;
    bool backdrop_sources_differ = parent && parent->backdrop_custom_properties != parent->custom_properties;
    for (const auto& pair : custom_winners) {
        const auto& declaration = *pair.second.declaration;
        auto value = str_trim(declaration.source);
        if (str_iequals(value, "inherit") || str_iequals(value, "unset")) continue;
        const bool initial = str_iequals(value, "initial") || str_iequals(value, "revert");
        sources.emplace(pair.first, custom_property_source_s{declaration.source, initial});
        if (!initial && declaration.original_source) backdrop_sources_differ = true;
    }

    auto scope = compute_frozen_custom_property_scope(sources, parent ? parent->custom_properties : nullptr,
        runtime->options.max_variable_depth, runtime->options.max_variable_expansion_length,
        runtime->value_audit?runtime->value_audit->work:nullptr);
    if(runtime->value_audit && runtime->value_audit->scope)
        runtime->value_audit->scope(runtime->value_audit->context,scope.get());
    // Ordinary nodes share the exact legacy scope without a second source map or
    // resolution. Only effective winning differences (or inherited differences)
    // need a focal view. CSS-wide classification always comes from the legacy winner.
    auto backdrop_scope = scope;
    if (backdrop_sources_differ) {
        custom_property_sources_s backdrop_sources;
        for (const auto& pair : sources) {
            const auto& declaration = *custom_winners.at(pair.first).declaration;
            backdrop_sources.emplace(pair.first, custom_property_source_s{
                !pair.second.initial && declaration.original_source
                    ? *declaration.original_source : pair.second.source, pair.second.initial});
        }
        backdrop_scope = compute_frozen_custom_property_scope(backdrop_sources,
            parent ? parent->backdrop_custom_properties : nullptr,
            runtime->options.max_variable_depth, runtime->options.max_variable_expansion_length,
        runtime->value_audit?runtime->value_audit->work:nullptr);
    }

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
        const bool substituted = resolve_frozen_computed_value(declaration.source.c_str(), scope.get(),
            runtime->options.max_variable_depth, runtime->options.max_variable_expansion_length, resolved,
            runtime->value_audit?runtime->value_audit->work:nullptr);
        if(runtime->value_audit && runtime->value_audit->value)
            runtime->value_audit->value(runtime->value_audit->context,node,declaration.name,declaration.source,substituted?&resolved:nullptr);
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
            !selector_match(rule->selector, node, selector_context)) continue;
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
                !resolve_frozen_computed_value(content_source.c_str(), scope.get(),
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
        // Background, borders and box-shadow are not inherited, so their symbolic state
        // starts from the CSS initial values for this node.
        color_references.text_shadow_uses_current_color =
            copy_text_color_flags(*parent->color_references);
    }
    // T19_SCOPE_BEGIN: benchmark replaces only this region for the eager baseline.
    auto* scope_counters = typography_scope_counters;
    if (scope_counters) ++scope_counters->node_computations;
    std::optional<bool> typography_scope_result;
    auto typography_scope_uncertain = [&]() {
        if (typography_scope_result) return *typography_scope_result;
        if (scope_counters) ++scope_counters->queries;
        for (auto* current=scope.get(); current; current=current->parent()) {
            if (scope_counters) ++scope_counters->scopes;
            for (const auto& terminal:current->local_entries()) {
                if (scope_counters) ++scope_counters->terminals;
                if (terminal.status()!=custom_property_status::valid) {
                    if (scope_counters) ++scope_counters->uncertain_exits;
                    typography_scope_result=true;
                    return true;
                }
            }
        }
        typography_scope_result=false;
        return false;
    };
    // T19_SCOPE_END
    auto apply_definition = [&](size_t i) {
        if (!cascade[i].is_set) return false;
        const auto& definition = registry->definitions[i];
        const auto& compiled = cascade[i].value;
        const int typography_index=definition.name=="font-family"?0:definition.name=="font-style"?1:definition.name=="letter-spacing"?2:definition.name=="font-size"?3:definition.name=="font-weight"?4:definition.name=="line-height"?5:-1;
        if (typography_index>=0) {
            auto& origin=typography.provenance[static_cast<size_t>(typography_index)];
            origin.declaration=true;
            origin.inherited=parent && (compiled.mode==compiled_value_mode::inherit ||
                (compiled.mode==compiled_value_mode::unset && definition.inherited));
            origin.important=cascade[i].priority.important; origin.inline_style=cascade[i].priority.inline_;
            origin.layer=static_cast<int32_t>(cascade[i].priority.layer);
            origin.source_order=cascade[i].priority.source_order; origin.declaration_order=cascade[i].priority.decl_order;
            origin.accepted_source=compiled.typography_source.size()<=65536?compiled.typography_source:std::string{};
        }
        if (typography_index>=0 && compiled.mode==compiled_value_mode::deferred && typography_scope_uncertain()) {
            typography_reset(definition.name); typography.coverage_gaps |= 2;
        }
        const std::string initial_value = definition.name == "font-size" ?
            std::to_string(measures->document->initial_font_size) + "px" : definition.initial_value;
        std::string value;
        bool explicit_inherit = false;
        switch (compiled.mode) {
        case compiled_value_mode::value:
            typography_set(definition.name, compiled.typography_source);
            value = compiled.typed_value;
            break;
        case compiled_value_mode::initial:
            typography_reset(definition.name);
            value = initial_value;
            break;
        case compiled_value_mode::inherit:
            if (!parent) typography_reset(definition.name);
            if (!definition.inherit_explicit && !definition.inherit_context) return false;
            explicit_inherit = parent != nullptr;
            value = initial_value;
            break;
        case compiled_value_mode::unset:
            if (definition.inherited && parent) return false;
            typography_reset(definition.name);
            value = initial_value;
            break;
        case compiled_value_mode::deferred: {
            const bool substituted = resolve_frozen_computed_value(compiled.deferred_source.c_str(),
                                  definition.name == "backdrop-filter" ? backdrop_scope.get() : scope.get(),
                                  runtime->options.max_variable_depth,
                                  runtime->options.max_variable_expansion_length, value,
                                  runtime->value_audit?runtime->value_audit->work:nullptr);
            if(runtime->value_audit && runtime->value_audit->value)
                runtime->value_audit->value(runtime->value_audit->context,node,definition.name,compiled.deferred_source,substituted?&value:nullptr);
            if (!substituted) {
                if (typography_index>=0) {
                    // Missing/cyclic and expansion-budget failures are not
                    // distinguished here. Never convert uncertainty to known.
                    typography_reset(definition.name);
                    typography.provenance[static_cast<size_t>(typography_index)].inherited=false;
                    typography.coverage_gaps |= 2;
                }
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
                if (typography_index>=0 && parent && (css_wide==property_css_wide_e::inherit || css_wide==property_css_wide_e::unset))
                    typography.provenance[static_cast<size_t>(typography_index)].inherited=true;
                if (css_wide == property_css_wide_e::initial || !parent) typography_reset(definition.name);
                if (css_wide == property_css_wide_e::unset && definition.inherited && parent) return false;
                explicit_inherit = css_wide == property_css_wide_e::inherit &&
                    parent && (definition.inherit_explicit || definition.inherit_context);
                value = initial_value;
                break;
            }
            const std::string typography_resolved_source=value;
            std::string typography_keyword;
            if (css_single_identifier(value,typography_keyword)) typography_keyword=typography_ascii_lower(typography_keyword);
            const bool typography_initial_wide=typography_keyword=="initial" || typography_keyword=="revert" || typography_keyword=="revert-layer";
            if (typography_index>=0) {
                if (typography_initial_wide) typography_reset(definition.name);
                else if (typography_keyword=="inherit" || typography_keyword=="unset") {
                    typography.provenance[static_cast<size_t>(typography_index)].inherited=parent!=nullptr;
                    if (!parent) typography_reset(definition.name);
                }
            }
            if (!definition.parse(value.c_str(), value)) {
                if (typography_index>=0 && !typography_css_wide(typography_keyword)) {
                    // Legacy parse rejection is not proof that a valid but
                    // unimplemented computed value is the parent's normal/zero.
                    typography_reset(definition.name);
                    typography.provenance[static_cast<size_t>(typography_index)].inherited=false;
                    typography.coverage_gaps |= 2;
                }
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
            } else {
                if (typography_index>=0 && !typography_scope_uncertain() && !typography_css_wide(typography_keyword)) typography_set(definition.name, typography_resolved_source);
            }
            break;
        }
        }
        auto apply_selected = [&](aether_computed_style_t& style,
                                  color_resolution_state_s& references, bool emit_diagnostics,
                                  measure_apply_state_s& destination) {
            if (explicit_inherit) {
                if (definition.inherit_context) {
                    const ColorMetadataReadView colors(*parent->color_references);
                    const MeasuresReadView parent_measures(*parent->measures);
                    property_apply_context_s context{&style, &references, &destination, &colors};
                    definition.inherit_context(context, parent->style->view(), &parent_measures);
                } else definition.inherit_explicit(&style, parent->style->view());
                return true;
            }
            return apply_property_value(definition, style, references, value, emit_diagnostics, destination);
        };
        measure_apply_state_s selected_measures(*measures, definition.name);
        if (!apply_selected(next, color_references, true, selected_measures)) {
            if (typography_index>=0) { typography_reset(definition.name); typography.coverage_gaps |= 2; }
            return false;
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
    // Deterministic local-time-zero sample, after normal cascade and before
    // freezing/diff/layout. Each resolution starts from an unanimated builder.
    const auto animation=paused_animation_read(cascade,registry);
    const compiled_rule_s* animation_rule=nullptr;
    if(animation.name!="none") {
        for(const auto* rule_ptr:runtime->paused_rules) {
            const auto& rule=*rule_ptr;
            if(!rule.keyframes||rule.keyframes->name!=animation.name||
               !media_condition_matches(rule.condition,&runtime->environment))continue;
            if(!animation_rule||rule.layer>animation_rule->layer||
               (rule.layer==animation_rule->layer&&rule.source_order>animation_rule->source_order))animation_rule=&rule;
        }
    }
    if(!animation.profile_supported || (animation.name!="none"&&(!animation_rule||!animation_rule->keyframes->profile_supported))) {
        report(runtime,"AETHER-CSS-ANIMATION-PROFILE","animation",animation.name.c_str(),
               "Animation is outside the deterministic paused capture profile.");
    } else if(animation.active && animation_rule) {
        for(const auto& endpoint:animation_rule->keyframes->values) {
            const auto* definition=registry->try_get(endpoint.property.c_str());
            if(!definition)throw std::logic_error("Missing registered animation target");
            const auto& winner=cascade[static_cast<size_t>(definition->id)];
            if(winner.is_set&&winner.priority.important)continue;
            double sampled=endpoint.from+(endpoint.to-endpoint.from)*animation.progress;
            if(endpoint.property=="order")sampled=std::floor(sampled+0.5);
            if(endpoint.property=="flex-direction")sampled=animation.progress<0.5?endpoint.from:endpoint.to;
            std::string value=paused_animation::number_text(sampled);
            if(endpoint.property=="flex-basis"||endpoint.property=="row-gap"||endpoint.property=="column-gap")value+="px";
            measure_apply_state_s destination(*measures,endpoint.property);
            if(!apply_property_value(*definition,next,color_references,value,true,destination))
                throw std::invalid_argument("Invalid sampled animation property");
        }
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
        }
    }

    // Read successful contextual outputs here, never reconstruct specified units
    // from lossy scalars. Inherited unitless line height keeps its kind and is
    // evaluated with this node's final font-size, unlike inherited absolute px.
    if (typography.size_state==TypographyFieldState::known) {
        typography.size_px=next.font_size;
        typography.size_dependencies=measure_property_dependencies(*measures,"font-size");
        // The existing resolver treats these keywords as parent-relative, but
        // its unit-token dependency scanner cannot see keyword dependencies.
        const auto size_source=typography_ascii_lower(typography.provenance[3].resolved_source);
        if (size_source=="smaller" || size_source=="larger") typography.size_dependencies |= MEASURE_PARENT;
    }
    if (typography.weight_state==TypographyFieldState::known) typography.weight=next.font_weight;
    if (typography.line_height_state==TypographyFieldState::known) {
        typography.line_height_kind=static_cast<TypographyLineHeightKind>(measures->line_height.kind);
        typography.line_height_value=measures->line_height.value;
        typography.line_height_used_px=measure_used_line_height(measures.get(),next);
        typography.line_height_dependencies=measure_property_dependencies(*measures,"line-height");
        if (measures->line_height.kind!=measure_line_height_kind_e::absolute_css_px)
            typography.line_height_dependencies |= MEASURE_SELF_FONT;
    }
    measure_recompute_dependencies(*measures);
    const bool is_new_node = found == runtime->states.end();
    // One final semantic comparison per existing node. Neither historical
    // declaration masks nor the parent's delta determine this node's work.
    StyleDiff diff;
    if (is_new_node) {
        diff.own_impact = STYLE_IMPACT_ALL_BITS;
        diff.inherited_value_impact = STYLE_IMPACT_ALL_BITS;
        diff.style_storage_equivalent = false;
    } else {
        const auto& previous = *found->second;
        diff = classify_style_diff(previous.style_view(), *previous.measures(),
            *previous.color_references(), next, *measures, color_references);
    }
    // A no-op legacy callback must not hide a semantic evidence change.
    if (typography_discarded) { typography=TypographyEvidence{}; typography.coverage_gaps |= 2; }
    if (!is_new_node && (!found->second->typography() ||
        found->second->typography()->evidence != typography)) {
        diff.own_impact |= STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT;
        diff.inherited_value_impact |= STYLE_IMPACT_TEXT_LAYOUT | STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT;
    }
    impact = diff.own_impact;
    inherited_change_impact = diff.inherited_value_impact;
    auto state = std::make_unique<PreparedNodeResult>();
    if (diff.style_storage_equivalent) state->style = found->second->style();
    else require_style_status(freeze_style(next, state->style));
    state->typography=std::move(typography);
    state->measures = measures;
    state->custom_properties = std::move(scope);
    state->backdrop_custom_properties = std::move(backdrop_scope);
    // Always refresh symbolic metadata, even when identical RGBA reused the style.
    // prepare_tree visits descendants regardless of parent impact: explicit background
    // inherit must observe concrete <-> currentColor changes with the same parent RGBA.
    require_style_status(freeze_color_metadata(color_references,state->color_references));
    state->declared_impact = diff.own_impact;
    state->declared_inherited_impact = diff.inherited_value_impact;
    state->inherited_change_impact = inherited_change_impact;
    auto* result = state.get();
    state->impact=impact;
    runtime->prepared[node] = std::move(state);
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
    aether_style_runtime_s* staged, aether_node_s* host, PreparedNodeResult* host_state,
    aether_pseudo_element_t pseudo, selector_match_context_s& selector_context) {
    if (!staged->index.may_target_pseudo(pseudo)) return {};
    auto generated = create_generated_node();
    generated->type = AETHER_NODE_ELEMENT; generated->parent = host;
    generated->tag_name = host->tag_name; generated->attributes = host->attributes;
    generated->state = host->state; generated->is_generated = true;
    generated->snapshot_owned = true; generated->identity->attached = false;
    generated->pseudo_element = pseudo; generated->style_origin = host;
    uint32_t impact = 0, inherited = 0;
    std::optional<std::string> content;
    compute_node(staged, generated.get(), host_state,
        host_state ? host_state->inherited_change_impact : 0, impact, inherited, selector_context, &content);
    if (!content) { staged->prepared.erase(generated.get()); return {}; }
    if (!content->empty()) {
        auto text = std::make_unique<aether_node_s>(); text->type = AETHER_NODE_TEXT;
        text->parent = generated.get(); text->text = std::move(*content);
        generated->children.push_back(text.get()); text.release();
    }
    return generated;
}
static void prepare_tree(aether_style_runtime_s* staged, aether_node_s* node,
    PreparedNodeResult* parent, uint32_t inherited_parent,
    std::vector<prepared_children_s>& links, std::vector<aether_node_s*>& order,
    selector_match_context_s& selector_context) {
    if (!node || node->is_generated) return;
    auto* next_parent = parent;
    uint32_t next_inherited = inherited_parent;
    if (node->type == AETHER_NODE_ELEMENT) {
        // DOM sources are not correlated with add_stylesheet handles. Never infer
        // that a link/style was fetched/compiled from an unrelated sheet epoch.
        if (node->tag_name=="link" || node->tag_name=="style") {
            staged->source_accounting.external_pending=true;
            staged->source_accounting.incomplete(style_source_loss::external_source);
        }
        uint32_t impact = 0;
        next_parent = compute_node(staged, node, parent, inherited_parent, impact, next_inherited, selector_context);
        order.push_back(node);
        prepared_children_s link; link.host = node;
        link.children.reserve(node->children.size() + 2); link.storage.reserve(2);
        auto before = prepare_generated_pseudo(staged, node, next_parent, aether_pseudo_element_t::before, selector_context);
        auto after = prepare_generated_pseudo(staged, node, next_parent, aether_pseudo_element_t::after, selector_context);
        if (before) { link.children.push_back(before.get()); order.push_back(before.get()); link.storage.push_back(std::move(before)); }
        for (auto* child : node->children) if (!child->is_generated) link.children.push_back(child);
        if (after) { link.children.push_back(after.get()); order.push_back(after.get()); link.storage.push_back(std::move(after)); }
        links.push_back(std::move(link));
    }
    for (auto* child : node->children)
        if (!child->is_generated) prepare_tree(staged, child, next_parent, next_inherited, links, order, selector_context);
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
        // The new sheet shares the document source budget with all live sheets.
        // No source copying/parsing happens before the compiler reserves it.
        style_source_accounting_s existing;
        for (const auto& entry : runtime->sheets) existing.merge(entry.second->accounting);
        style_source_budget_s budget;
        budget.source_bytes=existing.source_bytes;
        budget.declarations=existing.declarations;
        budget.rules=existing.rules;
        budget.compiled_rules=existing.compiled_rules;
        budget.expanded_declarations=existing.expanded_declarations;
        budget.compiled_bytes=existing.compiled_bytes;
        const auto result = style_source_compile(source, handle, layer, sheet->rules, next_rule_id,
                                                collector, &diagnostics, &sheet->accounting, &budget);
        if (result != AETHER_OK) return result;
        // Preserve the compiler's global positions, including the shared position
        // of selectors in one list. ID capacity already bounds this narrowing.
        const auto rule_count = static_cast<int32_t>(sheet->rules.size());
        auto index = aether_style_runtime_s::prepare_rule_index(runtime, sheet.get());
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
        auto index = aether_style_runtime_s::prepare_rule_index(runtime, nullptr, sheet_handle);
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
        // This is an owning internal clone, not a flat ABI projection.
        const auto status = aether_computed_style_handle_clone(&update->current, out_current);
        if (status != AETHER_OK) return status;
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
    StyleRevisionRef revision;
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
        staged.value_audit = runtime->value_audit;
        staged.options = runtime->options; staged.environment = requested_environment;
        if (isolate_legacy_root && root != document && root->parent && root->parent->type != AETHER_NODE_DOCUMENT)
            staged.legacy_inheritance_root = root;
        staged.copy_index_for_preparation(*runtime);
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
            const auto& token = entry.second->node_lifetime();
            if (!token || (token->alive && token->attached))
                staged.states.emplace(entry.first, entry.second);
        }
        std::vector<aether_node_s*> order;
        {
            // No external callbacks or DOM publication occur during traversal.
            // Diagnostics are buffered; generated child links are staged. Destroy
            // selector scratch before freezing records, commit, or callbacks.
#ifndef NDEBUG
            const auto* identity = document->identity.get();
            const auto structure_epoch = identity->structure_epoch;
#endif
            // The ancestor-ID experiment is not accepted: normal preparation
            // retains the original matcher path. Only private tests opt in.
            selector_match_context_s selector_context;
            prepare_tree(&staged, document, nullptr, 0, transaction->links, order, selector_context);
            assert(document->identity.get() == identity && identity->structure_epoch == structure_epoch);
        }
        std::unordered_set<aether_node_s*> current_nodes(order.begin(), order.end());
        // Freeze document once, only after root-font bootstrap and complete traversal.
        StyleRevisionInput revision_input;
        revision_input.contextual=staged.legacy_inheritance_root==nullptr;
        const auto& d=*staged.document_measures;
        require_style_status(freeze_document({d.viewport_width,d.viewport_height,d.initial_font_size,d.root_font_size,d.generation,d.identity},revision_input.document));
        revision_input.sheet_epoch=transaction->base_sheet_epoch;
        revision_input.generation=d.generation; revision_input.document_identity=d.identity;
        revision_input.input_epoch=transaction->input_epoch;
        revision_input.structure_epoch=document->identity->structure_epoch;
        revision_input.style_tree_epoch=document->identity->style_tree_epoch+1;
        revision_input.document_lifetime=document->identity; revision_input.environment=requested_environment;
        std::unordered_map<aether_node_s*,std::shared_ptr<aether_node_s>> leaves;
        for(const auto& link:transaction->links)for(const auto& leaf:link.storage)leaves.emplace(leaf.get(),leaf);
        BackingBuildInput backing;
        std::unordered_map<aether_node_s*,FrozenNodeRef> next_states;
        // Raw direct text only: no CSS normalization, grouping, font or geometry.
        // One document budget, compact range per text node, checked before copies.
        using namespace aether::rendering::text;
        ParagraphDiagnosticBudget text_budget;
        bool text_limited=false;
        const ParagraphDiagnosticEpochs text_epochs{revision_input.document_identity,
            revision_input.generation, revision_input.input_epoch, revision_input.structure_epoch,
            revision_input.style_tree_epoch, revision_input.sheet_epoch};
        for (auto* node : order) {
            if (text_limited) break;
            std::vector<ParagraphDiagnosticPiece> pieces;
            for (auto* child : node->children) if (!child->is_generated && child->type==AETHER_NODE_TEXT) {
                if (pieces.size()>=text_budget.limits.ranges-text_budget.ranges) {text_limited=true;break;}
                pieces.push_back({aether_node_identity(child),child->text});
            }
            if (text_limited) break;
            if (!pieces.empty() && freeze_paragraph_diagnostic(pieces,text_epochs,text_budget,
                staged.prepared.at(node)->text_diagnostic)!=ParagraphDiagnosticStatus::prepared) text_limited=true;
        }
        if (text_limited) staged.source_accounting.incomplete(style_source_loss::budget);
        const auto source_accounting=std::make_shared<const style_source_accounting_s>(staged.source_accounting);
        revision_input.entries.reserve(order.size());
        for(auto* node:order) {
            const auto& prepared=*staged.prepared.at(node);
            NodeRecordInput input;
            input.origin=RecordOrigin::runtime; input.node=node; input.style=prepared.style;
            auto typography_record=std::make_shared<TypographyRecord>();
            typography_record->evidence=prepared.typography;
            typography_record->inputs=source_accounting;
            typography_record->text_diagnostic=prepared.text_diagnostic;
            if (source_accounting->losses || !source_accounting->inputs_accounted)
                typography_record->evidence.coverage_gaps |= 4;
            typography_record->generation=revision_input.generation;
            typography_record->document_identity=revision_input.document_identity;
            typography_record->input_epoch=revision_input.input_epoch;
            typography_record->structure_epoch=revision_input.structure_epoch;
            typography_record->style_tree_epoch=revision_input.style_tree_epoch;
            typography_record->sheet_epoch=transaction->base_sheet_epoch;
            input.typography=std::move(typography_record);
            require_style_status(freeze_runtime_measures(*prepared.measures,revision_input.document,input.measures));
            input.custom_properties=prepared.custom_properties;input.color_references=prepared.color_references;
            input.declared_impact=prepared.declared_impact;input.declared_inherited_impact=prepared.declared_inherited_impact;
            input.inherited_change_impact=prepared.inherited_change_impact;
            input.document_identity=d.identity;input.node_identity=aether_node_identity(node);
            input.node_lifetime=node->identity;input.document_lifetime=document->identity;
            if(auto it=leaves.find(node);it!=leaves.end())input.generated_leaf=it->second;
            FrozenNodeRef record;require_style_status(freeze_node_record(input,record));
            next_states.emplace(node,record);backing.entries.emplace(node,record);
            revision_input.entries.push_back({node,record,prepared.impact});
        }
        require_style_status(freeze_backing(backing,revision_input.backing));
        if(runtime->revision)for(const auto& old:runtime->revision->entries())
            if(!current_nodes.count(old.node()))revision_input.retired.push_back({old.record(),STYLE_IMPACT_ALL_BITS});
        StyleRevisionRef revision;require_style_status(freeze_style_revision(revision_input,revision));
        for(const auto& entry:revision_input.entries)if(entry.impact) {
            auto update=std::make_unique<aether_style_update_s>();update->node=entry.node;update->impact=entry.impact;
            const auto& record=entry.record;
            require_style_status(computed_style_deep_copy(record->style_view(),&update->current.style));
            update->current.record=record;update->current.measures=record->measures();update->current.revision=revision;
            update->current.document_identity=record->document_identity();update->current.node_identity=record->node_identity();
            update->current.node_lifetime=record->node_lifetime();update->current.document_lifetime=record->document_lifetime();
            transaction->updates.push_back(std::move(update));
        }
        staged.states.swap(next_states);staged.prepared.clear();
        if(transaction->updates.size()>static_cast<size_t>(INT32_MAX))return AETHER_ERROR_INVALID_STATE;
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
    transaction->document_lifetime->style_tree_epoch = transaction->revision->style_tree_epoch();
    runtime->states.swap(transaction->candidate->states);
    runtime->document_measures.swap(transaction->candidate->document_measures);
    runtime->environment = transaction->revision->environment();
    runtime->generation = transaction->revision->generation();
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
    if (!runtime->revision || !runtime->revision->contextual()) return AETHER_ERROR_INVALID_STATE;
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
AETHER_API uint64_t aether_style_revision_generation(aether_style_revision_t revision) { return revision ? revision->value->generation() : 0; }
AETHER_API uint64_t aether_style_revision_document_identity(aether_style_revision_t revision) { return revision ? revision->value->document_identity() : 0; }
AETHER_API int32_t aether_style_revision_style_count(aether_style_revision_t revision) { return revision ? static_cast<int32_t>(revision->value->entries().size()) : 0; }
AETHER_API int32_t aether_style_revision_change_count(aether_style_revision_t revision) { return revision ? static_cast<int32_t>(revision->value->changes().size()) : 0; }
static aether_status_t revision_get_entry(aether_style_revision_t revision, ReadOnlyArrayView<FrozenRevisionEntry> entries,
    int32_t index,aether_node_t* out_node,aether_computed_style_handle_t* out_style,uint32_t* out_impact) {
    if(out_node)*out_node=nullptr;
    if(out_style)*out_style=nullptr;
    if(out_impact)*out_impact=0;
    if(!revision)return AETHER_ERROR_NULL_ARG;
    if(index<0||static_cast<size_t>(index)>=entries.size())return AETHER_ERROR_INVALID_STATE;
    const auto& entry=entries[static_cast<size_t>(index)];
    if(out_style&&entry.record()) {
        auto handle=std::unique_ptr<aether_computed_style_handle_s>(new(std::nothrow)aether_computed_style_handle_s());
        if(!handle)return AETHER_ERROR_OUT_OF_MEMORY;
        const auto& record=entry.record();
        auto status=computed_style_deep_copy(record->style_view(),&handle->style);if(status!=AETHER_OK)return status;
        handle->record=record;handle->measures=record->measures();handle->revision=revision->value;
        handle->document_identity=record->document_identity();handle->node_identity=record->node_identity();
        handle->document_lifetime=record->document_lifetime();handle->node_lifetime=record->node_lifetime();
        *out_style=handle.release();
    }
    if(out_node)*out_node=entry.node();
    if(out_impact)*out_impact=entry.impact();
    return AETHER_OK;
}
AETHER_API aether_status_t aether_style_revision_get_style(aether_style_revision_t revision, int32_t index,
    aether_node_t* node, aether_computed_style_handle_t* style, uint32_t* impact) {
    return revision_get_entry(revision, revision ? revision->value->entries() : ReadOnlyArrayView<FrozenRevisionEntry>{}, index, node, style, impact);
}
AETHER_API aether_status_t aether_style_revision_get_change(aether_style_revision_t revision, int32_t index,
    aether_node_t* node, aether_computed_style_handle_t* style, uint32_t* impact) {
    return revision_get_entry(revision, revision ? revision->value->changes() : ReadOnlyArrayView<FrozenRevisionEntry>{}, index, node, style, impact);
}
static aether_status_t create_revision_map(aether_style_revision_t revision,aether_style_map_t* out_map,bool legacy=false) {
    if(out_map)*out_map=nullptr;
    if(!revision||!out_map)return AETHER_ERROR_NULL_ARG;
    try {
        auto map=std::make_unique<aether_style_map_s>();const auto& r=*revision->value;
        MapVersionInput input;input.backing=r.backing();input.revision=revision->value;
        input.mode=legacy||!r.contextual()?MapMode::legacy_complete:MapMode::contextual;
        input.document_identity=r.document_identity();input.generation=r.generation();input.document_lifetime=r.document_lifetime();
        input.structure_epoch=r.structure_epoch();input.input_epoch=r.input_epoch();input.style_tree_epoch=r.style_tree_epoch();
        auto status=make_map_version(input,map->current);if(status!=AETHER_OK)return status;
        *out_map=map.release();return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API aether_status_t aether_style_revision_create_map(aether_style_revision_t revision, aether_style_map_t* out_map) {
    if (out_map) *out_map = nullptr;
    if (!revision || !out_map) return AETHER_ERROR_NULL_ARG;
    if (!revision->value->contextual()) return AETHER_ERROR_INVALID_STATE;
    return create_revision_map(revision, out_map);
}
AETHER_API aether_status_t aether_style_runtime_create_resolved_style_map(aether_style_runtime_t runtime, aether_doc_t doc, aether_style_map_t* out_map) {
    if (out_map) *out_map = nullptr;
    if (!runtime || !doc || !out_map) return AETHER_ERROR_NULL_ARG;
    if (!runtime->revision || runtime->revision->document_identity() != aether_node_identity(doc)) return AETHER_ERROR_INVALID_STATE;
    aether_style_revision_handle_s handle{runtime->revision};
    const auto status = create_revision_map(&handle, out_map, true);
    return status;
}
AETHER_API aether_status_t aether_style_runtime_update_resolved_style_map(aether_style_runtime_t runtime,
    aether_style_map_t map,const aether_node_t* nodes,int32_t node_count) {
    if(!runtime||!map||(!nodes&&node_count>0))return AETHER_ERROR_NULL_ARG;
    const auto previous=pin_style_map(map);
    if(node_count<0||(previous&&previous->contextual()))return AETHER_ERROR_INVALID_STATE;
    try {
        BackingBuildInput backing;if(previous)backing.entries=previous->backing()->entries();
        for(int32_t i=0;i<node_count;++i){auto* node=nodes[i];if(!node)continue;
            const auto it=runtime->states.find(node);
            if(it==runtime->states.end())backing.entries.erase(node);
            else backing.entries.insert_or_assign(node,it->second);
        }
        MapVersionInput input;input.mode=MapMode::legacy_partial;
        if(previous){input.document_lifetime=previous->document_lifetime();input.document_identity=previous->document_identity();
            input.input_epoch=previous->input_epoch();input.structure_epoch=previous->structure_epoch();input.style_tree_epoch=previous->style_tree_epoch();}
        auto status=freeze_backing(backing,input.backing);if(status!=AETHER_OK)return status;
        MapVersionRef next;status=make_map_version(input,next);if(status!=AETHER_OK)return status;
        map->current.swap(next);return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API void aether_style_runtime_clear_cache(aether_style_runtime_t runtime) { if (runtime) { runtime->states.clear(); ++runtime->sheet_epoch; } }

void style_runtime_set_value_audit(aether_style_runtime_t runtime,const style_value_audit_s* audit) {
    if(runtime&&!runtime->preparing&&runtime->owner_thread==std::this_thread::get_id())
        runtime->value_audit=audit;
}

// Operational consumers must check this both before and after any callback.
// Immutable historical descriptors remain readable but cannot prove currency.
bool aether::style::typography_inputs_current(aether_style_runtime_t runtime, const MapVersionRef& version) noexcept {
    if (!runtime || runtime->owner_thread!=std::this_thread::get_id() || runtime->preparing ||
        !version || !version->contextual() || !version->revision() ||
        runtime->revision!=version->revision() || runtime->sheet_epoch!=version->revision()->sheet_epoch()) return false;
    return version->document_lifetime() && structure_valid(version.get()) && inputs_current(version.get());
}
