#include "immutable_style_backing.h"
#include "../diagnostics/work_counters.h"
#include <bit>
#include "../dom/dom_types.h"
#include <algorithm>
#include <vector>
namespace aether::style {
namespace {
// Source causes are independent of attachment: staged generated nodes become
// attached only at commit. Effective eligibility is checked by the layout
// certificate/full helper, never guessed while a preview is still detached.
ContextOwnerEntry static_owner(const FrozenNodeRef& record) {
    diagnostics::count_work(diagnostics::WorkCounter::context_static_visits);
    ContextOwnerEntry entry{record->node(),record->node_lifetime(),0};
    if(!entry.identity || entry.identity->owner!=entry.node || !entry.identity->alive) return entry;
    if(entry.node->type!=AETHER_NODE_ELEMENT || entry.node->anonymous_flex_owner) return entry;
    const auto& style=*record->style()->payload_owner();
    entry.causes=(!style.transforms().empty()?StaticTransform:0)|(!style.backdrop_filters().empty()?StaticBackdrop:0);
    return entry;
}
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
        std::vector<ContextOwnerIndex::Entry> owners;
        size_t unbound=0;
        for(const auto& item:entries) {
            if(!item.second->node_lifetime()) ++unbound;
            auto entry=static_owner(item.second); if(entry.causes) append_context_entry(owners,std::move(entry));
        }
        auto context=publish_context_owners({},ContextOwnerIndex::build(std::move(owners)));
        BackingRef result(new const ImmutableStyleBacking(std::move(root),in.entries.size(),unbound,std::move(context),make_context_lineage()));
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
        size_t unbound=base ? base->unbound_context_entries_ : 0;
        auto owners=base?base->context_owners_->owners:ContextOwnerIndex{};
        auto topology=base?base->context_topology_:make_context_lineage();
        bool topology_changed=false;
        for (const auto& entry : patch) {
            const auto* old_leaf=ImmutableStyleBacking::find_leaf(root.get(),reinterpret_cast<uintptr_t>(entry.node));
            const auto previous=old_leaf && old_leaf->entry.first==entry.node?old_leaf->entry.second:FrozenNodeRef{};
            if(previous && !previous->node_lifetime()) --unbound;
            if(entry.record && !entry.record->node_lifetime()) ++unbound;
            if(!previous || !entry.record || previous->node_lifetime()!=entry.record->node_lifetime()) topology_changed=true;
            if(previous && (!entry.record || previous->node_lifetime()!=entry.record->node_lifetime()))
                owners=erase_context_owner(std::move(owners),reinterpret_cast<uintptr_t>(previous->node_lifetime().get()));
            if(entry.record) owners=update_context_owner(std::move(owners),static_owner(entry.record));
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
        BackingRef result(new const ImmutableStyleBacking(std::move(root),size,unbound,publish_context_owners(base?base->context_owners_:ContextOwnersRef{},std::move(owners)),topology_changed?make_context_lineage():topology));
        out=std::move(result); return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
}
