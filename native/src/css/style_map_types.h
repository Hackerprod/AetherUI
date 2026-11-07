#pragma once
#include <aether/aether.h>
#include "computed_style_internal.h"
#include "measure_state.h"
#include "../dom/node_identity.h"
#include <memory>
#include <new>
#include <unordered_map>
struct aether_node_s;
struct style_revision_s;
/* A wrapper owns a complete independent backing. Layout retains a snapshot,
 * never this mutable wrapper. Contextual revision ownership is acyclic. */
struct aether_style_map_s {
    std::unordered_map<aether_node_s*, aether_computed_style_t> styles;
    std::unordered_map<aether_node_s*, std::shared_ptr<const measure_state_s>> measures;
    std::unordered_map<aether_node_s*, std::shared_ptr<node_identity_s>> node_lifetimes;
    std::shared_ptr<const style_revision_s> revision;
    std::shared_ptr<node_identity_s> document_lifetime;
    uint64_t document_identity = 0;
    uint64_t generation = 0;
    uint64_t structure_epoch = 0;
    uint64_t input_epoch = 0;
    uint64_t style_tree_epoch = 0;
    bool contextual = false;
    ~aether_style_map_s() { for (auto& entry : styles) aether_computed_style_free(&entry.second); }
    aether_style_map_s() = default;
    aether_style_map_s(const aether_style_map_s&) = delete;
    aether_style_map_s& operator=(const aether_style_map_s&) = delete;
    void swap(aether_style_map_s& other) noexcept {
        styles.swap(other.styles); measures.swap(other.measures); node_lifetimes.swap(other.node_lifetimes); revision.swap(other.revision);
        document_lifetime.swap(other.document_lifetime);
        std::swap(document_identity, other.document_identity); std::swap(generation, other.generation);
        std::swap(structure_epoch, other.structure_epoch);
        std::swap(input_epoch, other.input_epoch); std::swap(style_tree_epoch, other.style_tree_epoch); std::swap(contextual, other.contextual);
    }
};
inline std::unique_ptr<aether_style_map_s> aether_style_map_snapshot(const aether_style_map_s* source) {
    auto result = std::make_unique<aether_style_map_s>();
    if (!source) return result;
    result->styles.reserve(source->styles.size());
    for (const auto& entry : source->styles) {
        // Insert the canonical empty owner first: any failed copy is reclaimed.
        auto& copy = result->styles.emplace(entry.first, aether_computed_style_t{}).first->second;
        if (computed_style_deep_copy(entry.second, &copy) != AETHER_OK) throw std::bad_alloc();
    }
    result->node_lifetimes = source->node_lifetimes;
    result->measures = source->measures; result->revision = source->revision;
    result->document_lifetime = source->document_lifetime;
    result->document_identity = source->document_identity; result->generation = source->generation;
    result->input_epoch = source->input_epoch; result->style_tree_epoch = source->style_tree_epoch;
    result->structure_epoch = source->structure_epoch; result->contextual = source->contextual;
    return result;
}
inline bool aether_style_map_structure_valid(const aether_style_map_s* map) noexcept {
    return !map || !map->document_lifetime || (map->document_lifetime->alive &&
        map->document_lifetime->structure_epoch == map->structure_epoch);
}

// Building a new layout requires the exact live inputs used by this revision.
// Existing layouts do not use this gate: they own their text/control/pseudo
// snapshots and remain paintable after later nonstructural revisions.
inline bool aether_style_map_inputs_current(const aether_style_map_s* map) noexcept {
    return !map || !map->contextual || !map->document_lifetime ||
        (map->document_lifetime->alive && map->document_lifetime->mutation_epoch == map->input_epoch &&
         map->document_lifetime->style_tree_epoch == map->style_tree_epoch);
}
