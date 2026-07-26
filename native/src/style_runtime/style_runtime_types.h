#pragma once
#include "../dom/pseudo_element.h"

#include <aether/aether.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <memory>

// ═══ String utilities (inline, shared) ═════════════════════════

inline std::string str_trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

inline std::string str_to_lower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

inline bool str_iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

inline bool str_starts_with_i(const std::string& s, const char* prefix) {
    size_t plen = std::strlen(prefix);
    if (s.size() < plen) return false;
    for (size_t i = 0; i < plen; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

inline int32_t find_top_level(const std::string& s, char ch, int32_t start = 0) {
    int32_t depth = 0;
    char quote = '\0';
    int32_t len = static_cast<int32_t>(s.size());
    for (int32_t i = start; i < len; ++i) {
        char c = s[i];
        if (quote != '\0') {
            if (c == '\\' && i + 1 < len) { ++i; continue; }
            if (c == quote) quote = '\0';
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
        if (c == ')' || c == ']' || c == '}') { depth = std::max(0, depth - 1); continue; }
        if (depth == 0 && c == ch) return i;
    }
    return -1;
}

inline std::vector<std::string> split_top_level(const std::string& s, char ch) {
    std::vector<std::string> result;
    int32_t start = 0;
    int32_t len = static_cast<int32_t>(s.size());
    while (start < len) {
        int32_t pos = find_top_level(s, ch, start);
        if (pos < 0) {
            result.push_back(s.substr(start));
            break;
        }
        result.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return result;
}

// ═══ Specificity ═══════════════════════════════════════════════

struct specificity_s {
    int32_t id_count = 0;
    int32_t class_count = 0;
    int32_t type_count = 0;

    int compare_to(const specificity_s& o) const {
        if (id_count != o.id_count) return (id_count < o.id_count) ? -1 : 1;
        if (class_count != o.class_count) return (class_count < o.class_count) ? -1 : 1;
        if (type_count != o.type_count) return (type_count < o.type_count) ? -1 : 1;
        return 0;
    }

    specificity_s operator+(const specificity_s& o) const {
        return { id_count + o.id_count, class_count + o.class_count, type_count + o.type_count };
    }

    bool operator==(const specificity_s& o) const {
        return id_count == o.id_count && class_count == o.class_count && type_count == o.type_count;
    }
};

// ═══ Selector dependency flags ═════════════════════════════════

enum selector_dependency_flags : uint16_t {
    SELECTOR_DEP_NONE           = 0,
    SELECTOR_DEP_SELF_CLASS     = 1 << 0,
    SELECTOR_DEP_SELF_ID        = 1 << 1,
    SELECTOR_DEP_SELF_ATTRIBUTE = 1 << 2,
    SELECTOR_DEP_SELF_STATE     = 1 << 3,
    SELECTOR_DEP_ANCESTOR       = 1 << 4,
    SELECTOR_DEP_SIBLING        = 1 << 5,
    SELECTOR_DEP_STRUCTURE      = 1 << 6,
    SELECTOR_DEP_DESCENDANT     = 1 << 7,
};

inline selector_dependency_flags operator|(selector_dependency_flags a, selector_dependency_flags b) {
    return static_cast<selector_dependency_flags>(
        static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline selector_dependency_flags& operator|=(selector_dependency_flags& a, selector_dependency_flags b) {
    a = a | b;
    return a;
}

using selector_dependency_t = selector_dependency_flags;

// ═══ Selector combinator ═══════════════════════════════════════

enum selector_combinator_t : uint8_t {
    COMB_NONE = 0,
    COMB_DESCENDANT,
    COMB_CHILD,
    COMB_ADJACENT_SIBLING,
    COMB_GENERAL_SIBLING,
};

// ═══ Attribute match operator ═════════════════════════════════

enum attribute_match_operator_t : uint8_t {
    ATTR_EXISTS = 0,
    ATTR_EQUALS,
    ATTR_INCLUDES,
    ATTR_DASH_MATCH,
    ATTR_PREFIX,
    ATTR_SUFFIX,
    ATTR_CONTAINS,
};

// ═══ Pseudo selector kind ═════════════════════════════════════

enum pseudo_selector_kind_t : uint8_t {
    PSEUDO_REQUIRED_STATE = 0,
    PSEUDO_FORBIDDEN_STATE,
    PSEUDO_ROOT,
    PSEUDO_EMPTY,
    PSEUDO_FIRST_CHILD,
    PSEUDO_LAST_CHILD,
    PSEUDO_ONLY_CHILD,
    PSEUDO_NTH_CHILD,
    PSEUDO_IS,
    PSEUDO_WHERE,
    PSEUDO_NOT,
    PSEUDO_HAS,
};

// ═══ Style pseudo-state flags ═════════════════════════════════

enum style_pseudo_state_t : uint32_t {
    PSEUDO_STATE_NONE          = 0,
    PSEUDO_STATE_HOVER         = 1u << 0,
    PSEUDO_STATE_ACTIVE        = 1u << 1,
    PSEUDO_STATE_FOCUS         = 1u << 2,
    PSEUDO_STATE_FOCUS_WITHIN  = 1u << 3,
    PSEUDO_STATE_DISABLED      = 1u << 4,
    PSEUDO_STATE_CHECKED       = 1u << 5,
    PSEUDO_STATE_INVALID       = 1u << 6,
    PSEUDO_STATE_READ_ONLY     = 1u << 7,
    PSEUDO_STATE_SELECTED      = 1u << 8,
    PSEUDO_STATE_EXPANDED      = 1u << 9,
};

// ═══ NthExpression ════════════════════════════════════════════

struct nth_expression_s {
    int step   = 0;
    int offset = 0;

    bool matches(int one_based_index) const {
        if (step == 0) return one_based_index == offset;
        int diff = one_based_index - offset;
        return diff == 0 || (diff % step == 0 && diff / step >= 0);
    }
};

// ═══ Attribute selector ═══════════════════════════════════════

struct attribute_selector_s {
    std::string name;
    attribute_match_operator_t op = ATTR_EXISTS;
    std::string value;
    bool has_value        = false;
    bool case_insensitive = false;
};

// ═══ Pseudo selector (forward-declares compiled_selector_s) ══

struct compiled_selector_s;

struct pseudo_selector_s {
    pseudo_selector_kind_t kind = PSEUDO_REQUIRED_STATE;
    style_pseudo_state_t state  = PSEUDO_STATE_NONE;
    nth_expression_s nth;
    std::unique_ptr<std::vector<compiled_selector_s>> nested;
};

// ═══ Compound selector ════════════════════════════════════════

struct compound_selector_s {
    std::string type_name;
    std::string id;
    std::vector<std::string> classes;
    std::vector<attribute_selector_s> attributes;
    std::vector<pseudo_selector_s> pseudos;
};

// ═══ Selector step (right-to-left) ════════════════════════════

struct selector_step_s {
    compound_selector_s compound;
    selector_combinator_t relation_to_next = COMB_NONE;
};

// ═══ Selector anchor ═══════════════════════════════════════════

struct selector_anchor_s {
    std::string id;
    std::string class_name;
    std::string type_name;

    bool is_universal() const {
        return id.empty() && class_name.empty() && type_name.empty();
    }
};

// ═══ Compiled selector ═════════════════════════════════════════

struct compiled_selector_s {
    std::vector<selector_step_s> steps;
    selector_anchor_s anchor;
    specificity_s specificity;
    selector_dependency_flags dependencies = SELECTOR_DEP_NONE;
    aether_pseudo_element_t pseudo_element = aether_pseudo_element_t::none;
    std::string source;
};

// ═══ Compiled style value ══════════════════════════════════════

enum class compiled_value_mode : uint8_t {
    value = 0,
    initial,
    inherit,
    unset,
    deferred,
};

struct compiled_style_value_s {
    compiled_value_mode mode = compiled_value_mode::value;
    std::string typed_value;
    std::string deferred_source;
};

// ═══ Property declarations ═════════════════════════════════════

struct compiled_property_declaration_s {
    int32_t property_id = 0;
    compiled_style_value_s value;
    bool important = false;
    int32_t declaration_order = 0;
};

struct custom_property_declaration_s {
    std::string name;
    std::string source;
    bool important = false;
    int32_t declaration_order = 0;
};

struct compiled_content_declaration_s {
    std::string source;
    bool important = false;
    int32_t declaration_order = 0;
};

// ═══ StyleSheet layer ══════════════════════════════════════════

enum class style_sheet_layer : uint8_t {
    engine_defaults = 0,
    theme = 1,
    author = 2,
};

// ═══ Media condition AST ═══════════════════════════════════════

struct media_condition_s {
    enum class kind_t : uint8_t {
        always_true,
        always_false,
        and_,
        or_,
        not_,
        length_feature,
        enum_feature,
    } kind = kind_t::always_true;

    media_condition_s* left   = nullptr;
    media_condition_s* right  = nullptr;
    media_condition_s* inner  = nullptr;

    enum class length_feature_t : uint8_t {
        min_width,
        max_width,
        min_height,
        max_height,
        width_equals,
        height_equals,
    } length_feature = {};

    aether_css_length length_value = {};

    enum class enum_feature_t : uint8_t {
        color_scheme,
        pointer_accuracy,
        motion_preference,
    } enum_feature = {};

    int32_t enum_value = 0;
};

inline media_condition_s* media_condition_always() {
    static media_condition_s node{ media_condition_s::kind_t::always_true };
    return &node;
}

inline media_condition_s* media_condition_never() {
    static media_condition_s node{ media_condition_s::kind_t::always_false };
    return &node;
}

inline void media_condition_destroy(media_condition_s* node) {
    if (!node) return;
    if (node == media_condition_always() || node == media_condition_never()) return;
    media_condition_destroy(node->left);
    media_condition_destroy(node->right);
    media_condition_destroy(node->inner);
    delete node;
}

media_condition_s* media_condition_and(media_condition_s* left, media_condition_s* right);
media_condition_s* media_condition_or(media_condition_s* left, media_condition_s* right);
media_condition_s* media_condition_not(media_condition_s* value);
bool media_condition_matches(const media_condition_s* node, const aether_style_environment_t* env);
media_condition_s* media_condition_parse(const char* source, aether_style_diagnostic_fn diag, void* diag_data, int32_t offset);

// ═══ Compiled rule & stylesheet ════════════════════════════════

struct compiled_rule_s {
    int32_t  rule_id = 0;
    int64_t  sheet_handle = 0;
    style_sheet_layer layer = style_sheet_layer::author;
    compiled_selector_s selector;
    media_condition_s* condition = nullptr;
    std::vector<compiled_property_declaration_s> declarations;
    std::vector<custom_property_declaration_s>   custom_declarations;
    std::vector<compiled_content_declaration_s>  content_declarations;
    int32_t source_order = 0;
};

struct compiled_style_sheet_s {
    int64_t handle = 0;
    style_sheet_layer layer = style_sheet_layer::author;
    std::string source_name;
    std::vector<compiled_rule_s> rules;
};

// ═══ Node info for rule index ══════════════════════════════════

struct node_style_info_s {
    std::string id;
    std::string type_name;
    std::vector<std::string> classes;
};

// ═══ Cascade types ═════════════════════════════════════════════

struct cascade_priority_t {
    bool important = false;
    bool inline_ = false;
    style_sheet_layer layer = style_sheet_layer::engine_defaults;
    specificity_s specificity = {};
    int32_t source_order = 0;
    int32_t decl_order = 0;
};

int cascade_priority_compare(const cascade_priority_t* a, const cascade_priority_t* b);

struct cascade_entry_s {
    bool is_set = false;
    cascade_priority_t priority = {};
    compiled_style_value_s value;

    void offer(const cascade_priority_t& p, const compiled_style_value_s& v);
};

struct cascade_entry_custom_s {
    bool is_set = false;
    cascade_priority_t priority = {};
    std::string source;

    void offer(const cascade_priority_t& p, const std::string& s);
};

// ═══ Custom property scope ═════════════════════════════════════

inline const std::string& custom_property_invalid_value() {
    static const std::string v("\0aether-invalid-custom-property\0", 32);
    return v;
}

struct custom_property_scope_s {
    std::unordered_map<std::string, std::string> local;
    custom_property_scope_s* parent = nullptr;

    bool try_get(const std::string& name, std::string& value) const;
};

// ═══ Variable resolver ═════════════════════════════════════════

bool variable_resolve(
    const char* source,
    const custom_property_scope_s* scope,
    int32_t max_depth,
    int32_t max_expansion_length,
    std::string& out_value);

// ═══ Rule index ════════════════════════════════════════════════

struct rule_index_s {
    std::unordered_map<std::string, std::vector<const compiled_rule_s*>> by_id;
    std::unordered_map<std::string, std::vector<const compiled_rule_s*>> by_class;
    std::unordered_map<std::string, std::vector<const compiled_rule_s*>> by_type;
    std::vector<const compiled_rule_s*> universal;
    selector_dependency_flags dependencies = SELECTOR_DEP_NONE;
};

void rule_index_clear(rule_index_s* idx);
void rule_index_add(rule_index_s* idx, const compiled_rule_s* rule);
void rule_index_rebuild(rule_index_s* idx, const std::vector<const compiled_style_sheet_s*>& sheets);
void rule_index_get_candidates(const rule_index_s* idx, const node_style_info_s* node, std::vector<const compiled_rule_s*>& out);

// ═══ Selector parser public API ═══════════════════════════════════════

bool selector_parse(const char* source, compiled_selector_s& out);
bool selector_parse_list(const char* source, std::vector<compiled_selector_s>& out);
bool selector_match(const compiled_selector_s& selector, aether_node_s* element);

aether_status_t style_source_compile(
    const char* source,
    int64_t sheet_handle,
    int32_t layer,
    std::vector<compiled_rule_s>& out_rules,
    int32_t& out_rule_id_counter);

aether_status_t style_source_compile_inline(
    const char* source,
    std::vector<compiled_property_declaration_s>& out_properties,
    std::vector<custom_property_declaration_s>& out_custom);
