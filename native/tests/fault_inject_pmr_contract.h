#pragma once
#include "../src/css/frozen_c_string.h"
#include <memory_resource>

// Qualify the real allocator used by FrozenCString, not an OS/compiler macro.
// libc++ may use ordinary new for char alignment; libstdc++ may use aligned new.
// Counters from qualification must never satisfy runtime coverage assertions.
namespace pmr_fault_test {
struct Counters {
    size_t ordinary_calls = ordinary_test_calls;
    size_t ordinary_failures = ordinary_test_failures;
    size_t aligned_calls = aligned_test_calls;
    size_t aligned_failures = aligned_test_failures;
};
enum class Family { ordinary, aligned };
template<class Check>
Family qualify(Check check) {
    check(fail_after == -1, "PMR qualification starts outside fault scope");
    std::pmr::polymorphic_allocator<const char> allocator{std::pmr::new_delete_resource()};
    const Counters before;
    const char* bytes = allocator.allocate(41);
    std::pmr::new_delete_resource()->deallocate(const_cast<char*>(bytes), 41, alignof(char));
    const bool ordinary = ordinary_test_calls - before.ordinary_calls == 1;
    const bool aligned = aligned_test_calls - before.aligned_calls == 1;
    check(ordinary != aligned, "char PMR allocation has exactly one intercepted family");
    check(ordinary_test_calls - before.ordinary_calls + aligned_test_calls - before.aligned_calls == 1,
          "char PMR allocation is directly observable");
    const Family family = aligned ? Family::aligned : Family::ordinary;
    bool threw = false;
    fault_delivered = false; fail_after = 0;
    try { bytes = allocator.allocate(41); std::pmr::new_delete_resource()->deallocate(const_cast<char*>(bytes), 41, alignof(char)); }
    catch (const std::bad_alloc&) { threw = true; }
    fail_after = -1;
    check(threw && fault_delivered, "actual char PMR allocation propagates injected OOM");

    // Exercise the production Storage constructor and payload, not a stand-in
    // resource. Its two allocations are shared ownership and the const array.
    // A partial construction must release ownership and preserve existing pins.
    const aether::style::FrozenCString retained("retained before payload failure");
    const auto* pin = retained.c_str();
    unsigned failures = 0;
    bool completed = false, payload_failed = false;
    for (long index = 0; index < 3; ++index) {
        const Counters attempt;
        threw = false; fault_delivered = false; fail_after = index;
        try {
            const aether::style::FrozenCString candidate("candidate frozen payload");
            fail_after = -1;
            check(candidate.view() == "candidate frozen payload", "frozen payload retry preserves bytes");
            check(candidate.c_str()[candidate.size()] == '\0', "frozen payload retry terminates array");
            completed = true;
        } catch (const std::bad_alloc&) { fail_after = -1; threw = true; }
        if (threw) {
            ++failures;
            check(fault_delivered, "frozen string OOM came from fault harness");
            if (index == 1) {
                payload_failed = family == Family::aligned
                    ? aligned_test_failures == attempt.aligned_failures + 1
                    : ordinary_test_failures == attempt.ordinary_failures + 1;
            }
        } else check(!fault_delivered, "frozen string did not swallow injected OOM");
        check(retained.c_str() == pin && retained.view() == "retained before payload failure",
              "frozen payload failure preserves retained storage");
        if (completed) break;
    }
    check(completed && failures == 2 && payload_failed,
          "frozen string ownership and real PMR payload both fail before clean retry");
    std::printf("char PMR fault contract: %s new, ownership and payload OOM verified\n",
                aligned ? "aligned" : "ordinary");
    return family;
}
template<class Check>
void require_sweep_coverage(Family family, const Counters& before, Check check) {
    const bool reached = family == Family::aligned
        ? aligned_test_calls > before.aligned_calls && aligned_test_failures > before.aligned_failures
        : ordinary_test_calls > before.ordinary_calls && ordinary_test_failures > before.ordinary_failures;
    check(reached, "runtime sweep reaches and fails the actual char PMR allocation family");
    std::printf("runtime allocation coverage: ordinary calls=%zu failures=%zu, aligned calls=%zu failures=%zu\n",
                ordinary_test_calls - before.ordinary_calls, ordinary_test_failures - before.ordinary_failures,
                aligned_test_calls - before.aligned_calls, aligned_test_failures - before.aligned_failures);
}
}
