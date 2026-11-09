#include "state_invalidation.h"
#include "../src/dom/dom_types.h"
#include <cstdio>
int main() {
    compiled_selector_s selector;
    if (!selector_parse(".card:hover > .label", selector)) return 1;
    const auto program = aether::style::invalidation::compile(1, selector);
    if (program.occurrences.size() != 1 || program.occurrences[0].route.size() != 2) return 2;
    std::puts("C2 compiler smoke PASS (WIP)");
}
