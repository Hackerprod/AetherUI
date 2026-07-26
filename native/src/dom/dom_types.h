#pragma once
#include <aether/aether.h>
#include "pseudo_element.h"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <cstdlib>
#include <cstring>

struct aether_node_s {
    aether_node_type_t type;
    aether_node_s* parent = nullptr;
    std::vector<aether_node_s*> children;
    std::string tag_name;
    std::map<std::string, std::string, std::less<>> attributes;
    int32_t state = 0;
    std::string text;
    int64_t style_node_id = 0;
    bool is_generated = false;
    aether_pseudo_element_t pseudo_element = aether_pseudo_element_t::none;
    // The originating element is kept separately from parent: generated nodes inherit from their
    // host, while selector combinators must navigate from the host's real DOM position.
    aether_node_s* style_origin = nullptr;
    virtual ~aether_node_s() = default;
};

struct aether_doc_s : aether_node_s {
    int64_t version = 0;
    std::unordered_map<std::string, aether_node_s*> id_index;
    int64_t next_style_node_id = 1;

    aether_doc_s() { type = AETHER_NODE_DOCUMENT; }
};
