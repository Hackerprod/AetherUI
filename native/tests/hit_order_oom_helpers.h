#pragma once
#include "../src/layout/layout_types.h"
// Optional hit-order allocation failure may publish a complete uncached layout,
// or stable_sort may recover its nothrow temporary allocation in place.
// Compare every reachable box's geometry, not only the prior result's survival.
inline bool hit_oom_same_rect(aether_rectf a, aether_rectf b) {
    return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;
}
inline bool hit_oom_same_tree(const aether_layout_box_s* a, const aether_layout_box_s* b) {
    if (!a || !b) return a==b;
    if (a->kind!=b->kind || a->children.size()!=b->children.size() ||
        !hit_oom_same_rect(a->border_box,b->border_box) || !hit_oom_same_rect(a->padding_box,b->padding_box) ||
        !hit_oom_same_rect(a->content_box,b->content_box) || !hit_oom_same_rect(a->margin_box,b->margin_box)) return false;
    for(size_t i=0;i<a->children.size();++i) if(!hit_oom_same_tree(a->children[i],b->children[i])) return false;
    return true;
}
inline bool hit_oom_optional_success(const aether_layout_result_s* output, const aether_layout_result_s* baseline) {
    if (!output || output==baseline || !hit_oom_same_tree(output->root,baseline->root)) return false;
    if (output->hit_order_cache) {
        StyleLookup styles(output->assets,{},false);
        // A retained table must still be exactly equivalent to fresh classification.
        for (const auto& entry : output->hit_order_cache->entries) {
            auto expected=layout_children_paint_order(entry.first,styles,BoxStylePolicy::snapshot);
            if(expected.size()!=entry.second.size()) return false;
            for(size_t i=0;i<expected.size();++i)
                if(expected[i].dom_index!=entry.second[i].dom_index) return false;
        }
        if(output->hit_order_cache->entries.size()!=size_t(output->root->descendants_and_self())) return false;
    }
    return true;
}
