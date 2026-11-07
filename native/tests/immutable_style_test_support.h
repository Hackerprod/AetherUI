#pragma once
// Private fixture adapters. They publish a new immutable legacy map rather than
// mutating a retained record or manufacturing a flat view of frozen payloads.
#include "../src/css/style_map_types.h"
#include "../src/css/style_freeze.h"
#include "../src/css/computed_style_handle.h"
#include <stdexcept>

namespace immutable_style_test {
inline void ok(aether_status_t value) {
    if (value != AETHER_OK) throw std::runtime_error("immutable fixture construction failed");
}
inline aether::style::FrozenNodeRef record(aether_style_map_t map, aether_node_t node) {
    if (!map || !map->current || map->current->contextual())
        throw std::runtime_error("fixture requires an existing legacy map");
    auto value = map->current->backing()->find_owner(node);
    if (!value) throw std::runtime_error("fixture node absent");
    return value;
}
inline void publish(aether_style_map_t map, aether::style::BackingBuildInput input) {
    const auto old = map->current;
    aether::style::BackingRef backing;
    ok(aether::style::freeze_backing(input, backing));
    aether::style::MapVersionInput version;
    version.backing = std::move(backing);
    version.document_lifetime = old->document_lifetime();
    version.document_identity = old->document_identity();
    version.structure_epoch = old->structure_epoch();
    version.input_epoch = old->input_epoch();
    version.style_tree_epoch = old->style_tree_epoch();
    aether::style::MapVersionRef replacement;
    ok(aether::style::make_map_version(version, replacement));
    map->current = std::move(replacement);
}
inline void replace_style(aether_style_map_t map, aether_node_t node,
                          aether::style::FrozenStyleRef style) {
    const auto old = record(map, node);
    aether::style::NodeRecordInput in;
    in.origin = old->origin(); in.node = old->node(); in.style = std::move(style);
    in.measures = old->measures(); in.custom_properties = old->custom_properties();
    in.color_references = old->color_references();
    in.declared_impact = old->declared_impact();
    in.declared_inherited_impact = old->declared_inherited_impact();
    in.inherited_change_impact = old->inherited_change_impact();
    in.document_identity = old->document_identity(); in.node_identity = old->node_identity();
    in.node_lifetime = old->node_lifetime(); in.document_lifetime = old->document_lifetime();
    in.generated_leaf = old->generated_leaf();
    aether::style::FrozenNodeRef replacement;
    ok(aether::style::freeze_node_record(in, replacement));
    aether::style::BackingBuildInput input;
    input.entries = map->current->backing()->entries();
    input.entries.at(node) = std::move(replacement);
    publish(map, std::move(input));
}
inline void replace_style(aether_style_map_t map, aether_node_t node,
                          const aether_computed_style_t& independent_input) {
    aether::style::FrozenStyleRef style;
    ok(aether::style::freeze_style(independent_input, style));
    replace_style(map, node, std::move(style));
}
template<class Edit> void edit_scalars(aether_style_map_t map, aether_node_t node, Edit edit) {
    const auto old = record(map, node);
    auto scalars = old->style_view().scalars();
    edit(scalars);
    replace_style(map, node, std::make_shared<const aether::style::FrozenStyle>(
        scalars, old->style()->payload_owner()));
}
inline void erase(aether_style_map_t map, aether_node_t node) {
    (void)record(map, node);
    aether::style::BackingBuildInput input;
    input.entries = map->current->backing()->entries();
    input.entries.erase(node);
    publish(map, std::move(input));
}
}
