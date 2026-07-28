#include "layout_types.h"
#include "../css/style_map_types.h"
#include "../dom/dom_types.h"

aether_layout_result_s::~aether_layout_result_s() {
    delete root;
    assert(live_box_count == 0 && "all layout boxes must be owned by the final tree or released explicitly");
    if (layout_only_style_map) {
        for (auto* node : layout_only_style_nodes) {
            const auto it = layout_only_style_map->styles.find(node);
            if (it == layout_only_style_map->styles.end()) continue;
            aether_computed_style_free(&it->second);
            layout_only_style_map->styles.erase(it);
        }
    }
    for (auto* node : layout_only_nodes) delete node;
}

aether_layout_box_s::~aether_layout_box_s() {
    // Unpublish before recursively freeing descendants: a re-layout may delete
    // a previously published subtree while the result remains live.
    if (owner) owner->unregister_box(this);
    for (auto* child : children) delete child;
    if (owner) owner->release_box();
}

aether_layout_box_s* aether_layout_result_s::create_box() {
    auto* box = new aether_layout_box_s();
    box->owner = this;
    ++live_box_count;
    return box;
}
