#include "style_c_export.h"
#include "frozen_style.h"
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace aether::style {
namespace {
int32_t abi_count(size_t count) {
    if (count > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
        throw std::length_error("style ABI count overflow");
    return static_cast<int32_t>(count);
}
}
void* CallbackStyleCopy::allocate(size_t bytes) {
    if (!bytes) return nullptr;
    void* allocation = std::malloc(bytes);
    if (!allocation) throw std::bad_alloc();
    try { allocations_.push_back(allocation); }
    catch (...) { std::free(allocation); throw; }
    return allocation;
}
template<class T> T* CallbackStyleCopy::copy(ReadOnlyArrayView<T> source) {
    static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);
    abi_count(source.size());
    if (source.size() > std::numeric_limits<size_t>::max() / sizeof(T))
        throw std::length_error("style export size overflow");
    // malloc implicitly creates the array and its implicit-lifetime C leaves.
    auto* result = static_cast<T*>(allocate(source.size() * sizeof(T)));
    for (size_t i = 0; i < source.size(); ++i) result[i] = source[i];
    return result;
}
aether_linear_gradient CallbackStyleCopy::copy_gradient(GradientReadView source) {
    aether_linear_gradient result{};
    result.direction_x = source.direction_x;
    result.direction_y = source.direction_y;
    result.stop_count = abi_count(source.stops.size());
    result.stops = copy(source.stops);
    return result;
}
void CallbackStyleCopy::populate(ComputedStyleReadView source) {
    source.scalars().copy_to(value_);
    const auto backgrounds = source.background_layers();
    value_.background_layers.count = abi_count(backgrounds.size());
    if (backgrounds.size() > std::numeric_limits<size_t>::max() / sizeof(aether_background_layer))
        throw std::length_error("background export size overflow");
    value_.background_layers.items = static_cast<aether_background_layer*>(
        allocate(backgrounds.size() * sizeof(aether_background_layer)));
    for (size_t i = 0; i < backgrounds.size(); ++i) {
        const auto source_layer = backgrounds[i].view();
        auto& layer = value_.background_layers.items[i];
        layer = {};
        source_layer.scalars.copy_to(layer);
        layer.gradient = copy_gradient(source_layer.gradient);
        const auto text = source_layer.image_source;
        if (text.present()) {
            if (text.size() == std::numeric_limits<size_t>::max())
                throw std::length_error("image source export size overflow");
            layer.image_source = static_cast<char*>(allocate(text.size() + 1));
            for (size_t j = 0; j < text.size(); ++j) layer.image_source[j] = text[j];
            layer.image_source[text.size()] = '\0';
        }
    }
    value_.backdrop_filters = {copy(source.backdrop_filters()), abi_count(source.backdrop_filters().size())};
    value_.text_gradient = copy_gradient(source.text_gradient());
    value_.transforms = {copy(source.transforms()), abi_count(source.transforms().size())};
    value_.text_shadows = {copy(source.text_shadows()), abi_count(source.text_shadows().size())};
    value_.box_shadows = {copy(source.box_shadows()), abi_count(source.box_shadows().size())};
}
CallbackStyleCopy::~CallbackStyleCopy() noexcept {
    for (void* allocation : allocations_) std::free(allocation);
}
aether_status_t CallbackStyleCopy::create(ComputedStyleReadView source,
    std::unique_ptr<CallbackStyleCopy>& output) noexcept {
    try {
        auto prepared = std::unique_ptr<CallbackStyleCopy>(new CallbackStyleCopy);
        prepared->populate(source);
        output = std::move(prepared);
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
aether_status_t export_owned_flat(ComputedStyleReadView source, aether_computed_style_t* output) noexcept {
    if (!output) return AETHER_ERROR_NULL_ARG;
    std::unique_ptr<CallbackStyleCopy> prepared;
    const auto status = CallbackStyleCopy::create(source, prepared);
    if (status != AETHER_OK) return status;
    *output = prepared->value_;
    prepared->allocations_.clear(); // ownership transferred; no allocation can fail here
    return AETHER_OK;
}
} // namespace aether::style
