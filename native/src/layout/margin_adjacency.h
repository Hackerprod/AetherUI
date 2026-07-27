#pragma once

/* CSS 2.1 §8.3.1 margin collapsing eligibility. These predicates deliberately
   separate tree facts from the arithmetic in box_model. */
struct aether_margin_adjacency_facts {
    bool has_border_or_padding_before = false;
    bool has_border_or_padding_after = false;
    bool has_in_flow_content_before = false;
    bool has_in_flow_content_after = false;
    bool establishes_block_formatting_context = false;
    bool has_clearance = false;
    bool has_definite_height = false;
    bool has_positive_min_height = false;
};

bool aether_sibling_margins_adjoin(const aether_margin_adjacency_facts& between);
bool aether_parent_start_margin_adjoins_child(const aether_margin_adjacency_facts& parent);
bool aether_parent_end_margin_adjoins_child(const aether_margin_adjacency_facts& parent);
bool aether_empty_block_margins_adjoin(const aether_margin_adjacency_facts& block);
