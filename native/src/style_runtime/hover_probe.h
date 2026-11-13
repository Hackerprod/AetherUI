#pragma once
// Private, opt-in diagnostics. No public ABI, heap, callbacks or synchronization.
#ifndef AETHER_HOVER_PROBE
#define AETHER_HOVER_PROBE 0
#endif
#include <array>
#include <cstdint>
#if AETHER_HOVER_PROBE == 2
#include <chrono>
#endif
namespace aether::hover_probe {
enum Phase { preparation, matching, cascade, freeze, backing, updates, publication, replay, cleanup, commit_total, count };
struct Snapshot {
 std::array<uint64_t,count> ns{};
 uint64_t compute_original{}, compute_pseudo{}, candidates{}, matcher_calls{}, inline_compiles{};
 uint64_t frozen_records{}, revision_entries{}, retired_entries{}, update_copies{};
};
#if AETHER_HOVER_PROBE
inline thread_local Snapshot data{};
inline void reset() noexcept { data={}; }
inline Snapshot snapshot() noexcept { return data; }
#define AETHER_HP_ADD(field,n) (::aether::hover_probe::data.field += (n))
#else
inline void reset() noexcept {}
inline Snapshot snapshot() noexcept { return {}; }
#define AETHER_HP_ADD(field,n) ((void)0)
#endif
class Scope {
#if AETHER_HOVER_PROBE == 2
 using Clock=std::chrono::steady_clock;
 Phase phase_; Clock::time_point start_; bool active_=true;
#endif
public:
 explicit Scope(Phase p) noexcept {
#if AETHER_HOVER_PROBE == 2
 phase_=p; start_=Clock::now();
#else
 (void)p;
#endif
 }
 void stop() noexcept {
#if AETHER_HOVER_PROBE == 2
 if(active_) {data.ns[phase_]+=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start_).count();active_=false;}
#endif
 }
 void resume() noexcept {
#if AETHER_HOVER_PROBE == 2
 start_=Clock::now();active_=true;
#endif
 }
 ~Scope(){stop();}
 Scope(const Scope&)=delete;
};
}
