#pragma once
#include "style_read_view.h"
#include <vector>

namespace aether::style {
namespace export_detail {
class OwnedFlatExport;
// Original allocations, never the mutable descriptors, determine destruction.
class MallocAllocationLedger final {
    std::vector<void*> allocations_;
    void disarm() noexcept { allocations_.clear(); }
    friend class OwnedFlatExport;
public:
    MallocAllocationLedger() noexcept = default;
    ~MallocAllocationLedger() noexcept { clear_and_free(); }
    MallocAllocationLedger(const MallocAllocationLedger&) = delete;
    MallocAllocationLedger& operator=(const MallocAllocationLedger&) = delete;
    void* allocate(size_t bytes);
    void clear_and_free() noexcept;
};
// Checked independently of any view, before allocation or reading elements.
int32_t abi_count(size_t count);
size_t array_bytes(size_t count, size_t element_size);
size_t string_bytes(size_t length);
}
// One caller-owned frame per callback invocation. Nested descriptors may be
// edited/rebound, but their buffers must not be freed or retained by callbacks.
class CallbackStyleCopy final {
    aether_computed_style_t value_{};
    export_detail::MallocAllocationLedger allocations_;
    bool ready_ = false;
public:
    CallbackStyleCopy() noexcept = default;
    ~CallbackStyleCopy() noexcept = default;
    CallbackStyleCopy(const CallbackStyleCopy&) = delete;
    CallbackStyleCopy& operator=(const CallbackStyleCopy&) = delete;
    CallbackStyleCopy(CallbackStyleCopy&&) = delete;
    CallbackStyleCopy& operator=(CallbackStyleCopy&&) = delete;
    // Failure publishes no partial loan and permits retry. Preparing a ready
    // frame rejects without changing its existing loan.
    aether_status_t prepare(ComputedStyleReadView source) noexcept;
    const aether_computed_style_t* get() const noexcept { return ready_ ? &value_ : nullptr; }
};
// On failure *output is untouched. On success caller receives independent
// malloc-owned buffers, freed with aether_computed_style_free. As with the C
// copy API, output must not hold an unreleased prior allocation on success.
aether_status_t export_owned_flat(ComputedStyleReadView source,
    aether_computed_style_t* output) noexcept;
} // namespace aether::style
