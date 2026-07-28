#include "layout_types.h"

#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    aether_layout_result_s result{};
    auto* root = result.create_box();
    auto* child = result.create_box();
    auto* grandchild = result.create_box();
    root->add_child(child);
    child->add_child(grandchild);
    result.root = root;

    require(result.live_box_count == 3,
        "the ownership counter must include every allocated layout box");
    require(root->descendants_and_self() == 3,
        "the fixture must form a three-node layout tree");

    delete result.root;
    result.root = nullptr;
    require(result.live_box_count == 0,
        "recursive layout-box destruction must release every descendant exactly once");
    return 0;
}
