#pragma once

#include <aether/aether.h>
#include <unordered_map>

struct aether_node_s;

// The legacy CSS resolver and the full StyleRuntime share this layout-facing view only.
struct aether_style_map_s {
    std::unordered_map<aether_node_s*, aether_computed_style_t> styles;
};
