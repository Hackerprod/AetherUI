#include "style_c_export.h"
#include "frozen_style.h"
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace aether::style {
namespace export_detail {
int32_t abi_count(size_t count) {
    if (count > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
        throw std::length_error("style ABI count overflow");
    return static_cast<int32_t>(count);
}
size_t array_bytes(size_t count, size_t element_size) {
    abi_count(count);
    if (element_size && count > std::numeric_limits<size_t>::max() / element_size)
        throw std::length_error("style export size overflow");
    return count * element_size;
}
size_t string_bytes(size_t length) {
    if (length == std::numeric_limits<size_t>::max())
        throw std::length_error("image source export size overflow");
    return length + 1;
}
void* MallocAllocationLedger::allocate(size_t bytes) {
    if (!bytes) return nullptr;
    void* allocation = std::malloc(bytes);
    if (!allocation) throw std::bad_alloc();
    try { allocations_.push_back(allocation); }
    catch (...) { std::free(allocation); throw; }
    return allocation;
}
namespace {
template<class T> T* copy(ReadOnlyArrayView<T> source, MallocAllocationLedger& allocations) {
    static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);
    static_assert(alignof(T) <= alignof(std::max_align_t));
    // malloc implicitly creates the array and its implicit-lifetime C leaves.
    auto* result = static_cast<T*>(allocations.allocate(array_bytes(source.size(), sizeof(T))));
    for (size_t i = 0; i < source.size(); ++i) result[i] = source[i];
    return result;
}
aether_linear_gradient copy_gradient(GradientReadView source, MallocAllocationLedger& allocations) {
    aether_linear_gradient result{};
    result.direction_x = source.direction_x;
    result.direction_y = source.direction_y;
    result.stop_count = abi_count(source.stops.size());
    result.stops = copy(source.stops, allocations);
    return result;
}
void populate_flat(ComputedStyleReadView source, aether_computed_style_t& value_, MallocAllocationLedger& allocations) {
    source.scalars().copy_to(value_);
    const auto backgrounds = source.background_layers();
    value_.background_layers.count = abi_count(backgrounds.size());
    static_assert(alignof(aether_background_layer) <= alignof(std::max_align_t));
    value_.background_layers.items = static_cast<aether_background_layer*>(
        allocations.allocate(array_bytes(backgrounds.size(), sizeof(aether_background_layer))));
    for (size_t i = 0; i < backgrounds.size(); ++i) {
        const auto source_layer = backgrounds[i].view();
        auto& layer = value_.background_layers.items[i];
        layer = {};
        source_layer.scalars.copy_to(layer);
        layer.gradient = copy_gradient(source_layer.gradient, allocations);
        const auto text = source_layer.image_source;
        if (text.present()) {
            layer.image_source = static_cast<char*>(allocations.allocate(string_bytes(text.size())));
            for (size_t j = 0; j < text.size(); ++j) layer.image_source[j] = text[j];
            layer.image_source[text.size()] = '\0';
        }
    }
    value_.backdrop_filters = {copy(source.backdrop_filters(), allocations), abi_count(source.backdrop_filters().size())};
    value_.text_gradient = copy_gradient(source.text_gradient(), allocations);
    value_.transforms = {copy(source.transforms(), allocations), abi_count(source.transforms().size())};
    value_.text_shadows = {copy(source.text_shadows(), allocations), abi_count(source.text_shadows().size())};
    value_.box_shadows = {copy(source.box_shadows(), allocations), abi_count(source.box_shadows().size())};
}
} // namespace
void MallocAllocationLedger::clear_and_free() noexcept {
    for (void* allocation : allocations_) std::free(allocation);
    allocations_.clear();
}
// Owned export has no access to a frame already lent to a callback.
class OwnedFlatExport final {
    aether_computed_style_t value_{};
    MallocAllocationLedger allocations_;
public:
    OwnedFlatExport() noexcept = default;
    void populate(ComputedStyleReadView source) { populate_flat(source, value_, allocations_); }
    void transfer_to(aether_computed_style_t& output) noexcept {
        output = value_;
        allocations_.disarm();
    }
};
} // namespace export_detail

aether_status_t CallbackStyleCopy::prepare(ComputedStyleReadView source) noexcept {
    if (ready_) return AETHER_ERROR_INVALID_STATE;
    aether_status_t status;
    try {
        export_detail::populate_flat(source, value_, allocations_);
        ready_ = true;
        return AETHER_OK;
    } catch (const std::bad_alloc&) { status = AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { status = AETHER_ERROR_INVALID_STATE; }
    allocations_.clear_and_free();
    value_ = {};
    return status;
}
aether_status_t export_owned_flat(ComputedStyleReadView source, aether_computed_style_t* output) noexcept {
    if (!output) return AETHER_ERROR_NULL_ARG;
    try {
        export_detail::OwnedFlatExport prepared;
        prepared.populate(source);
        prepared.transfer_to(*output);
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
} // namespace aether::style
