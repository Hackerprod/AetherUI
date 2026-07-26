#include "style_runtime_types.h"

// ═══ Rule index ════════════════════════════════════════════════
//
// Buckets compiled rules by their selector's anchor (rightmost simple
// selector) so that candidate retrieval for a given node is O(1) per
// anchor type instead of O(N) over all rules.
//
// Anchor priority: id > class > type > universal.
// A rule is placed in exactly one bucket.

static void add_to_bucket(
    std::unordered_map<std::string, std::vector<const compiled_rule_s*>>& map,
    const std::string& key,
    const compiled_rule_s* rule)
{
    map[key].push_back(rule);
}

// Convert a type name to lowercase for case-insensitive bucket key.
static std::string type_key(const std::string& s) {
    return str_to_lower(s);
}

void rule_index_clear(rule_index_s* idx) {
    idx->by_id.clear();
    idx->by_class.clear();
    idx->by_type.clear();
    idx->universal.clear();
    idx->dependencies = SELECTOR_DEP_NONE;
}

void rule_index_add(rule_index_s* idx, const compiled_rule_s* rule) {
    idx->dependencies = idx->dependencies | rule->selector.dependencies;
    const auto& anchor = rule->selector.anchor;

    if (!anchor.id.empty()) {
        add_to_bucket(idx->by_id, anchor.id, rule);
    } else if (!anchor.class_name.empty()) {
        add_to_bucket(idx->by_class, anchor.class_name, rule);
    } else if (!anchor.type_name.empty()) {
        add_to_bucket(idx->by_type, type_key(anchor.type_name), rule);
    } else {
        idx->universal.push_back(rule);
    }
}

void rule_index_rebuild(
    rule_index_s* idx,
    const std::vector<const compiled_style_sheet_s*>& sheets)
{
    rule_index_clear(idx);

    // Sort sheets by layer then handle to preserve cascade source order.
    std::vector<const compiled_style_sheet_s*> sorted = sheets;
    std::sort(sorted.begin(), sorted.end(),
        [](const compiled_style_sheet_s* a, const compiled_style_sheet_s* b) {
            if (a->layer != b->layer) return a->layer < b->layer;
            return a->handle < b->handle;
        });

    for (const auto* sheet : sorted) {
        for (const auto& rule : sheet->rules) {
            rule_index_add(idx, &rule);
        }
    }
}

void rule_index_get_candidates(
    const rule_index_s* idx,
    const node_style_info_s* node,
    std::vector<const compiled_rule_s*>& out)
{
    std::unordered_set<int32_t> seen;

    auto add_range = [&](const std::vector<const compiled_rule_s*>& rules) {
        for (const auto* rule : rules) {
            if (seen.insert(rule->rule_id).second) {
                out.push_back(rule);
            }
        }
    };

    if (!node->id.empty()) {
        auto it = idx->by_id.find(node->id);
        if (it != idx->by_id.end()) {
            add_range(it->second);
        }
    }

    for (const auto& cls : node->classes) {
        auto it = idx->by_class.find(cls);
        if (it != idx->by_class.end()) {
            add_range(it->second);
        }
    }

    if (!node->type_name.empty()) {
        auto it = idx->by_type.find(type_key(node->type_name));
        if (it != idx->by_type.end()) {
            add_range(it->second);
        }
    }

    add_range(idx->universal);
}
