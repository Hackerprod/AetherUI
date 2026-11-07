#pragma once
#include <aether/aether.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include "style_map_types.h"
#include "computed_style_handle.h"

/* ── Internal CSS data structures ─────────────────────────────────── */

struct css_declaration_s {
    std::string name;
    std::string value;
    bool important = false;
};

struct css_simple_selector_s {
    std::string tag_name;   // empty = any
    std::string id;         // empty = any
    std::vector<std::string> classes;
    std::vector<std::pair<std::string, std::string>> attributes; // name, value (empty = presence only)
    std::vector<std::string> pseudo_classes;
};

enum css_combinator_t : uint8_t {
    COMB_NONE = 0,
    COMB_DESCENDANT,
    COMB_CHILD,
};

struct css_selector_step_s {
    css_simple_selector_s simple;
    css_combinator_t to_left = COMB_NONE;
};

struct css_selector_s {
    std::vector<css_selector_step_s> steps; // rightmost first
    int specificity = 0;
    std::string source;
};

struct css_rule_s {
    std::vector<css_selector_s> selectors;
    std::vector<css_declaration_s> declarations;
    int source_order = 0;
};

struct aether_stylesheet_s {
    std::vector<css_rule_s> rules;
};


/* Selector matching (implemented in selector.cpp) */
bool css_selector_matches(const css_selector_s& sel, aether_node_s* element);
