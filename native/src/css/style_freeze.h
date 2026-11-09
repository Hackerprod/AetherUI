#pragma once
#include "frozen_style.h"

namespace aether::style {
// These factories never adopt or mutate the source. Failure leaves the previous
// output owner unchanged. Borrowed source storage must be valid for the call.
aether_status_t freeze_style(const aether_computed_style_t& source, FrozenStyleRef& output) noexcept;
aether_status_t freeze_document(DocumentMeasureValues source,
    std::shared_ptr<const FrozenDocumentMeasureContext>& output) noexcept;
aether_status_t freeze_measure_payload(MeasureScalarValues scalars,
    std::span<const MeasureBackgroundPosition> positions, std::span<const MeasureBackgroundSize> sizes,
    std::span<const PropertyDependencyInput> dependencies, FrozenMeasurePayloadRef& output) noexcept;
aether_status_t freeze_measures(MeasureScalarValues scalars,
    std::shared_ptr<const FrozenDocumentMeasureContext> document,
    std::span<const MeasureBackgroundPosition> positions, std::span<const MeasureBackgroundSize> sizes,
    std::span<const PropertyDependencyInput> dependencies, std::shared_ptr<const FrozenMeasures>& output) noexcept;
} // namespace aether::style
