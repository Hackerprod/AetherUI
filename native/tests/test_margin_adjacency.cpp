#include "../src/layout/margin_adjacency.h"
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) { if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }

int main() {
    aether_margin_adjacency_facts plain{};
    require(aether_sibling_margins_adjoin(plain), "adjacent normal-flow block siblings must collapse");
    require(aether_parent_start_margin_adjoins_child(plain), "parent top and first child must collapse without a separator");
    require(aether_parent_end_margin_adjoins_child(plain), "auto-height parent bottom and last child must collapse without a separator");
    require(aether_empty_block_margins_adjoin(plain), "empty auto-height block margins must collapse through the block");

    auto facts = plain; facts.has_in_flow_content_before = true;
    require(!aether_sibling_margins_adjoin(facts), "an inline line between siblings must inhibit collapse");
    require(!aether_parent_start_margin_adjoins_child(facts), "parent content before first child must inhibit start collapse");
    facts = plain; facts.has_border_or_padding_before = true;
    require(!aether_parent_start_margin_adjoins_child(facts), "parent border/padding before child must inhibit start collapse");
    facts = plain; facts.has_border_or_padding_after = true;
    require(!aether_parent_end_margin_adjoins_child(facts), "parent border/padding after child must inhibit end collapse");
    facts = plain; facts.has_definite_height = true;
    require(!aether_parent_end_margin_adjoins_child(facts) && !aether_empty_block_margins_adjoin(facts), "definite height must inhibit end and empty-block collapse");
    facts = plain; facts.establishes_block_formatting_context = true;
    require(aether_sibling_margins_adjoin(facts), "a sibling BFC still participates in the parent block flow");
    require(!aether_parent_start_margin_adjoins_child(facts) && !aether_parent_end_margin_adjoins_child(facts), "new formatting context must inhibit parent-child collapse");
    facts = plain; facts.has_clearance = true;
    require(!aether_sibling_margins_adjoin(facts), "clearance must inhibit sibling collapse");
    return 0;
}
