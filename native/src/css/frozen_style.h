#pragma once
#include "style_read_view.h"
#include <memory>
#include <unordered_map>
#include "contextual_math.h"
#include <span>
#include <string_view>
#include <utility>

namespace aether::style {

class ImmutableUtf8 {
    const FrozenCString text_;
public:
    ImmutableUtf8() noexcept = default;
    explicit ImmutableUtf8(std::string_view text) : text_(text) {}
    Utf8ReadView view() const noexcept { return Utf8ReadView(text_); }
};
class FrozenGradient {
    const float direction_x_ = 0, direction_y_ = 0;
    const ImmutableArray<aether_gradient_stop> stops_;
public:
    FrozenGradient() noexcept = default;
    FrozenGradient(float x, float y, ImmutableArray<aether_gradient_stop> stops) noexcept
        : direction_x_(x), direction_y_(y), stops_(std::move(stops)) {}
    GradientReadView view() const noexcept { return {direction_x_, direction_y_, stops_.view()}; }
};
class FrozenBackgroundLayer {
    const BackgroundLayerScalars scalars_;
    const FrozenGradient gradient_;
    const ImmutableUtf8 image_source_;
public:
    FrozenBackgroundLayer(BackgroundLayerScalars scalars, FrozenGradient gradient, ImmutableUtf8 image) noexcept
        : scalars_(scalars), gradient_(std::move(gradient)), image_source_(std::move(image)) {}
    BackgroundLayerReadView view() const noexcept { return {scalars_, gradient_.view(), image_source_.view()}; }
};
class FrozenStylePayload {
    const ImmutableArray<FrozenBackgroundLayer> backgrounds_;
    const ImmutableArray<aether_backdrop_filter> filters_;
    const FrozenGradient text_gradient_;
    const ImmutableArray<aether_transform_operation> transforms_;
    const ImmutableArray<aether_shadow> text_shadows_, box_shadows_;
public:
    FrozenStylePayload(ImmutableArray<FrozenBackgroundLayer> backgrounds,
        ImmutableArray<aether_backdrop_filter> filters, FrozenGradient gradient,
        ImmutableArray<aether_transform_operation> transforms,
        ImmutableArray<aether_shadow> text_shadows, ImmutableArray<aether_shadow> box_shadows) noexcept
        : backgrounds_(std::move(backgrounds)), filters_(std::move(filters)), text_gradient_(std::move(gradient)),
          transforms_(std::move(transforms)), text_shadows_(std::move(text_shadows)), box_shadows_(std::move(box_shadows)) {}
    auto background_layers() const noexcept { return backgrounds_.view(); }
    auto backdrop_filters() const noexcept { return filters_.view(); }
    auto text_gradient() const noexcept { return text_gradient_.view(); }
    auto transforms() const noexcept { return transforms_.view(); }
    auto text_shadows() const noexcept { return text_shadows_.view(); }
    auto box_shadows() const noexcept { return box_shadows_.view(); }
};
class FrozenStyle {
    const StyleScalars scalars_;
    const std::shared_ptr<const FrozenStylePayload> payload_;
public:
    FrozenStyle(StyleScalars scalars, std::shared_ptr<const FrozenStylePayload> payload)
        : scalars_(scalars), payload_(std::move(payload)) {
        if (!payload_) throw std::invalid_argument("missing frozen style payload");
    }
    ComputedStyleReadView view() const noexcept { return {scalars_, *payload_}; }
    const std::shared_ptr<const FrozenStylePayload>& payload_owner() const noexcept { return payload_; }
};
using FrozenStyleRef = std::shared_ptr<const FrozenStyle>;

// Transport values for a future builder adapter, deliberately independent of
// mutable measure_state.h. They own no pointers and have no runtime/backrefs.
struct DocumentMeasureValues {
    float viewport_width = 0, viewport_height = 0;
    float initial_font_size = 16, root_font_size = 16;
    uint64_t generation = 0, identity = 0;
};
class FrozenDocumentMeasureContext {
    const DocumentMeasureValues values_;
public:
    explicit FrozenDocumentMeasureContext(DocumentMeasureValues values) noexcept : values_(values) {}
    const DocumentMeasureValues& values() const noexcept { return values_; }
};
enum class FrozenLineHeightKind { normal, number, absolute_css_px };
struct MeasureScalarValues {
    float parent_font_size = 16, self_font_size = 16;
    FrozenLineHeightKind line_height_kind = FrozenLineHeightKind::normal;
    float line_height_value = 1.2f;
    aether_css_length row_gap{}, column_gap{};
    uint32_t dependencies = 0;
    int32_t flex_order = 0;
};
struct MeasureBackgroundPosition { aether_css_length x{}, y{}; };
struct MeasureBackgroundSize { int32_t kind = 0; aether_css_length x{}, y{}; };
struct PropertyDependencyInput { std::string_view name; uint32_t flags = 0; };
class FrozenPropertyDependency {
    const ImmutableUtf8 name_;
    const uint32_t flags_;
public:
    explicit FrozenPropertyDependency(PropertyDependencyInput input) : name_(input.name), flags_(input.flags) {}
    Utf8ReadView name() const noexcept { return name_.view(); }
    uint32_t flags() const noexcept { return flags_; }
};
class FrozenMeasures {
    const MeasureScalarValues scalars_;
    const std::shared_ptr<const FrozenDocumentMeasureContext> document_;
    const ImmutableArray<MeasureBackgroundPosition> positions_;
    const ImmutableArray<MeasureBackgroundSize> sizes_;
    const ImmutableArray<FrozenPropertyDependency> dependencies_;
    const std::unordered_map<std::string, aether::css::math_tree> contextual_lengths_;
public:
    FrozenMeasures(MeasureScalarValues scalars, std::shared_ptr<const FrozenDocumentMeasureContext> document,
        ImmutableArray<MeasureBackgroundPosition> positions, ImmutableArray<MeasureBackgroundSize> sizes,
        ImmutableArray<FrozenPropertyDependency> dependencies,
        std::unordered_map<std::string, aether::css::math_tree> contextual_lengths = {})
        : scalars_(scalars), document_(std::move(document)), positions_(std::move(positions)),
          sizes_(std::move(sizes)), dependencies_(std::move(dependencies)), contextual_lengths_(std::move(contextual_lengths)) {
        if (!document_) throw std::invalid_argument("missing frozen document context");
    }
    const MeasureScalarValues& scalars() const noexcept { return scalars_; }
    const FrozenDocumentMeasureContext& document() const noexcept { return *document_; }
    const std::shared_ptr<const FrozenDocumentMeasureContext>& document_owner() const noexcept { return document_; }
    auto background_positions() const noexcept { return positions_.view(); }
    auto background_sizes() const noexcept { return sizes_.view(); }
    auto property_dependencies() const noexcept { return dependencies_.view(); }
    const auto& contextual_lengths() const noexcept { return contextual_lengths_; }
    aether::css::math_tree contextual_tree(const std::string& name) const {
        const auto found = contextual_lengths_.find(name);
        return found == contextual_lengths_.end() ? nullptr : found->second;
    }
    uint32_t dependency(std::string_view name) const noexcept {
        for (const auto& entry : dependencies_.view()) if (entry.name().equals(name)) return entry.flags();
        return 0;
    }
};

} // namespace aether::style
