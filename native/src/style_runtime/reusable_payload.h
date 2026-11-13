#pragma once
// R0b capture evidence only. No eligibility test, lookup, or reuse path.
#include "hover_provenance.h"
#include "typography_evidence.h"
#include "frozen_runtime_metadata.h"
#include "../css/frozen_style.h"
#include "../css/measure_state.h"

namespace aether::style {
// A new const allocation for every math node prevents a caller's mutable alias
// from crossing this boundary. Limits also reject cycles/malformed input.
inline aether::css::math_tree capture_payload_math(const aether::css::math_tree& tree,
                                                  size_t& budget, size_t depth=0) {
    if (!tree) return {};
    if (!budget || depth> aether::css::contextual_math_detail::max_depth)
        throw std::invalid_argument("payload math limits");
    --budget;
    aether::css::math_node copy;
    copy.kind=tree->kind; copy.op=tree->op; copy.unit=tree->unit;
    copy.value=tree->value; copy.percentage=tree->percentage;
    copy.children.reserve(tree->children.size());
    for (const auto& child:tree->children)
        copy.children.push_back(capture_payload_math(child,budget,depth+1));
    return std::make_shared<const aether::css::math_node>(std::move(copy));
}
// Own semantic fields only: deliberately no document owner or epoch-bearing
// final records. Const members prohibit mutation even via a non-const owner.
struct FrozenPayloadMeasures {
    const int32_t flex_order;
    const bool direction_rtl;
    const int32_t writing_mode;
    const float parent_font_size, self_font_size;
    const measure_line_height_s line_height;
    const aether_css_length row_gap, column_gap;
    const std::vector<measure_background_position_s> background_positions;
    const std::vector<measure_background_size_s> background_sizes;
    const uint32_t dependencies;
    const std::unordered_map<std::string,uint32_t> property_dependencies;
    const std::unordered_map<std::string,aether::css::math_tree> contextual_lengths;
    const std::unordered_map<std::string,aether::css::intrinsic_size_keyword> intrinsic_keywords;
private:
    static auto capture_trees(const measure_state_s& s) {
        std::unordered_map<std::string,aether::css::math_tree> out;
        for (const auto& [key,tree]:s.contextual_lengths) {
            size_t budget=aether::css::contextual_math_detail::max_nodes;
            out.emplace(key,capture_payload_math(tree,budget));
        }
        return out;
    }
public:
    explicit FrozenPayloadMeasures(const measure_state_s& s)
        : flex_order(s.flex_order), direction_rtl(s.direction_rtl), writing_mode(s.writing_mode),
          parent_font_size(s.parent_font_size), self_font_size(s.self_font_size), line_height(s.line_height),
          row_gap(s.row_gap), column_gap(s.column_gap), background_positions(s.background_positions),
          background_sizes(s.background_sizes), dependencies(s.dependencies),
          property_dependencies(s.property_dependencies), contextual_lengths(capture_trees(s)),
          intrinsic_keywords(s.intrinsic_keywords) {}
};
struct FrozenReusableNodePayload {
    const FrozenStyleRef style;
    const FrozenScopeRef normal_scope, backdrop_scope;
    const FrozenColorRef colors;
    const TypographyEvidence typography; // Prepared evidence, not final accounting.
    const FrozenPayloadMeasures measures;
    const bool document_element;
    FrozenReusableNodePayload(FrozenStyleRef st, FrozenScopeRef normal, FrozenScopeRef backdrop,
        FrozenColorRef color, const TypographyEvidence& type, const measure_state_s& m, bool root)
        : style(std::move(st)), normal_scope(std::move(normal)), backdrop_scope(std::move(backdrop)),
          colors(std::move(color)), typography(type), measures(m), document_element(root) {}
};
struct ReusablePayloadSnapshot {
    struct Entry {
        std::shared_ptr<node_identity_s> node, parent;
        std::shared_ptr<const FrozenReusableNodePayload> payload;
    };
    // Mutable identity tokens are lifetime evidence, never semantic payload.
    std::shared_ptr<node_identity_s> document, root;
    uint64_t source_generation=0, published_generation=0, source_input_epoch=0;
    uint64_t source_structure_epoch=0, source_style_tree_epoch=0, published_style_tree_epoch=0;
    uint64_t sheet_epoch=0, non_state_epoch=0;
    bool non_state_exhausted=true, contextual=false;
    aether_style_environment_t environment{};
    aether_style_runtime_options_t options{};
    std::vector<Entry> entries;
};
using ReusablePayloadSnapshotRef=std::shared_ptr<const ReusablePayloadSnapshot>;
}
