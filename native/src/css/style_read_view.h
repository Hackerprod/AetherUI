#pragma once
#include <aether/aether.h>
#include "immutable_array.h"
#include <array>
#include <cstring>
#include <string_view>

// Internal field manifest. Public C headers remain the ABI source of truth.
#define AETHER_STYLE_SCALAR_FIELDS(X) \
    X(int32_t, display) \
    X(int32_t, flex_direction) \
    X(int32_t, justify_content) \
    X(int32_t, align_items) \
    X(int32_t, align_self) \
    X(float, flex_grow) \
    X(float, flex_shrink) \
    X(aether_flex_basis, flex_basis) \
    X(aether_bool_t, flex_wrap) \
    X(float, gap) \
    X(int32_t, position) \
    X(aether_css_edges, inset) \
    X(aether_css_length, width) \
    X(aether_css_length, height) \
    X(aether_css_length, min_width) \
    X(aether_css_length, min_height) \
    X(aether_css_length, max_width) \
    X(aether_css_length, max_height) \
    X(aether_css_edges, margin) \
    X(aether_css_edges, padding) \
    X(aether_css_edges, border_width) \
    X(int32_t, box_sizing) \
    X(aether_color_rgba, color) \
    X(aether_color_rgba, background_color) \
    X(aether_color_rgba, border_color) \
    X(aether_color_edges, border_colors) \
    X(aether_border_style_edges, border_styles) \
    X(aether_bool_t, has_text_gradient) \
    X(aether_transform_origin, transform_origin) \
    X(aether_border_radii, border_radii) \
    X(float, opacity) \
    X(float, font_size) \
    X(int32_t, font_weight) \
    X(float, line_height) \
    X(int32_t, text_align) \
    X(int32_t, text_transform) \
    X(int32_t, white_space) \
    X(int32_t, overflow_x) \
    X(int32_t, overflow_y) \
    X(int32_t, cursor) \
    X(int32_t, pointer_events) \
    X(aether_bool_t, visible) \
    X(int32_t, z_index) \
    X(int32_t, z_index_kind) \
    X(int32_t, transition_properties) \
    X(float, transition_duration) \
    X(float, transition_delay) \
    X(int32_t, transition_timing)

namespace aether::style {

struct StyleScalars {
#define AETHER_DECLARE_SCALAR(type, name) type name{};
    AETHER_STYLE_SCALAR_FIELDS(AETHER_DECLARE_SCALAR)
#undef AETHER_DECLARE_SCALAR
    std::array<char, 64> font_family{};
    static StyleScalars copy_from(const aether_computed_style_t& source) noexcept {
        StyleScalars result;
#define AETHER_COPY_SCALAR(type, name) result.name = source.name;
        AETHER_STYLE_SCALAR_FIELDS(AETHER_COPY_SCALAR)
#undef AETHER_COPY_SCALAR
        std::memcpy(result.font_family.data(), source.font_family, result.font_family.size());
        return result;
    }
    void copy_to(aether_computed_style_t& destination) const noexcept {
#define AETHER_EXPORT_SCALAR(type, name) destination.name = name;
        AETHER_STYLE_SCALAR_FIELDS(AETHER_EXPORT_SCALAR)
#undef AETHER_EXPORT_SCALAR
        std::memcpy(destination.font_family, font_family.data(), font_family.size());
    }
};

// No char-array pointer is exposed from slot storage. C strings are created in
// independent C-owned buffers by the exporter, including their trailing NUL.
class Utf8ReadView {
    bool present_ = false;
    ReadOnlyArrayView<char> bytes_;
public:
    Utf8ReadView() noexcept = default;
    Utf8ReadView(bool present, ReadOnlyArrayView<char> bytes) noexcept : present_(present), bytes_(bytes) {}
    bool present() const noexcept { return present_; }
    size_t size() const noexcept { return bytes_.size(); }
    bool empty() const noexcept { return bytes_.empty(); }
    const char& operator[](size_t index) const noexcept { return bytes_[index]; }
    ReadOnlyArrayView<char> bytes() const noexcept { return bytes_; }
    bool equals(std::string_view value) const noexcept {
        if (!present_ || size() != value.size()) return false;
        for (size_t i = 0; i < size(); ++i) if (bytes_[i] != value[i]) return false;
        return true;
    }
};

struct GradientReadView {
    float direction_x = 0, direction_y = 0;
    ReadOnlyArrayView<aether_gradient_stop> stops;
};
struct BackgroundLayerScalars {
    int32_t kind = 0;
    aether_css_length position_x{}, position_y{};
    int32_t size_kind = 0;
    aether_css_length size_x{}, size_y{};
    int32_t repeat_x = 0, repeat_y = 0;
    static BackgroundLayerScalars copy_from(const aether_background_layer& value) noexcept {
        return {value.kind, value.position_x, value.position_y, value.size_kind,
                value.size_x, value.size_y, value.repeat_x, value.repeat_y};
    }
    void copy_to(aether_background_layer& value) const noexcept {
        value.kind = kind; value.position_x = position_x; value.position_y = position_y;
        value.size_kind = size_kind; value.size_x = size_x; value.size_y = size_y;
        value.repeat_x = repeat_x; value.repeat_y = repeat_y;
    }
};
struct BackgroundLayerReadView {
    BackgroundLayerScalars scalars;
    GradientReadView gradient;
    Utf8ReadView image_source;
};

class FrozenBackgroundLayer;
class FrozenStylePayload;
class FrozenStyle;
class ComputedStyleReadView {
    const StyleScalars* scalars_ = nullptr;
    const FrozenStylePayload* payload_ = nullptr;
    friend class FrozenStyle;
    ComputedStyleReadView(const StyleScalars& scalars, const FrozenStylePayload& payload) noexcept
        : scalars_(&scalars), payload_(&payload) {}
public:
    const StyleScalars& scalars() const noexcept { return *scalars_; }
    ReadOnlyArrayView<FrozenBackgroundLayer> background_layers() const noexcept;
    ReadOnlyArrayView<aether_backdrop_filter> backdrop_filters() const noexcept;
    ReadOnlyArrayView<aether_transform_operation> transforms() const noexcept;
    ReadOnlyArrayView<aether_shadow> text_shadows() const noexcept;
    ReadOnlyArrayView<aether_shadow> box_shadows() const noexcept;
    GradientReadView text_gradient() const noexcept;
    // Scalar overrides belong to the caller's stack/box; its lifetime must also
    // cover this borrowed view. Payload ownership is unchanged.
    ComputedStyleReadView with_scalars(const StyleScalars& scalars) const noexcept {
        return {scalars, *payload_};
    }
};

} // namespace aether::style
