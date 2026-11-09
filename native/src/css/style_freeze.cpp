#include "style_freeze.h"
#include <cstring>
#include <limits>
#include <stdexcept>

namespace aether::style {
namespace {
template<class T> size_t checked_count(const T* items, int32_t count) {
    if (count < 0 || (count && !items) || (!count && items))
        throw std::invalid_argument("invalid flat list storage");
    const auto size = static_cast<size_t>(count);
    if (size > std::numeric_limits<size_t>::max() / sizeof(T))
        throw std::length_error("flat list size overflow");
    return size;
}
template<class T> ImmutableArray<T> copy_list(const T* items, int32_t count) {
    const auto size = checked_count(items, count);
    return ImmutableArray<T>::generate(size, [&](size_t i) { return items[i]; });
}
FrozenGradient freeze_gradient(const aether_linear_gradient& gradient) {
    return {gradient.direction_x, gradient.direction_y, copy_list(gradient.stops, gradient.stop_count)};
}
void validate_flat(const aether_computed_style_t& source) {
    checked_count(source.background_layers.items, source.background_layers.count);
    checked_count(source.backdrop_filters.items, source.backdrop_filters.count);
    checked_count(source.transforms.items, source.transforms.count);
    checked_count(source.text_shadows.items, source.text_shadows.count);
    checked_count(source.box_shadows.items, source.box_shadows.count);
    checked_count(source.text_gradient.stops, source.text_gradient.stop_count);
    for (int32_t i = 0; i < source.background_layers.count; ++i) {
        const auto& layer = source.background_layers.items[i];
        checked_count(layer.gradient.stops, layer.gradient.stop_count);
    }
}
}
aether_status_t freeze_style(const aether_computed_style_t& source, FrozenStyleRef& output) noexcept {
    try {
        validate_flat(source);
        auto backgrounds = ImmutableArray<FrozenBackgroundLayer>::generate(
            static_cast<size_t>(source.background_layers.count), [&](size_t i) {
                const auto& layer = source.background_layers.items[i];
                return FrozenBackgroundLayer(BackgroundLayerScalars::copy_from(layer), freeze_gradient(layer.gradient),
                    layer.image_source ? ImmutableUtf8(std::string_view(layer.image_source)) : ImmutableUtf8{});
            });
        auto filters = copy_list(source.backdrop_filters.items, source.backdrop_filters.count);
        auto gradient = freeze_gradient(source.text_gradient);
        auto transforms = copy_list(source.transforms.items, source.transforms.count);
        auto text_shadows = copy_list(source.text_shadows.items, source.text_shadows.count);
        auto box_shadows = copy_list(source.box_shadows.items, source.box_shadows.count);
        auto payload = std::make_shared<const FrozenStylePayload>(std::move(backgrounds), std::move(filters),
            std::move(gradient), std::move(transforms), std::move(text_shadows), std::move(box_shadows));
        auto prepared = std::make_shared<const FrozenStyle>(StyleScalars::copy_from(source), std::move(payload));
        output = std::move(prepared);
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
aether_status_t freeze_document(DocumentMeasureValues source,
    std::shared_ptr<const FrozenDocumentMeasureContext>& output) noexcept {
    try {
        auto prepared = std::make_shared<const FrozenDocumentMeasureContext>(source);
        output = std::move(prepared); return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
aether_status_t freeze_measure_payload(MeasureScalarValues scalars,
    std::span<const MeasureBackgroundPosition> positions, std::span<const MeasureBackgroundSize> sizes,
    std::span<const PropertyDependencyInput> dependencies, FrozenMeasurePayloadRef& output) noexcept {
    try {
        if (positions.empty() || sizes.empty()) return AETHER_ERROR_INVALID_STATE;
        uint32_t combined = 0;
        for (size_t i = 0; i < dependencies.size(); ++i) {
            if (dependencies[i].name.empty()) return AETHER_ERROR_INVALID_STATE;
            for (size_t j = 0; j < i; ++j) if (dependencies[i].name == dependencies[j].name) return AETHER_ERROR_INVALID_STATE;
            combined |= dependencies[i].flags;
        }
        if (combined != scalars.dependencies) return AETHER_ERROR_INVALID_STATE;
        auto frozen_positions = ImmutableArray<MeasureBackgroundPosition>::generate(positions.size(), [&](size_t i) { return positions[i]; });
        auto frozen_sizes = ImmutableArray<MeasureBackgroundSize>::generate(sizes.size(), [&](size_t i) { return sizes[i]; });
        size_t count = 0;
        for (const auto& entry : dependencies) if (entry.flags) ++count;
        size_t cursor = 0;
        auto frozen_dependencies = ImmutableArray<FrozenPropertyDependency>::generate(count, [&](size_t) {
            while (!dependencies[cursor].flags) ++cursor;
            return FrozenPropertyDependency(dependencies[cursor++]);
        });
        auto prepared = std::make_shared<const FrozenMeasurePayload>(scalars,
            std::move(frozen_positions), std::move(frozen_sizes), std::move(frozen_dependencies));
        output = std::move(prepared); return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
aether_status_t freeze_measures(MeasureScalarValues scalars, std::shared_ptr<const FrozenDocumentMeasureContext> document,
    std::span<const MeasureBackgroundPosition> positions, std::span<const MeasureBackgroundSize> sizes,
    std::span<const PropertyDependencyInput> dependencies, std::shared_ptr<const FrozenMeasures>& output) noexcept {
    if(!document)return AETHER_ERROR_INVALID_STATE;
    FrozenMeasurePayloadRef payload;
    const auto status=freeze_measure_payload(scalars,positions,sizes,dependencies,payload);
    if(status!=AETHER_OK)return status;
    try { auto result=std::make_shared<const FrozenMeasures>(std::move(payload),std::move(document));output=std::move(result);return AETHER_OK; }
    catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}
}
} // namespace aether::style
