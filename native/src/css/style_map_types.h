#pragma once

#include <aether/aether.h>
#include <unordered_map>

struct aether_node_s;

// The legacy CSS resolver and the full StyleRuntime share this layout-facing view only.
struct aether_style_map_s {
    std::unordered_map<aether_node_s*, aether_computed_style_t> styles;

    // The map owns every stored style: each value is an independent deep copy,
    // never a view of the style runtime's states. Freeing them here keeps map
    // entries valid across state recomputation (e.g. a hover that replaces a
    // node's state and destroys its owned buffers).
    ~aether_style_map_s() {
        for (auto& entry : styles) aether_computed_style_free(&entry.second);
    }

    aether_style_map_s() = default;
    aether_style_map_s(const aether_style_map_s&) = delete;
    aether_style_map_s& operator=(const aether_style_map_s&) = delete;
    aether_style_map_s(aether_style_map_s&&) = default;
    aether_style_map_s& operator=(aether_style_map_s&&) = default;
};
