#include "layout_types.h"

aether_layout_box_s::~aether_layout_box_s() {
    for (auto* child : children) delete child;
    if (owner) owner->release_box();
}

aether_layout_box_s* aether_layout_result_s::create_box() {
    auto* box = new aether_layout_box_s();
    box->owner = this;
    ++live_box_count;
    return box;
}
