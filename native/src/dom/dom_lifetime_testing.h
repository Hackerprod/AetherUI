#pragma once
// Test link-unit only. Counts object instances, not allocation bytes, shared_ptr
// control blocks or arbitrary memory. Normal product has no member or ledger.
#if defined(AETHER_DOM_LIFETIME_TESTING)
#include <array>
#include <atomic>
#include <cstdint>
namespace aether::dom::lifetime_test {
enum Family : unsigned { node, storage, legacy_cell, owner_control, owner_relay,
    identity, interaction_registry, interaction_claim, family_count };
struct Ledger {
    std::array<std::atomic<uint64_t>,family_count> live{};
    std::atomic<bool> invalid{false};
};
inline Ledger ledger;
inline void add(Family family) noexcept {
    auto& counter=ledger.live[family];auto value=counter.load(std::memory_order_relaxed);
    for(;;){
        if(value==UINT64_MAX){ledger.invalid.store(true,std::memory_order_relaxed);return;}
        if(counter.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return;
    }
}
inline void remove(Family family) noexcept {
    auto& counter=ledger.live[family];auto value=counter.load(std::memory_order_relaxed);
    for(;;){
        if(!value){ledger.invalid.store(true,std::memory_order_relaxed);return;}
        if(counter.compare_exchange_weak(value,value-1,std::memory_order_relaxed))return;
    }
}
template<Family F> struct Instance {
    Instance() noexcept {add(F);}
    Instance(const Instance&) noexcept {add(F);}
    Instance(Instance&&) noexcept {add(F);}
    Instance& operator=(const Instance&) noexcept {return *this;}
    Instance& operator=(Instance&&) noexcept {return *this;}
    ~Instance(){remove(F);}
};
struct Snapshot {
    std::array<uint64_t,family_count> live{};
    bool valid=false;
    bool operator==(const Snapshot&) const = default;
};
// Call only with all participating threads joined and operations quiescent.
// A non-atomic vector of atomic loads is not a concurrent snapshot certificate.
inline Snapshot snapshot() noexcept {
    Snapshot result;
    for(unsigned i=0;i<family_count;++i)result.live[i]=ledger.live[i].load(std::memory_order_relaxed);
    result.valid=!ledger.invalid.load(std::memory_order_relaxed);return result;
}
}
#define AETHER_DOM_COUNT_INSTANCE(family) aether::dom::lifetime_test::Instance<aether::dom::lifetime_test::family> lifetime_test_instance_;
#else
#define AETHER_DOM_COUNT_INSTANCE(family)
#endif
