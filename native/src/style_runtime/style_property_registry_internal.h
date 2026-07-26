#pragma once

#include "style_runtime_types.h"
#include <functional>
#include <string>

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

struct style_property_definition_s {
    int32_t id = -1;
    std::string name;
    bool inherited = false;
    uint32_t impact = STYLE_IMPACT_NONE;
    std::string initial_value;
    property_parse_fn parse;
    property_apply_fn apply;
};

// ═══ Shorthand definition ══════════════════════════════════════

using shorthand_expand_fn = std::function<bool(const char* source, style_shorthand_writer_s& writer)>;

struct style_shorthand_definition_s {
    std::string name;
    shorthand_expand_fn expand;
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

    void register_shorthand(const char* name, shorthand_expand_fn expand) {
        if (sealed) return;
        if (!name || !*name) return;

        std::string normalized = str_trim(str_to_lower(name));
        if (by_name.count(normalized) || shorthands.count(normalized)) return;

        style_shorthand_definition_s def;
        def.name = normalized;
        def.expand = std::move(expand);

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
        for (const auto& shorthand : shorthand_defs)
            shorthands[shorthand.name] = &shorthand;
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
