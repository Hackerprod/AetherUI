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

    // Model a published flex-item subtree containing text fragments. Text boxes
    // are paintable but deliberately have no primary-box map entry.
    auto* published = result.create_box();
    auto* text_fragment = result.create_box();
    const auto element = reinterpret_cast<aether_node_s*>(static_cast<uintptr_t>(1));
    published->element = element;
    text_fragment->element = element;
    published->add_child(text_fragment);
    result.primary_boxes.emplace(element, published);
    result.paint_order.push_back(published);
    result.paint_order.push_back(text_fragment);

    // A replacement may already be published when an obsolete box is destroyed;
    // unregistering the old instance must never erase the replacement.
    auto* replacement = result.create_box();
    replacement->element = element;
    result.primary_boxes[element] = replacement;
    result.paint_order.push_back(replacement);
    delete published;
    require(result.paint_order.size() == 1 && result.paint_order.front() == replacement &&
            result.find_primary_box(element) == replacement,
        "destroying a published subtree must remove stale element and text entries without unpublishing its replacement");

    delete replacement;
    require(result.paint_order.empty() && result.find_primary_box(element) == nullptr,
        "destroying the replacement must remove its final paint and primary-box entries");

    delete result.root;
    result.root = nullptr;
    require(result.live_box_count == 0,
        "recursive layout-box destruction must release every descendant exactly once");
    return 0;
}
