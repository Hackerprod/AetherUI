#include "../src/css/eligibility/capture_gate.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <type_traits>

int main() {
    namespace gate = aether::style::eligibility;
    static_assert(noexcept(gate::inspect_external_capture_inputs(nullptr, nullptr, nullptr)));
    static_assert(std::is_trivially_copyable_v<gate::Result>);
    static_assert(!std::is_default_constructible_v<gate::Result>);
    static_assert(!std::is_constructible_v<gate::Result, gate::State, gate::Reason>);
    static_assert(!std::is_assignable_v<gate::Result&, gate::Result>);
    int live_marker = 42;
    // These values are never dereferenced. Inaccessible sentinels exercise the
    // contract that admission returns before touching external mutable inputs.
    const std::array<const void*, 4> values = {
        nullptr, &live_marker,
        reinterpret_cast<const void*>(std::uintptr_t{1}),
        reinterpret_cast<const void*>(std::uintptr_t{4095})
    };
    for (const auto* runtime : values)
        for (const auto* document : values)
            for (const auto* layout : values) {
                const auto result = gate::inspect_external_capture_inputs(runtime, document, layout);
                assert(result.state() == gate::State::Unknown);
                assert(result.reason() == gate::Reason::ExclusiveOwnershipUnavailable);
                assert(!result.permits_authored_input_validation());
            }
    assert(live_marker == 42);
}
