#include "style_runtime_types.h"
#include "../primitives/ascii_text.h"
#include <array>
#include <utility>

rule_index_s::rule_index_s(const rule_index_s& other)
    : dependencies(other.dependencies), by_id_(other.by_id_), by_class_(other.by_class_),
      by_type_(other.by_type_), universal_(other.universal_) {}

rule_index_s& rule_index_s::operator=(const rule_index_s& other) {
    if (this != &other) {
        rule_index_s copy(other); // Complete the fallible copy before publication.
        swap(*this, copy);
    } else {
        invalidate_capabilities();
    }
    return *this;
}

rule_index_s::rule_index_s(rule_index_s&& other) noexcept { swap(*this, other); }
rule_index_s& rule_index_s::operator=(rule_index_s&& other) noexcept {
    if (this != &other) {
        rule_index_s moved(std::move(other));
        swap(*this, moved);
    }
    return *this;
}
void swap(rule_index_s& left, rule_index_s& right) noexcept {
    using std::swap;
    swap(left.by_id_, right.by_id_); swap(left.by_class_, right.by_class_);
    swap(left.by_type_, right.by_type_); swap(left.universal_, right.universal_);
    swap(left.dependencies, right.dependencies);
    swap(left.before_possible_, right.before_possible_);
    swap(left.after_possible_, right.after_possible_);
    swap(left.pseudo_capabilities_complete_, right.pseudo_capabilities_complete_);
    swap(left.disjoint_runtime_rules_, right.disjoint_runtime_rules_);
}

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
    return aether::ascii_lowercase(s);
}

void rule_index_clear(rule_index_s* idx) {
    idx->invalidate_capabilities();
    idx->by_id_.clear();
    idx->by_class_.clear();
    idx->by_type_.clear();
    idx->universal_.clear();
    idx->dependencies = SELECTOR_DEP_NONE;
}

void rule_index_add(rule_index_s* idx, const compiled_rule_s* rule) {
    idx->invalidate_capabilities();
    idx->dependencies = idx->dependencies | rule->selector.dependencies;
    const auto& anchor = rule->selector.anchor;

    if (!anchor.id.empty()) {
        add_to_bucket(idx->by_id_, anchor.id, rule);
    } else if (!anchor.class_name.empty()) {
        add_to_bucket(idx->by_class_, anchor.class_name, rule);
    } else if (!anchor.type_name.empty()) {
        add_to_bucket(idx->by_type_, type_key(anchor.type_name), rule);
    } else {
        idx->universal_.push_back(rule);
    }
}

void rule_index_rebuild(
    rule_index_s* idx,
    const std::vector<const compiled_style_sheet_s*>& sheets)
{
    rule_index_clear(idx);

    // Keep rebuilding deterministic. Cascade correctness comes from each rule's
    // global source_order, not this insertion order or later bucket traversal.
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

static void get_generic_candidates(
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
        auto it = idx->by_id().find(node->id);
        if (it != idx->by_id().end()) {
            add_range(it->second);
        }
    }

    for (const auto& cls : node->classes) {
        auto it = idx->by_class().find(cls);
        if (it != idx->by_class().end()) {
            add_range(it->second);
        }
    }

    if (!node->type_name.empty()) {
        auto it = idx->by_type().find(type_key(node->type_name));
        if (it != idx->by_type().end()) {
            add_range(it->second);
        }
    }

    add_range(idx->universal());
}

// Runtime rules have stable IDs and exactly one bucket. Only repeated class
// tokens can visit a range twice. Keep the first eight matching bucket addresses
// inline; missing classes consume no slots. The overflow set is lazy and local.
static void get_disjoint_candidates(
    const rule_index_s* idx, const node_style_info_s* node,
    std::vector<const compiled_rule_s*>& out)
{
    const auto append = [&](const rule_index_s::bucket_type& rules) {
        out.insert(out.end(), rules.begin(), rules.end());
    };
    if (!node->id.empty()) {
        const auto it = idx->by_id().find(node->id);
        if (it != idx->by_id().end()) append(it->second);
    }
    std::array<const rule_index_s::bucket_type*, 8> inline_seen{};
    size_t inline_count = 0;
    std::unordered_set<const rule_index_s::bucket_type*> overflow;
    for (const auto& cls : node->classes) {
        const auto it = idx->by_class().find(cls);
        if (it == idx->by_class().end()) continue;
        const auto* bucket = &it->second;
        const auto end = inline_seen.begin() + inline_count;
        if (std::find(inline_seen.begin(), end, bucket) != end) continue;
        if (inline_count < inline_seen.size()) inline_seen[inline_count++] = bucket;
        else if (!overflow.insert(bucket).second) continue;
        append(*bucket);
    }
    if (!node->type_name.empty()) {
        const auto it = idx->by_type().find(type_key(node->type_name));
        if (it != idx->by_type().end()) append(it->second);
    }
    append(idx->universal());
}

void rule_index_get_candidates(
    const rule_index_s* idx, const node_style_info_s* node,
    std::vector<const compiled_rule_s*>& out)
{
    if (idx->has_disjoint_runtime_rules()) get_disjoint_candidates(idx, node, out);
    else get_generic_candidates(idx, node, out);
}
