#include "style_diff.h"
#include "../css/frozen_style.h"
#include "../css/measure_state.h"
#include "style_property_registry_internal.h"
#include "frozen_runtime_metadata.h"

namespace aether::style {
namespace {
constexpr uint32_t all_impact =
    uint32_t(STYLE_IMPACT_LAYOUT) | uint32_t(STYLE_IMPACT_PAINT) | uint32_t(STYLE_IMPACT_HITTEST) |
    uint32_t(STYLE_IMPACT_COMPOSITE) | uint32_t(STYLE_IMPACT_TEXT_LAYOUT) |
    uint32_t(STYLE_IMPACT_CURSOR) | uint32_t(STYLE_IMPACT_INHERITED);

// Typed equality deliberately preserves == semantics (+0 == -0; NaN != NaN).
// Never compare padding, pointers, or an active union arm as a whole byte blob.
template<class T> bool equal(const T& a, const T& b) noexcept { return a == b; }
bool equal(const aether_css_length& a, const aether_css_length& b) noexcept {
    return a.value == b.value && a.unit == b.unit && a.linear_percent == b.linear_percent &&
        a.linear_em == b.linear_em && a.linear_rem == b.linear_rem &&
        a.linear_vw == b.linear_vw && a.linear_vh == b.linear_vh;
}
bool equal(const aether_color_rgba& a, const aether_color_rgba& b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
bool equal(const aether_css_edges& a, const aether_css_edges& b) noexcept {
    return equal(a.top, b.top) && equal(a.right, b.right) && equal(a.bottom, b.bottom) && equal(a.left, b.left);
}
bool equal(const aether_color_edges& a, const aether_color_edges& b) noexcept {
    return equal(a.top, b.top) && equal(a.right, b.right) && equal(a.bottom, b.bottom) && equal(a.left, b.left);
}
bool equal(const aether_border_style_edges& a, const aether_border_style_edges& b) noexcept {
    return a.top == b.top && a.right == b.right && a.bottom == b.bottom && a.left == b.left;
}
bool equal(const aether_flex_basis& a, const aether_flex_basis& b) noexcept {
    return a.kind == b.kind && equal(a.length, b.length);
}
bool equal(const aether_corner_radius& a, const aether_corner_radius& b) noexcept {
    return equal(a.horizontal, b.horizontal) && equal(a.vertical, b.vertical);
}
bool equal(const aether_border_radii& a, const aether_border_radii& b) noexcept {
    return equal(a.top_left, b.top_left) && equal(a.top_right, b.top_right) &&
        equal(a.bottom_left, b.bottom_left) && equal(a.bottom_right, b.bottom_right);
}
bool equal(const aether_transform_origin& a, const aether_transform_origin& b) noexcept {
    return equal(a.x, b.x) && equal(a.y, b.y);
}
bool equal(const aether_gradient_stop& a, const aether_gradient_stop& b) noexcept {
    return equal(a.color, b.color) && equal(a.position, b.position) &&
        a.offset == b.offset && a.has_position == b.has_position;
}
bool equal(const aether_transform_operation& a, const aether_transform_operation& b) noexcept {
    return a.kind == b.kind && equal(a.x, b.x) && equal(a.y, b.y) &&
        a.scalar_x == b.scalar_x && a.scalar_y == b.scalar_y && a.angle_radians == b.angle_radians &&
        a.matrix_a == b.matrix_a && a.matrix_b == b.matrix_b && a.matrix_c == b.matrix_c &&
        a.matrix_d == b.matrix_d && a.matrix_e == b.matrix_e && a.matrix_f == b.matrix_f;
}
bool equal(const aether_backdrop_filter& a, const aether_backdrop_filter& b) noexcept {
    return a.kind == b.kind && equal(a.length_amount, b.length_amount) && a.scalar_amount == b.scalar_amount;
}
bool equal(const aether_shadow& a, const aether_shadow& b) noexcept {
    return a.offset_x == b.offset_x && a.offset_y == b.offset_y && a.blur_radius == b.blur_radius &&
        a.spread_radius == b.spread_radius && equal(a.color, b.color) && a.inset == b.inset;
}
bool equal(Utf8ReadView a, const char* b) noexcept {
    if (a.present() != (b != nullptr)) return false;
    return !b || a.equals(std::string_view(b));
}
template<class T> bool known(const T&) noexcept { return true; }
bool known(const aether_transform_operation& a) noexcept {
    switch (a.kind) {
    case AETHER_TRANSFORM_TRANSLATE: case AETHER_TRANSFORM_SCALE: case AETHER_TRANSFORM_ROTATE:
    case AETHER_TRANSFORM_SKEW_X: case AETHER_TRANSFORM_SKEW_Y: case AETHER_TRANSFORM_MATRIX: return true;
    default: return false;
    }
}
bool known(const aether_backdrop_filter& a) noexcept {
    switch (a.kind) {
    case AETHER_BACKDROP_FILTER_BLUR: case AETHER_BACKDROP_FILTER_BRIGHTNESS:
    case AETHER_BACKDROP_FILTER_CONTRAST: case AETHER_BACKDROP_FILTER_SATURATE: return true;
    default: return false;
    }
}
bool known_background(int32_t kind) noexcept {
    return kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT || kind == AETHER_BACKGROUND_LAYER_IMAGE_URL;
}
template<class T> bool array_equal(ReadOnlyArrayView<T> a, const T* b, int32_t n, StyleDiff& diff) noexcept {
    if (n < 0 || (n && !b)) { diff.unclassified = true; return false; }
    bool same = a.size() == static_cast<size_t>(n);
    // Validate even unmatched entries: a new operation cannot silently acquire
    // an existing policy just because the other side has a shorter list.
    for (const auto& value : a) if (!known(value)) diff.unclassified = true;
    for (int32_t i = 0; i < n; ++i) if (!known(b[i])) diff.unclassified = true;
    for (size_t i = 0; i < a.size() && i < static_cast<size_t>(n); ++i)
        if (!equal(a[i], b[i])) same = false;
    return same;
}
bool equal(GradientReadView a, const aether_linear_gradient& b, StyleDiff& diff) noexcept {
    const bool stops_equal = array_equal(a.stops, b.stops, b.stop_count, diff);
    return a.direction_x == b.direction_x && a.direction_y == b.direction_y && stops_equal;
}
void changed(StyleDiff& result, uint32_t impact, bool inherited = false) noexcept {
    result.own_impact |= impact;
    if (inherited) result.inherited_value_impact |= impact;
}
void stored_delta(StyleDiff& result, bool same, uint32_t impact, bool inherited = false) noexcept {
    if (!same) { result.style_storage_equivalent = false; changed(result, impact, inherited); }
}

// Inventory-driven comparators have no generic implementation. New payloads
// cannot compile until their content reader and visual/storage policy exist.
template<StylePayload> struct PayloadComparator;
#define AETHER_ARRAY_PAYLOAD(name) \
    template<> struct PayloadComparator<StylePayload::name> { \
        static void compare(ComputedStyleReadView a, const aether_computed_style_t& b, StyleDiff& result) noexcept { \
            stored_delta(result, array_equal(a.name(), b.name.items, b.name.count, result), \
                StylePayloadPolicy<StylePayload::name>::impact, StylePayloadPolicy<StylePayload::name>::inherited); \
        } };
AETHER_ARRAY_PAYLOAD(transforms)
AETHER_ARRAY_PAYLOAD(backdrop_filters)
AETHER_ARRAY_PAYLOAD(text_shadows)
AETHER_ARRAY_PAYLOAD(box_shadows)
#undef AETHER_ARRAY_PAYLOAD
template<> struct PayloadComparator<StylePayload::text_gradient> {
    static void compare(ComputedStyleReadView a, const aether_computed_style_t& b, StyleDiff& result) noexcept {
        stored_delta(result, equal(a.text_gradient(), b.text_gradient, result), StylePayloadPolicy<StylePayload::text_gradient>::impact);
    }
};
template<> struct PayloadComparator<StylePayload::backgrounds> {
    static void compare(ComputedStyleReadView a, const aether_computed_style_t& b, StyleDiff& result) noexcept {
        const auto layers = a.background_layers();
        const auto& next = b.background_layers;
        if (next.count < 0 || (next.count && !next.items)) { result.unclassified = true; return; }
        bool storage_equal = layers.size() == static_cast<size_t>(next.count);
        bool visual_equal = storage_equal;
        for (const auto& layer : layers) if (!known_background(layer.view().scalars.kind)) result.unclassified = true;
        for (int32_t i = 0; i < next.count; ++i) if (!known_background(next.items[i].kind)) result.unclassified = true;
        for (size_t i = 0; i < layers.size() && i < static_cast<size_t>(next.count); ++i) {
            const auto layer = layers[i].view();
            const auto& x = layer.scalars;
            const auto& y = next.items[i];
            const bool scalars_equal = x.kind == y.kind && equal(x.position_x, y.position_x) &&
                equal(x.position_y, y.position_y) && x.size_kind == y.size_kind &&
                equal(x.size_x, y.size_x) && equal(x.size_y, y.size_y) &&
                x.repeat_x == y.repeat_x && x.repeat_y == y.repeat_y;
            const bool gradient_equal = equal(layer.gradient, y.gradient, result);
            const bool image_equal = equal(layer.image_source, y.image_source);
            storage_equal &= scalars_equal && gradient_equal && image_equal;
            visual_equal &= scalars_equal && (x.kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT ? gradient_equal : image_equal);
        }
        result.style_storage_equivalent &= storage_equal;
        if (!visual_equal) changed(result, StylePayloadPolicy<StylePayload::backgrounds>::impact);
    }
};
void diff_payloads(ComputedStyleReadView a, const aether_computed_style_t& b, StyleDiff& result) noexcept {
#define AETHER_COMPARE_PAYLOAD(name, impact, inherited) PayloadComparator<StylePayload::name>::compare(a, b, result);
    AETHER_STYLE_PAYLOAD_POLICIES(AETHER_COMPARE_PAYLOAD)
#undef AETHER_COMPARE_PAYLOAD
    const bool previous_context = !a.transforms().empty() || !a.backdrop_filters().empty();
    const bool candidate_context = b.transforms.count > 0 || b.backdrop_filters.count > 0;
    if (previous_context != candidate_context) changed(result, 1);
}

bool percentage(aether_css_length length, uint32_t flags) noexcept {
    return length.unit == AETHER_CSS_UNIT_PERCENT ||
        (length.unit == AETHER_CSS_UNIT_LINEAR && length.linear_percent != 0) || (flags & MEASURE_PERCENTAGE);
}
using Dependencies = std::array<uint32_t, style_dependency_catalog.size()>;
void set_dependency(Dependencies& values, std::string_view name, uint32_t flags, StyleDiff& result) noexcept {
    const size_t index = style_dependency_index(name);
    if (index == values.size()) result.unclassified = true;
    else values[index] |= flags;
    constexpr uint32_t known_flags = MEASURE_SELF_FONT | MEASURE_ROOT_FONT | MEASURE_VIEWPORT | MEASURE_PERCENTAGE | MEASURE_PARENT;
    if (flags & ~known_flags) result.unclassified = true;
}
void percentage_delta(StyleDiff& result, const Dependencies& a, const Dependencies& b,
        std::string_view name, aether_css_length previous, aether_css_length candidate) noexcept {
    const auto index = style_dependency_index(name);
    const auto policy = style_dependency_catalog[index].percentage;
    if (percentage(previous, a[index]) != percentage(candidate, b[index]))
        changed(result, policy == DependencyPercentagePolicy::dimension ? 7 :
            policy == DependencyPercentagePolicy::flex_basis ? 1 : 17);
}

struct SidecarInputs {
    const FrozenMeasures& previous;
    const measure_state_s& candidate;
    const StyleScalars& previous_style;
    const aether_computed_style_t& candidate_style;
    const FrozenColorMetadata& previous_colors;
    const color_resolution_state_s& candidate_colors;
};
template<StyleSidecar Field> void sidecar_changed(StyleDiff& result, bool different) noexcept {
    if (!different) return;
    using Policy = StyleSidecarPolicy<Field>;
    result.metadata_changed = true;
    if constexpr (Policy::kind == SidecarPolicyKind::value) changed(result, Policy::impact, Policy::inherited);
    if constexpr (Policy::kind == SidecarPolicyKind::inherited_metadata) result.inherited_metadata_changed = true;
}
// No generic implementation: extending the manifest without choosing a reader
// is a compilation error, even when an explicit metadata-only policy is added.
template<StyleSidecar> struct SidecarComparator;
#define AETHER_SIDECAR_COMPARATOR(name) \
    template<> struct SidecarComparator<StyleSidecar::name> { \
        static void compare(const SidecarInputs& input, StyleDiff& result) noexcept
#define AETHER_SIDECAR_END };
AETHER_SIDECAR_COMPARATOR(property_dependencies) {
    const auto& a = input.previous;
    const auto& b = input.candidate;
    Dependencies before{}, after{};
    for (const auto& dependency : a.property_dependencies()) {
        const auto name = dependency.name();
        set_dependency(before, std::string_view(name.c_str(), name.size()), dependency.flags(), result);
    }
    for (const auto& [name, flags] : b.property_dependencies) set_dependency(after, name, flags, result);
    sidecar_changed<StyleSidecar::property_dependencies>(result, before != after);
    // Exactly the current measure_has_percentage consumer set. Raw dependency
    // changes on every other registered property remain metadata-only.
#define AETHER_PERCENTAGE_DIMENSION(field, name) percentage_delta(result, before, after, name, input.previous_style.field, input.candidate_style.field)
    AETHER_PERCENTAGE_DIMENSION(width, "width"); AETHER_PERCENTAGE_DIMENSION(height, "height");
    AETHER_PERCENTAGE_DIMENSION(min_width, "min-width"); AETHER_PERCENTAGE_DIMENSION(min_height, "min-height");
    AETHER_PERCENTAGE_DIMENSION(max_width, "max-width"); AETHER_PERCENTAGE_DIMENSION(max_height, "max-height");
    AETHER_PERCENTAGE_DIMENSION(flex_basis.length, "flex-basis");
#undef AETHER_PERCENTAGE_DIMENSION
    percentage_delta(result, before, after, "row-gap", a.scalars().row_gap, b.row_gap);
    percentage_delta(result, before, after, "column-gap", a.scalars().column_gap, b.column_gap);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(contextual_lengths) {
    const auto& before = input.previous.contextual_lengths();
    const auto& after = input.candidate.contextual_lengths;
    bool different = before.size() != after.size();
    for (const auto& [name, tree] : before) {
        const auto found = after.find(name);
        if (found == after.end() || !aether::css::contextual_math_equal(tree, found->second)) {
            different = true; break;
        }
    }
    sidecar_changed<StyleSidecar::contextual_lengths>(result, different);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(combined_dependencies) {
    sidecar_changed<StyleSidecar::combined_dependencies>(result, input.previous.scalars().dependencies != input.candidate.dependencies);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(parent_font_size) {
    sidecar_changed<StyleSidecar::parent_font_size>(result, input.previous.scalars().parent_font_size != input.candidate.parent_font_size);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(self_font_size) {
    sidecar_changed<StyleSidecar::self_font_size>(result, input.previous.scalars().self_font_size != input.candidate.self_font_size);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(row_gap) {
    sidecar_changed<StyleSidecar::row_gap>(result, !equal(input.previous.scalars().row_gap, input.candidate.row_gap));
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(column_gap) {
    sidecar_changed<StyleSidecar::column_gap>(result, !equal(input.previous.scalars().column_gap, input.candidate.column_gap));
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(line_height) {
    const auto& m = input.previous.scalars();
    const auto& b = input.candidate;
    const auto same_kind = [&] {
        switch (m.line_height_kind) {
        case FrozenLineHeightKind::normal: return b.line_height.kind == measure_line_height_kind_e::normal;
        case FrozenLineHeightKind::number: return b.line_height.kind == measure_line_height_kind_e::number;
        case FrozenLineHeightKind::absolute_css_px: return b.line_height.kind == measure_line_height_kind_e::absolute_css_px;
        default: result.unclassified = true; return false;
        }
    }();
    switch (b.line_height.kind) {
    case measure_line_height_kind_e::normal: case measure_line_height_kind_e::number:
    case measure_line_height_kind_e::absolute_css_px: break;
    default: result.unclassified = true;
    }
    const float previous_used = m.line_height_kind == FrozenLineHeightKind::absolute_css_px ? m.line_height_value :
        m.self_font_size * (m.line_height_kind == FrozenLineHeightKind::normal ? 1.2f : m.line_height_value);
    const float candidate_used = b.line_height.kind == measure_line_height_kind_e::absolute_css_px ? b.line_height.value :
        b.self_font_size * (b.line_height.kind == measure_line_height_kind_e::normal ? 1.2f : b.line_height.value);
    sidecar_changed<StyleSidecar::line_height>(result, !same_kind || m.line_height_value != b.line_height.value || previous_used != candidate_used);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(background_positions) {
    const auto a = input.previous.background_positions();
    const auto& b = input.candidate.background_positions;
    bool different = a.size() != b.size();
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        different |= !equal(a[i].x, b[i].x) || !equal(a[i].y, b[i].y);
    sidecar_changed<StyleSidecar::background_positions>(result, different);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(background_sizes) {
    const auto a = input.previous.background_sizes();
    const auto& b = input.candidate.background_sizes;
    bool different = a.size() != b.size();
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        different |= a[i].kind != b[i].kind || !equal(a[i].x, b[i].x) || !equal(a[i].y, b[i].y);
    sidecar_changed<StyleSidecar::background_sizes>(result, different);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(document_inputs) {
    const auto& a = input.previous.document().values();
    const auto& b = input.candidate.document;
    if (!b) { result.unclassified = true; return; }
    sidecar_changed<StyleSidecar::document_inputs>(result, a.viewport_width != b->viewport_width ||
        a.viewport_height != b->viewport_height || a.initial_font_size != b->initial_font_size ||
        a.root_font_size != b->root_font_size || a.generation != b->generation || a.identity != b->identity);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(background_color_reference) {
    sidecar_changed<StyleSidecar::background_color_reference>(result, input.previous_colors.background() != input.candidate_colors.background_uses_current_color);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(border_color_references) {
    bool different = false;
    for (size_t i = 0; i < 4; ++i) different |= input.previous_colors.border(i) != input.candidate_colors.border_uses_current_color[i];
    sidecar_changed<StyleSidecar::border_color_references>(result, different);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(text_shadow_references) {
    const auto& a = input.previous_colors;
    const auto& b = input.candidate_colors.text_shadow_uses_current_color;
    bool different = a.text_shadow_count() != b.size();
    for (size_t i = 0; i < a.text_shadow_count() && i < b.size(); ++i) different |= a.text_shadow_at(i) != b[i];
    sidecar_changed<StyleSidecar::text_shadow_references>(result, different);
} AETHER_SIDECAR_END
AETHER_SIDECAR_COMPARATOR(box_shadow_references) {
    const auto& a = input.previous_colors;
    const auto& b = input.candidate_colors.box_shadow_uses_current_color;
    bool different = a.box_shadow_count() != b.size();
    for (size_t i = 0; i < a.box_shadow_count() && i < b.size(); ++i) different |= a.box_shadow_at(i) != b[i];
    sidecar_changed<StyleSidecar::box_shadow_references>(result, different);
} AETHER_SIDECAR_END
#undef AETHER_SIDECAR_COMPARATOR
#undef AETHER_SIDECAR_END
} // namespace

StyleDiff classify_style_diff(ComputedStyleReadView previous, const FrozenMeasures& previous_measures,
    const FrozenColorMetadata& previous_colors, const aether_computed_style_t& candidate,
    const measure_state_s& candidate_measures, const color_resolution_state_s& candidate_colors) noexcept {
    StyleDiff result;
    const auto& scalars = previous.scalars();
#define AETHER_DIFF_SCALAR(type, name) \
    stored_delta(result, equal(scalars.name, candidate.name), StyleFieldPolicy<StyleField::name>::impact, StyleFieldPolicy<StyleField::name>::inherited);
    AETHER_STYLE_SCALAR_FIELDS(AETHER_DIFF_SCALAR)
#undef AETHER_DIFF_SCALAR
    bool family_equal = true, family_storage_equal = true, terminated = false;
    static_assert(sizeof(candidate.font_family) == 64);
    for (size_t i = 0; i < scalars.font_family.size(); ++i) {
        const char a = scalars.font_family[i], b = candidate.font_family[i];
        family_storage_equal &= a == b;
        if (!terminated) {
            family_equal &= a == b;
            if (a == '\0' || b == '\0') terminated = true;
        }
    }
    if (!family_equal) changed(result, StyleFieldPolicy<StyleField::font_family>::impact, true);
    result.style_storage_equivalent &= family_storage_equal;
    diff_payloads(previous, candidate, result);
    const SidecarInputs sidecars{previous_measures, candidate_measures, scalars, candidate, previous_colors, candidate_colors};
#define AETHER_COMPARE_SIDECAR(name) SidecarComparator<StyleSidecar::name>::compare(sidecars, result);
    AETHER_STYLE_SIDECAR_FIELDS(AETHER_COMPARE_SIDECAR)
#undef AETHER_COMPARE_SIDECAR
    if (result.unclassified) {
        result.own_impact = all_impact;
        result.inherited_value_impact = all_impact;
        result.style_storage_equivalent = false;
    }
    return result;
}
} // namespace aether::style
