#pragma once

// Private C++ admission barrier. This is not a CSS compatibility certificate.
// No external mutable runtime/DOM is inspected without an ownership mechanism.
namespace aether::style::eligibility {

enum class State { Unknown, Unsupported, Eligible };
enum class Reason { ExclusiveOwnershipUnavailable };

class Result final {
    const State state_;
    const Reason reason_;
    Result(State state, Reason reason) noexcept : state_(state), reason_(reason) {}
    friend Result inspect_external_capture_inputs(const void*, const void*, const void*) noexcept;
public:
    State state() const noexcept { return state_; }
    Reason reason() const noexcept { return reason_; }
    // No public constructor/setter or caller-controlled flag can issue Eligible.
    bool permits_authored_input_validation() const noexcept {
        return state_ == State::Eligible;
    }
};

// Existing handles are deliberately opaque. This function must not dereference
// them, read epochs, invoke callbacks, or try to acquire lifetime from raw memory.
// There is no caller-provided "exclusive" flag: such a flag proves nothing.
// G1.2 has no Eligible path. A future ownership design needs separate review.
[[nodiscard]] Result inspect_external_capture_inputs(
    const void* runtime, const void* document, const void* layout) noexcept;

} // namespace aether::style::eligibility
