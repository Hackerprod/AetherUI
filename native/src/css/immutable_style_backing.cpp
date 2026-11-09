#include "immutable_style_backing.h"
#include "../diagnostics/work_counters.h"
#include <bit>
#include <algorithm>
#include <vector>
namespace aether::style {
namespace {
bool same_record_owner(const FrozenNodeRef& a, const FrozenNodeRef& b) noexcept {
    return a==b && !a.owner_before(b) && !b.owner_before(a);
}
}
ImmutableStyleBacking::NodeRef ImmutableStyleBacking::build_sorted(std::span<const Entry> entries) {
    if (entries.empty()) return {};
    if (entries.size()==1) return std::make_shared<const Node>(entries.front());
    const uintptr_t difference=reinterpret_cast<uintptr_t>(entries.front().first) ^ reinterpret_cast<uintptr_t>(entries.back().first);
    const unsigned bit=std::bit_width(difference)-1;
    const auto middle=std::partition_point(entries.begin(),entries.end(),[&](const Entry& entry) {
        return !one(reinterpret_cast<uintptr_t>(entry.first),bit);
    });
    const size_t split=static_cast<size_t>(middle-entries.begin());
    return std::make_shared<const Node>(bit,build_sorted(entries.first(split)),build_sorted(entries.subspan(split)));
}
ImmutableStyleBacking::NodeRef ImmutableStyleBacking::replace(const NodeRef& node, uintptr_t key, const FrozenNodeRef& record) {
    if (node->leaf()) {
        if (same_record_owner(node->entry.second,record)) return node;
        return std::make_shared<const Node>(Entry{node->entry.first,record});
    }
    if (one(key,node->bit)) {
        auto next=replace(node->one,key,record);
        if (next==node->one) return node;
        return std::make_shared<const Node>(node->bit,node->zero,std::move(next));
    }
    auto next=replace(node->zero,key,record);
    if (next==node->zero) return node;
    return std::make_shared<const Node>(node->bit,std::move(next),node->one);
}
ImmutableStyleBacking::NodeRef ImmutableStyleBacking::insert_at(const NodeRef& node, uintptr_t key, unsigned bit, const NodeRef& leaf) {
    if (node->leaf() || node->bit<bit)
        return one(key,bit) ? std::make_shared<const Node>(bit,node,leaf) : std::make_shared<const Node>(bit,leaf,node);
    if (one(key,node->bit)) return std::make_shared<const Node>(node->bit,node->zero,insert_at(node->one,key,bit,leaf));
    return std::make_shared<const Node>(node->bit,insert_at(node->zero,key,bit,leaf),node->one);
}
ImmutableStyleBacking::NodeRef ImmutableStyleBacking::upsert(const NodeRef& node, const Entry& entry, bool& added) {
    const uintptr_t key=reinterpret_cast<uintptr_t>(entry.first);
    const auto* found=find_leaf(node.get(),key);
    if (found && found->entry.first==entry.first) return replace(node,key,entry.second);
    added=true;
    auto leaf=std::make_shared<const Node>(entry);
    if (!found) return leaf;
    const uintptr_t difference=key ^ reinterpret_cast<uintptr_t>(found->entry.first);
    const unsigned bit=std::bit_width(difference)-1;
    return insert_at(node,key,bit,leaf);
}
ImmutableStyleBacking::NodeRef ImmutableStyleBacking::erase(const NodeRef& node, uintptr_t key, bool& removed) {
    if (!node) return node;
    if (node->leaf()) {
        if (reinterpret_cast<uintptr_t>(node->entry.first)!=key) return node;
        removed=true; return {};
    }
    if (one(key,node->bit)) {
        auto next=erase(node->one,key,removed);
        if (!removed) return node;
        if (!next) return node->zero;
        return std::make_shared<const Node>(node->bit,node->zero,std::move(next));
    }
    auto next=erase(node->zero,key,removed);
    if (!removed) return node;
    if (!next) return node->one;
    return std::make_shared<const Node>(node->bit,std::move(next),node->one);
}
aether_status_t freeze_backing(const BackingBuildInput& in, BackingRef& out) noexcept {
    for (const auto& [node,record] : in.entries)
        if (!node || !record || record->node()!=node) return AETHER_ERROR_INVALID_STATE;
    try {
        // Import validates every entry and never adopts mutable caller storage.
        // Full imports build each tree node once, rather than doing N persistent
        // updates to a temporary tree. Only delta patches pay for copied paths.
        std::vector<ImmutableStyleBacking::Entry> entries;
        entries.reserve(in.entries.size());
        for (const auto& entry : in.entries) entries.emplace_back(entry.first,entry.second);
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) {
            return reinterpret_cast<uintptr_t>(a.first)<reinterpret_cast<uintptr_t>(b.first);
        });
        auto root=ImmutableStyleBacking::build_sorted(entries);
        BackingRef result(new const ImmutableStyleBacking(std::move(root),in.entries.size()));
        out=std::move(result); return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
aether_status_t patch_backing(const BackingRef& base, std::span<const BackingPatchEntry> patch, BackingRef& out) noexcept {
    // Only private-factory backings and factory-frozen records can enter. The
    // original full import validated all reused leaves; only changed entries
    // need certification here. No base revision/history ownership is retained.
    for (const auto& entry : patch)
        if (!entry.node || (entry.record && entry.record->node()!=entry.node)) return AETHER_ERROR_INVALID_STATE;
    try {
        auto root=base ? base->root_ : ImmutableStyleBacking::NodeRef{};
        size_t size=base ? base->size_ : 0;
        for (const auto& entry : patch) {
            bool changed=false;
            if (entry.record) {
                const auto* old=ImmutableStyleBacking::find_leaf(root.get(),reinterpret_cast<uintptr_t>(entry.node));
                const bool copied=!old || old->entry.first!=entry.node || !same_record_owner(old->entry.second,entry.record);
                root=ImmutableStyleBacking::upsert(root,{entry.node,entry.record},changed);
                if (changed) ++size;
                if (copied) diagnostics::count_work(diagnostics::WorkCounter::map_entries_copied);
            } else {
                root=ImmutableStyleBacking::erase(root,reinterpret_cast<uintptr_t>(entry.node),changed);
                if (changed) --size;
            }
        }
        if (base && root==base->root_) { out=base; return AETHER_OK; }
        BackingRef result(new const ImmutableStyleBacking(std::move(root),size));
        out=std::move(result); return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
}
