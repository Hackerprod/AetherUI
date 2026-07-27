#include "margin_adjacency.h"

bool aether_sibling_margins_adjoin(const aether_margin_adjacency_facts& between) {
    return !between.has_border_or_padding_before && !between.has_border_or_padding_after &&
           !between.has_in_flow_content_before && !between.has_in_flow_content_after &&
           !between.has_clearance;
}

bool aether_parent_start_margin_adjoins_child(const aether_margin_adjacency_facts& parent) {
    return !parent.has_border_or_padding_before && !parent.has_in_flow_content_before &&
           !parent.establishes_block_formatting_context;
}

bool aether_parent_end_margin_adjoins_child(const aether_margin_adjacency_facts& parent) {
    return !parent.has_border_or_padding_after && !parent.has_in_flow_content_after &&
           !parent.establishes_block_formatting_context && !parent.has_definite_height &&
           !parent.has_positive_min_height;
}

bool aether_empty_block_margins_adjoin(const aether_margin_adjacency_facts& block) {
    return !block.has_border_or_padding_before && !block.has_border_or_padding_after &&
           !block.has_in_flow_content_before && !block.has_in_flow_content_after &&
           !block.establishes_block_formatting_context && !block.has_definite_height &&
           !block.has_positive_min_height;
}
