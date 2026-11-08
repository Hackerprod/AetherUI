#pragma once

#include "style_string_utils_internal.h"
#include "../css/measure_state.h"
#include "../css/frozen_measure_read.h"
#include "frozen_runtime_metadata.h"
#include <aether/aether.h>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ═══ Style impact flags ════════════════════════════════════════

enum style_impact_flags : uint32_t {
    STYLE_IMPACT_NONE        = 0,
    STYLE_IMPACT_LAYOUT      = 1 << 0,
    STYLE_IMPACT_PAINT       = 1 << 1,
    STYLE_IMPACT_HITTEST     = 1 << 2,
    STYLE_IMPACT_COMPOSITE   = 1 << 3,
    STYLE_IMPACT_TEXT_LAYOUT = 1 << 4,
    STYLE_IMPACT_CURSOR      = 1 << 5,
    STYLE_IMPACT_INHERITED   = 1 << 6,
};

inline style_impact_flags operator|(style_impact_flags a, style_impact_flags b) {
    return static_cast<style_impact_flags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline style_impact_flags& operator|=(style_impact_flags& a, style_impact_flags b) {
    a = a | b;
    return a;
}

// ═══ Shorthand writer ══════════════════════════════════════════

struct style_shorthand_writer_s {
    struct item {
        std::string name;
        std::string value;
    };
    std::vector<item> items;

    void set(const char* name, const char* value) {
        if (!name || !*name || !value || !*value) return;
        items.push_back({ str_trim(str_to_lower(name)), str_trim(value) });
    }
};

// ═══ Property definition ═══════════════════════════════════════

using property_parse_fn = std::function<bool(const char* source, std::string& out_value)>;
using property_apply_fn = std::function<void(aether_computed_style_t* style, const std::string& value)>;

/* Public computed styles are concrete. Private symbolic metadata survives
   in runtime node state so inherited currentColor can resolve for descendants. */
struct color_resolution_state_s {
    bool background_uses_current_color = false;
    bool border_uses_current_color[4] = { true, true, true, true };
    std::vector<bool> text_shadow_uses_current_color;
    std::vector<bool> box_shadow_uses_current_color;
};

struct property_apply_context_s {
    aether_computed_style_t* style = nullptr;
    color_resolution_state_s* color_resolution = nullptr;
    measure_apply_state_s* measures = nullptr;
    const aether::style::ColorMetadataReadView* parent_color_resolution = nullptr;
};

using property_apply_context_fn = std::function<void(
    property_apply_context_s& context, const std::string& value)>;

// Legacy applicators are intentionally adapted without changing their call sites.
// Fallible applicators return structured data; the runtime owns the policy of
// reporting it because it has the declaration's property name and source value.
struct property_apply_result_s {
    bool success = true;
    const char* diagnostic_code = nullptr;
    const char* diagnostic_message = nullptr;

    static property_apply_result_s failure(const char* code, const char* message) {
        return {false, code, message};
    }

    static property_apply_result_s warning(const char* code, const char* message) {
        return {true, code, message};
    }
};

// Optional pure diagnostic classification after parse returned false. The
// compiler/runtime own reporting, so literal and deferred rejections emit once.
using property_parse_failure_fn = std::function<property_apply_result_s(const char* source)>;

using property_apply_checked_fn = std::function<property_apply_result_s(
    aether_computed_style_t* style, const std::string& value)>;
using property_apply_context_checked_fn = std::function<property_apply_result_s(
    property_apply_context_s& context, const std::string& value)>;

// Opt-in hooks for properties whose explicit inheritance is not supplied by
// the default inherited-style copy. These never copy unrelated style fields.
enum class property_css_wide_e { none, initial, inherit, unset };
using property_css_wide_fn = property_css_wide_e (*)(const std::string& source);
using property_inherit_fn = void (*)(aether_computed_style_t* style,
                                     aether::style::ComputedStyleReadView parent);

struct style_property_definition_s {
    int32_t id = -1;
    std::string name;
    bool inherited = false;
    uint32_t impact = STYLE_IMPACT_NONE;
    std::string initial_value;
    property_parse_fn parse;
    property_parse_failure_fn parse_failure;
    property_css_wide_fn css_wide = nullptr;
    property_inherit_fn inherit_explicit = nullptr;
    std::function<void(property_apply_context_s&, aether::style::ComputedStyleReadView, const aether::style::MeasuresReadView*)> inherit_context;
    property_apply_fn apply;
    property_apply_checked_fn apply_checked;
    property_apply_context_fn apply_with_context;
    property_apply_context_checked_fn apply_with_context_checked;
};

// ═══ Shorthand definition ══════════════════════════════════════

using shorthand_expand_fn = std::function<bool(const char* source, style_shorthand_writer_s& writer)>;

struct style_shorthand_definition_s {
    std::string name;
    shorthand_expand_fn expand;
    // Complete destination set, independent of the authored value. Resolved at seal().
    std::vector<std::string> target_names;
    std::vector<int32_t> target_ids;
};

// ═══ Property registry ═════════════════════════════════════════

struct style_property_registry_s {
    std::unordered_map<std::string, const style_property_definition_s*> by_name;
    std::unordered_map<std::string, const style_shorthand_definition_s*> shorthands;
    std::vector<style_property_definition_s> definitions;
    std::vector<style_shorthand_definition_s> shorthand_defs;
    bool sealed = false;

    void register_property(
        const char* name,
        bool inherited,
        uint32_t impact,
        const char* initial_value,
        property_parse_fn parse,
        property_apply_fn apply)
    {
        if (sealed) return;
        if (!name || !*name) return;

        std::string normalized = str_trim(str_to_lower(name));
        if (by_name.count(normalized)) return;

        style_property_definition_s def;
        def.id = static_cast<int32_t>(definitions.size());
        def.name = normalized;
        def.inherited = inherited;
        def.impact = inherited ? (impact | STYLE_IMPACT_INHERITED) : impact;
        def.initial_value = initial_value ? initial_value : "";
        def.parse = std::move(parse);
        def.apply = std::move(apply);

        definitions.push_back(std::move(def));
        by_name[normalized] = &definitions.back();
    }

    void register_property_checked(
        const char* name,
        bool inherited,
        uint32_t impact,
        const char* initial_value,
        property_parse_fn parse,
        property_apply_checked_fn apply,
        property_parse_failure_fn parse_failure = {},
        property_inherit_fn inherit_explicit = {},
        property_css_wide_fn css_wide = {})
    {
        if (sealed) return;
        if (!name || !*name) return;

        std::string normalized = str_trim(str_to_lower(name));
        if (by_name.count(normalized)) return;

        style_property_definition_s def;
        def.id = static_cast<int32_t>(definitions.size());
        def.name = normalized;
        def.inherited = inherited;
        def.impact = inherited ? (impact | STYLE_IMPACT_INHERITED) : impact;
        def.initial_value = initial_value ? initial_value : "";
        def.parse = std::move(parse);
        def.apply_checked = std::move(apply);
        def.parse_failure = std::move(parse_failure);
        def.inherit_explicit = std::move(inherit_explicit);
        def.css_wide = std::move(css_wide);

        definitions.push_back(std::move(def));
        by_name[normalized] = &definitions.back();
    }

    void register_property_with_context(
        const char* name,
        bool inherited,
        uint32_t impact,
        const char* initial_value,
        property_parse_fn parse,
        property_apply_context_fn apply)
    {
        if (sealed) return;
        if (!name || !*name) return;

        std::string normalized = str_trim(str_to_lower(name));
        if (by_name.count(normalized)) return;

        style_property_definition_s def;
        def.id = static_cast<int32_t>(definitions.size());
        def.name = normalized;
        def.inherited = inherited;
        def.impact = inherited ? (impact | STYLE_IMPACT_INHERITED) : impact;
        def.initial_value = initial_value ? initial_value : "";
        def.parse = std::move(parse);
        def.apply_with_context = std::move(apply);

        definitions.push_back(std::move(def));
        by_name[normalized] = &definitions.back();
    }

    void register_property_checked_with_context(
        const char* name,
        bool inherited,
        uint32_t impact,
        const char* initial_value,
        property_parse_fn parse,
        property_apply_context_checked_fn apply,
        property_parse_failure_fn parse_failure = {})
    {
        if (sealed) return;
        if (!name || !*name) return;

        std::string normalized = str_trim(str_to_lower(name));
        if (by_name.count(normalized)) return;

        style_property_definition_s def;
        def.id = static_cast<int32_t>(definitions.size());
        def.name = normalized;
        def.inherited = inherited;
        def.impact = inherited ? (impact | STYLE_IMPACT_INHERITED) : impact;
        def.initial_value = initial_value ? initial_value : "";
        def.parse = std::move(parse);
        def.apply_with_context_checked = std::move(apply);
        def.parse_failure = std::move(parse_failure);

        definitions.push_back(std::move(def));
        by_name[normalized] = &definitions.back();
    }

    void register_shorthand(const char* name, shorthand_expand_fn expand,
                            std::vector<std::string> targets = {}) {
        if (sealed) return;
        if (!name || !*name) return;

        std::string normalized = str_trim(str_to_lower(name));
        if (by_name.count(normalized) || shorthands.count(normalized)) return;

        style_shorthand_definition_s def;
        def.name = normalized;
        def.expand = std::move(expand);
        def.target_names = std::move(targets);

        shorthand_defs.push_back(std::move(def));
        shorthands[normalized] = &shorthand_defs.back();
    }

    void seal() {
        // std::vector growth invalidates the pointers cached while properties are
        // registered. Rebuild both lookup tables once the registry becomes stable.
        by_name.clear();
        for (const auto& definition : definitions)
            by_name[definition.name] = &definition;
        shorthands.clear();
        for (auto& shorthand : shorthand_defs) {
            shorthands[shorthand.name] = &shorthand;
            shorthand.target_ids.clear();
            for (const auto& name : shorthand.target_names) {
                const auto found = by_name.find(name);
                if (found == by_name.end()) { shorthand.target_ids.clear(); break; }
                const auto id = found->second->id;
                bool duplicate = false;
                for (auto previous : shorthand.target_ids) if (previous == id) duplicate = true;
                if (duplicate) { shorthand.target_ids.clear(); break; }
                shorthand.target_ids.push_back(id);
            }
        }
        sealed = true;
    }

    const style_property_definition_s* try_get(const char* name) const {
        std::string normalized = str_trim(str_to_lower(name));
        auto it = by_name.find(normalized);
        return it != by_name.end() ? it->second : nullptr;
    }

    const style_shorthand_definition_s* try_get_shorthand(const char* name) const {
        std::string normalized = str_trim(str_to_lower(name));
        auto it = shorthands.find(normalized);
        return it != shorthands.end() ? it->second : nullptr;
    }

    int32_t property_count() const { return static_cast<int32_t>(definitions.size()); }
};

// ═══ Registry API ══════════════════════════════════════════════

style_property_registry_s* style_property_registry_get_global();
void style_property_registry_init();
void style_property_registry_shutdown();
