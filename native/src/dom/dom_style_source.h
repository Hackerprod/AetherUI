#pragma once
#include "dom_types.h"
// Exact native DOM source, no serialization, diagnostics, or user callbacks.
// On failure the output is unchanged. Generated content is excluded.
aether_status_t aether_dom_collect_style_source(aether_node_s* root,std::string& output) noexcept;
