#pragma once
#include "persistent_index.h"
#include "../dom/node_identity.h"
#include "../diagnostics/work_counters.h"
#include <atomic>
struct aether_node_s;
namespace aether::style {
// The tree is immutable and retains authentic tokens, never a prior snapshot.
// Static and transition causes use independent trees/serials and shared lineage.
enum ContextCause : uint8_t { StaticTransform=1, StaticBackdrop=2, ActiveTransform=4 };
struct ContextOwnerEntry {
    aether_node_s* node=nullptr;
    std::shared_ptr<node_identity_s> identity;
    uint8_t causes=0;
};
struct ContextIndexObserver {
    static void allocated() noexcept { diagnostics::count_work(diagnostics::WorkCounter::context_allocations); }
    static void visited() noexcept { diagnostics::count_work(diagnostics::WorkCounter::context_index_visits); }
};
using ContextOwnerIndex=PersistentIndex<ContextOwnerEntry,ContextIndexObserver>;
struct ContextLineage {};
using ContextLineageRef=std::shared_ptr<const ContextLineage>;
struct ContextOwnersSnapshot {
    const ContextOwnerIndex owners;
    const ContextLineageRef lineage;
    const uint64_t membership_serial;
    ContextOwnersSnapshot(ContextOwnerIndex entries,ContextLineageRef source):owners(std::move(entries)),lineage(std::move(source)),membership_serial(next_serial()) {
        diagnostics::count_work(diagnostics::WorkCounter::context_allocations);
    }
private:
    static uint64_t next_serial() noexcept { static std::atomic<uint64_t> serial{0}; return ++serial; }
};
using ContextOwnersRef=std::shared_ptr<const ContextOwnersSnapshot>;
inline ContextLineageRef make_context_lineage() {
    diagnostics::count_work(diagnostics::WorkCounter::context_allocations);
    return std::make_shared<const ContextLineage>();
}
inline ContextOwnersRef publish_context_owners(const ContextOwnersRef& previous,ContextOwnerIndex owners) {
    if(previous && previous->owners.shares_root(owners)) return previous;
    return std::make_shared<const ContextOwnersSnapshot>(std::move(owners),previous?previous->lineage:make_context_lineage());
}
inline ContextOwnerIndex update_context_owner(ContextOwnerIndex index,const ContextOwnerEntry& entry) {
    const auto key=reinterpret_cast<uintptr_t>(entry.identity.get());
    const auto* old=index.find(key);
    if (!entry.causes) return index.erase(key);
    if (old && old->node==entry.node && old->identity==entry.identity && old->causes==entry.causes) return index;
    diagnostics::count_work(diagnostics::WorkCounter::context_updates);
    return index.set(key,entry);
}
}
