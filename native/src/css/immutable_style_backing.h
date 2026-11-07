#pragma once
#include "frozen_node_record.h"
#include <unordered_map>
namespace aether::style {
using FrozenNodeIndex = std::unordered_map<const aether_node_s*, FrozenNodeRef>;
struct BackingBuildInput { FrozenNodeIndex entries; };
class ImmutableStyleBacking;
using BackingRef = std::shared_ptr<const ImmutableStyleBacking>;
aether_status_t freeze_backing(const BackingBuildInput&, BackingRef&) noexcept;
class ImmutableStyleBacking {
    const FrozenNodeIndex entries_;
    explicit ImmutableStyleBacking(FrozenNodeIndex&& index) : entries_(std::move(index)) {}
    friend aether_status_t freeze_backing(const BackingBuildInput&, BackingRef&) noexcept;
public:
    size_t size() const noexcept { return entries_.size(); }
    const FrozenNodeRecord* find(const aether_node_s* node) const noexcept {
        auto it=entries_.find(node); return it==entries_.end() ? nullptr : it->second.get();
    }
    FrozenNodeRef find_owner(const aether_node_s* node) const noexcept {
        auto it=entries_.find(node); return it==entries_.end() ? FrozenNodeRef{} : it->second;
    }
    // Enumeration is intentionally hash order, never a revision manifest.
    const FrozenNodeIndex& entries() const noexcept { return entries_; }
};
}
