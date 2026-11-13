#pragma once
#include "../css/style_read_view.h"
#include <array>
#include <string_view>

struct measure_state_s;
struct color_resolution_state_s;
namespace aether::style {
class FrozenMeasures;
class FrozenColorMetadata;

// These are deltas, never historical declaration masks or instructions to prune.
struct StyleDiff {
    uint32_t own_impact = 0;
    uint32_t inherited_value_impact = 0;
    bool inherited_metadata_changed = false;
    bool metadata_changed = false;
    bool style_storage_equivalent = true;
    bool unclassified = false;
};
enum class StyleField {
#define AETHER_DIFF_FIELD(type, name) name,
    AETHER_STYLE_SCALAR_FIELDS(AETHER_DIFF_FIELD)
#undef AETHER_DIFF_FIELD
    font_family, count
};
// Intentionally no primary implementation: adding a scalar requires a policy.
template<StyleField> struct StyleFieldPolicy;
template<> struct StyleFieldPolicy<StyleField::display> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::position> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::inset> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::width> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::height> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::min_width> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::min_height> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::max_width> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::max_height> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::margin> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::padding> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::border_width> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::overflow_x> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::overflow_y> { static constexpr uint32_t impact = 7; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::box_sizing> { static constexpr uint32_t impact = 1; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::flex_direction> { static constexpr uint32_t impact = 1; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::justify_content> { static constexpr uint32_t impact = 1; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::align_items> { static constexpr uint32_t impact = 1; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::align_self> { static constexpr uint32_t impact = 1; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::flex_grow> { static constexpr uint32_t impact = 1; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::flex_shrink> { static constexpr uint32_t impact = 1; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::flex_basis> { static constexpr uint32_t impact = 1; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::flex_wrap> { static constexpr uint32_t impact = 1; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::gap> { static constexpr uint32_t impact = 1; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::border_styles> { static constexpr uint32_t impact = 3; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::color> { static constexpr uint32_t impact = 66; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::background_color> { static constexpr uint32_t impact = 2; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::border_color> { static constexpr uint32_t impact = 2; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::border_colors> { static constexpr uint32_t impact = 2; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::has_text_gradient> { static constexpr uint32_t impact = 2; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::border_radii> { static constexpr uint32_t impact = 6; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::opacity> { static constexpr uint32_t impact = 12; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::transform_origin> { static constexpr uint32_t impact = 12; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::font_size> { static constexpr uint32_t impact = 83; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::font_family> { static constexpr uint32_t impact = 82; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::font_weight> { static constexpr uint32_t impact = 82; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::text_align> { static constexpr uint32_t impact = 82; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::text_transform> { static constexpr uint32_t impact = 82; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::line_height> { static constexpr uint32_t impact = 81; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::white_space> { static constexpr uint32_t impact = 81; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::cursor> { static constexpr uint32_t impact = 96; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::pointer_events> { static constexpr uint32_t impact = 68; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::visible> { static constexpr uint32_t impact = 70; static constexpr bool inherited = true; };
template<> struct StyleFieldPolicy<StyleField::z_index> { static constexpr uint32_t impact = 6; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::z_index_kind> { static constexpr uint32_t impact = 6; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::transition_properties> { static constexpr uint32_t impact = 2; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::transition_duration> { static constexpr uint32_t impact = 2; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::transition_delay> { static constexpr uint32_t impact = 2; static constexpr bool inherited = false; };
template<> struct StyleFieldPolicy<StyleField::transition_timing> { static constexpr uint32_t impact = 2; static constexpr bool inherited = false; };
#define AETHER_CHECK_DIFF_FIELD(type, name) static_assert(StyleFieldPolicy<StyleField::name>::impact != 0);
AETHER_STYLE_SCALAR_FIELDS(AETHER_CHECK_DIFF_FIELD)
#undef AETHER_CHECK_DIFF_FIELD
static_assert(StyleFieldPolicy<StyleField::font_family>::impact == 82);

// Explicit inventories for content stored outside the scalar manifest. Their
// policies are checked independently in the classifier unit test.
#define AETHER_STYLE_PAYLOAD_POLICIES(X) \
    X(backgrounds, 2, false) \
    X(backdrop_filters, 10, false) \
    X(text_gradient, 2, false) \
    X(transforms, 12, false) \
    X(text_shadows, 66, true) \
    X(box_shadows, 2, false)
enum class StylePayload {
#define AETHER_PAYLOAD_ENUM(name, impact, inherited) name,
    AETHER_STYLE_PAYLOAD_POLICIES(AETHER_PAYLOAD_ENUM)
#undef AETHER_PAYLOAD_ENUM
    count
};
template<StylePayload> struct StylePayloadPolicy;
#define AETHER_PAYLOAD_POLICY(name, mask, inherit) \
    template<> struct StylePayloadPolicy<StylePayload::name> { static constexpr uint32_t impact = mask; static constexpr bool inherited = inherit; };
AETHER_STYLE_PAYLOAD_POLICIES(AETHER_PAYLOAD_POLICY)
#undef AETHER_PAYLOAD_POLICY

// Adding an inventory entry requires both an explicit policy here and a
// comparator specialization in style_diff.cpp. There is no fallback to metadata.
#define AETHER_STYLE_SIDECAR_FIELDS(X) \
    X(property_dependencies) \
    X(contextual_lengths) \
    X(intrinsic_keywords) \
    X(combined_dependencies) \
    X(parent_font_size) \
    X(self_font_size) \
    X(line_height) \
    X(flex_order) \
    X(direction_rtl) \
    X(writing_mode) \
    X(background_gradient) \
    X(row_gap) \
    X(column_gap) \
    X(background_positions) \
    X(background_sizes) \
    X(document_inputs) \
    X(background_color_reference) \
    X(border_color_references) \
    X(text_shadow_references) \
    X(box_shadow_references)
enum class StyleSidecar {
#define AETHER_SIDECAR_ENUM(name) name,
    AETHER_STYLE_SIDECAR_FIELDS(AETHER_SIDECAR_ENUM)
#undef AETHER_SIDECAR_ENUM
    count
};
enum class SidecarPolicyKind { metadata, value, percentage_consumers, inherited_metadata };
template<StyleSidecar> struct StyleSidecarPolicy;
#define AETHER_SIDECAR_POLICY(name, classification, mask, inherit) \
    template<> struct StyleSidecarPolicy<StyleSidecar::name> { \
        static constexpr auto kind = SidecarPolicyKind::classification; \
        static constexpr uint32_t impact = mask; static constexpr bool inherited = inherit; };
AETHER_SIDECAR_POLICY(property_dependencies, percentage_consumers, 0, false)
AETHER_SIDECAR_POLICY(contextual_lengths, value, 7, false)
AETHER_SIDECAR_POLICY(intrinsic_keywords, value, 7, false)
AETHER_SIDECAR_POLICY(combined_dependencies, metadata, 0, false)
AETHER_SIDECAR_POLICY(parent_font_size, metadata, 0, false)
AETHER_SIDECAR_POLICY(self_font_size, metadata, 0, false)
AETHER_SIDECAR_POLICY(line_height, value, 81, true)
AETHER_SIDECAR_POLICY(flex_order, value, 7, false)
AETHER_SIDECAR_POLICY(direction_rtl, value, 7, true)
AETHER_SIDECAR_POLICY(writing_mode, value, 7, true)
AETHER_SIDECAR_POLICY(background_gradient, value, 2, false)
AETHER_SIDECAR_POLICY(row_gap, value, 17, false)
AETHER_SIDECAR_POLICY(column_gap, value, 17, false)
AETHER_SIDECAR_POLICY(background_positions, metadata, 0, false)
AETHER_SIDECAR_POLICY(background_sizes, metadata, 0, false)
AETHER_SIDECAR_POLICY(document_inputs, metadata, 0, false)
AETHER_SIDECAR_POLICY(background_color_reference, metadata, 0, false)
AETHER_SIDECAR_POLICY(border_color_references, metadata, 0, false)
AETHER_SIDECAR_POLICY(text_shadow_references, inherited_metadata, 0, false)
AETHER_SIDECAR_POLICY(box_shadow_references, metadata, 0, false)
#undef AETHER_SIDECAR_POLICY
#define AETHER_CHECK_SIDECAR(name) static_assert(StyleSidecarPolicy<StyleSidecar::name>::impact < 128);
AETHER_STYLE_SIDECAR_FIELDS(AETHER_CHECK_SIDECAR)
#undef AETHER_CHECK_SIDECAR

enum class DependencyPercentagePolicy { metadata, dimension, flex_basis, gap };
enum class PropertyStatePolicy { stored, alias, no_op };
struct StyleDependencyPolicy {
    std::string_view name;
    DependencyPercentagePolicy percentage;
    PropertyStatePolicy state;
};
// Local indices, independent of registry IDs. The registry correspondence test
// requires every definition, including aliases and no-ops: source scanning can
// attach dependencies even to a property without a layout consumer.
inline constexpr auto style_dependency_catalog = std::to_array<StyleDependencyPolicy>({
    {"-webkit-backdrop-filter", DependencyPercentagePolicy::metadata, PropertyStatePolicy::alias},
    {"-webkit-background-clip", DependencyPercentagePolicy::metadata, PropertyStatePolicy::alias},
    {"-webkit-text-fill-color", DependencyPercentagePolicy::metadata, PropertyStatePolicy::alias},
    {"align-content", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"align-items", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"align-self", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    // Parameters are resolved before layout; sampled target fields carry the visual diff.
    {"animation-delay", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"animation-direction", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"animation-duration", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"animation-fill-mode", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"animation-iteration-count", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"animation-name", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"animation-play-state", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"animation-timing-function", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"backdrop-filter", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"background-clip", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"background-color", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"background-image", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"background-position", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"background-repeat", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"background-size", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-bottom-color", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-bottom-left-radius", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-bottom-right-radius", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-bottom-style", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-bottom-width", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-left-color", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-left-style", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-left-width", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-radius", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-right-color", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-right-style", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-right-width", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-top-color", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-top-left-radius", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-top-right-radius", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-top-style", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"border-top-width", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"bottom", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"box-shadow", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"box-sizing", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"color", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"column-gap", DependencyPercentagePolicy::gap, PropertyStatePolicy::stored},
    {"cursor", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"display", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"flex-basis", DependencyPercentagePolicy::flex_basis, PropertyStatePolicy::stored},
    {"flex-direction", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"flex-grow", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"flex-shrink", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"flex-wrap", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"font-family", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"font-size", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"font-style", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"font-weight", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"height", DependencyPercentagePolicy::dimension, PropertyStatePolicy::stored},
    {"justify-content", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"left", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"letter-spacing", DependencyPercentagePolicy::metadata, PropertyStatePolicy::no_op},
    {"line-height", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"margin-bottom", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"margin-left", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"margin-right", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"margin-top", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"max-height", DependencyPercentagePolicy::dimension, PropertyStatePolicy::stored},
    {"max-width", DependencyPercentagePolicy::dimension, PropertyStatePolicy::stored},
    {"min-height", DependencyPercentagePolicy::dimension, PropertyStatePolicy::stored},
    {"min-width", DependencyPercentagePolicy::dimension, PropertyStatePolicy::stored},
    {"opacity", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"order", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"overflow-x", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"overflow-y", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"padding-bottom", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"padding-left", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"padding-right", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"padding-top", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"pointer-events", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"position", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"right", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"row-gap", DependencyPercentagePolicy::gap, PropertyStatePolicy::stored},
    {"text-align", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"text-fill-color", DependencyPercentagePolicy::metadata, PropertyStatePolicy::alias},
    {"text-shadow", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"text-transform", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"top", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"transform", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"transform-origin", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"transition-delay", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"transition-duration", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"transition-property", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"transition-timing-function", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"visibility", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"white-space", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
    {"width", DependencyPercentagePolicy::dimension, PropertyStatePolicy::stored},
    {"z-index", DependencyPercentagePolicy::metadata, PropertyStatePolicy::stored},
});
constexpr size_t style_dependency_index(std::string_view name) noexcept {
    for (size_t i = 0; i < style_dependency_catalog.size(); ++i)
        if (style_dependency_catalog[i].name == name) return i;
    return style_dependency_catalog.size();
}
consteval bool style_dependency_catalog_unique() {
    for (size_t i = 0; i < style_dependency_catalog.size(); ++i)
        for (size_t j = i + 1; j < style_dependency_catalog.size(); ++j)
            if (style_dependency_catalog[i].name == style_dependency_catalog[j].name) return false;
    return true;
}
static_assert(style_dependency_catalog_unique());

// Previous owners and the exclusive candidate remain live for this call. No
// parse/apply/deep-copy, allocation, registry/DOM/layout access, or callbacks.
StyleDiff classify_style_diff(ComputedStyleReadView previous,
    const FrozenMeasures& previous_measures, const FrozenColorMetadata& previous_colors,
    const aether_computed_style_t& candidate, const measure_state_s& candidate_measures,
    const color_resolution_state_s& candidate_colors) noexcept;
} // namespace aether::style
