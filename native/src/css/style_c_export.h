#pragma once
#include "style_read_view.h"
#include <memory>
#include <vector>

namespace aether::style {
// Owns a complete C-compatible copy for exactly one callback invocation. The
// callback may edit/rebind nested descriptors, but may not free or retain them.
// Destruction visits only the private original-allocation ledger.
class CallbackStyleCopy final {
    aether_computed_style_t value_{};
    std::vector<void*> allocations_;
    CallbackStyleCopy() = default;
    void* allocate(size_t bytes);
    template<class T> T* copy(ReadOnlyArrayView<T> source);
    aether_linear_gradient copy_gradient(GradientReadView source);
    void populate(ComputedStyleReadView source);
    friend aether_status_t export_owned_flat(ComputedStyleReadView, aether_computed_style_t*) noexcept;
public:
    CallbackStyleCopy(const CallbackStyleCopy&) = delete;
    CallbackStyleCopy& operator=(const CallbackStyleCopy&) = delete;
    ~CallbackStyleCopy() noexcept;
    const aether_computed_style_t* get() const noexcept { return &value_; }
    static aether_status_t create(ComputedStyleReadView source,
        std::unique_ptr<CallbackStyleCopy>& output) noexcept;
};
// On failure *output is untouched. On success caller receives independent
// malloc-owned buffers, freed with aether_computed_style_free. As with the C
// copy API, output must not hold an unreleased prior allocation on success.
aether_status_t export_owned_flat(ComputedStyleReadView source,
    aether_computed_style_t* output) noexcept;
} // namespace aether::style
