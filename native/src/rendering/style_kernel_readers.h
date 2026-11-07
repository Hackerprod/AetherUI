#pragma once

#include "../css/style_read_view.h"
#include <span>

namespace aether::rendering {

// Borrowed readonly input for kernels shared by genuine C-array owners and
// Frozen payloads. A Frozen range never acquires a contiguous-array interface.
template<class T> class IndexedReadRange {
    std::span<const T> flat_{};
    style::ReadOnlyArrayView<T> frozen_{};
    bool is_frozen_ = false;
public:
    IndexedReadRange() noexcept = default;
    explicit IndexedReadRange(std::span<const T> flat) noexcept : flat_(flat) {}
    explicit IndexedReadRange(style::ReadOnlyArrayView<T> frozen) noexcept
        : frozen_(frozen), is_frozen_(true) {}
    size_t size() const noexcept { return is_frozen_ ? frozen_.size() : flat_.size(); }
    bool empty() const noexcept { return size() == 0; }
    const T& operator[](size_t index) const noexcept {
        assert(index < size());
        return is_frozen_ ? frozen_[index] : flat_[index];
    }
};

struct GradientKernelReadView {
    float direction_x = 0, direction_y = 0;
    IndexedReadRange<aether_gradient_stop> stops;
    explicit GradientKernelReadView(style::GradientReadView source) noexcept
        : direction_x(source.direction_x), direction_y(source.direction_y), stops(source.stops) {}
    GradientKernelReadView(float x, float y, std::span<const aether_gradient_stop> values) noexcept
        : direction_x(x), direction_y(y), stops(values) {}
};

} // namespace aether::rendering
