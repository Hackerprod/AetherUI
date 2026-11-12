#include "capture_gate.h"

namespace aether::style::eligibility {
Result inspect_external_capture_inputs(
    const void*, const void*, const void*) noexcept {
    // Fail closed BEFORE inspecting any caller-owned mutable object. An epoch
    // check after an unsynchronized read would not establish exclusivity.
    return {State::Unknown, Reason::ExclusiveOwnershipUnavailable};
}
} // namespace aether::style::eligibility
