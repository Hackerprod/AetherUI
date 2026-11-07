#include "immutable_style_backing.h"
namespace aether::style {
aether_status_t freeze_backing(const BackingBuildInput& in, BackingRef& out) noexcept {
    for (const auto& [node,record] : in.entries)
        if (!node || !record || record->node()!=node) return AETHER_ERROR_INVALID_STATE;
    try {
        // Never adopt caller nodes, even from an rvalue: its iterators can survive.
        FrozenNodeIndex index;
        index.reserve(in.entries.size());
        for (const auto& [node,record] : in.entries) index.emplace(node,record);
        BackingRef result(new const ImmutableStyleBacking(std::move(index)));
        out=std::move(result); return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
}
