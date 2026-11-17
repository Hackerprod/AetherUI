#pragma once
#include "../text/shaped_run.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace aether::rendering::html_ltr {
// Private typed failures: callers translate status at the C boundary. These
// helpers allocate nothing and must run BEFORE ShapedRun/staging construction.
struct BoundaryError { aether_status_t status; };
inline constexpr double coordinate_limit = 65536.0;
inline constexpr std::int64_t transport_limit = 4194304;
[[noreturn]] inline void boundary_fail(aether_status_t status) { throw BoundaryError{status}; }
inline void finite_authored(double value) {
    // NaN cannot arise from admitted finite positive font metrics: evidence bug.
    if (std::isnan(value)) boundary_fail(AETHER_ERROR_INTERNAL);
    if (!std::isfinite(value)) boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
}
inline double logical_extent(double value) {
    finite_authored(value);
    if (value < 0.0 || value > coordinate_limit) boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
    return value;
}
inline double absolute_coordinate(double value) {
    finite_authored(value);
    if (value < -coordinate_limit || value > coordinate_limit) boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
    return value;
}
inline aether_text_metrics_t checked_metrics(double width, double height, double baseline) {
    logical_extent(width); logical_extent(height);
    if (std::isnan(baseline)) boundary_fail(AETHER_ERROR_INTERNAL);
    if (!std::isfinite(baseline) || baseline < 0.0 || baseline > height)
        boundary_fail(AETHER_ERROR_UNSUPPORTED);
    return {static_cast<float>(width), static_cast<float>(height), static_cast<float>(baseline)};
}
inline std::uint64_t checked_design_add(std::uint64_t sum, std::uint64_t delta) {
    if (delta > std::numeric_limits<std::uint64_t>::max() - sum)
        boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
    return sum + delta;
}
inline double scaled_design_prefix(std::uint64_t sum, double scale) {
    finite_authored(scale);
    if (scale <= 0.0 || scale > coordinate_limit) boundary_fail(AETHER_ERROR_INTERNAL);
    // Real PROFILE_1 scale is at least 8/16384. Thus every admissible prefix is
    // far below 2^53. Reject larger integers before converting, never round them
    // into range. This is not an alternate arbitrary tiny-scale font profile.
    constexpr std::uint64_t max_exact_double_integer = UINT64_C(1) << 53;
    if (sum > max_exact_double_integer) boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
    const double design = static_cast<double>(sum);
    if (design > coordinate_limit / scale) boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
    return logical_extent(design * scale);
}
inline std::int32_t checked_component(std::int64_t value) {
    // Comparison, not abs: INT32_MIN and INT64_MIN are safe inputs.
    if (value < -transport_limit || value > transport_limit ||
        value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max())
        boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
    return static_cast<std::int32_t>(value);
}
// Provenance: H contract + H-A2's absolute-prefix rule (design units × scale,
// quantize(x + prefix), subtract endpoints). Fresh pure implementation; no
// selected_text_product, DocumentSlice, RunCanvas or synthetic authority import.
inline std::int32_t quantize_absolute(double logical) {
    absolute_coordinate(logical); // MUST precede multiplication and rounding.
    const double rounded = std::round(logical * 64.0); // ties away from zero
    if (!std::isfinite(rounded) || rounded < -double(transport_limit) ||
        rounded > double(transport_limit) ||
        rounded < double(std::numeric_limits<std::int32_t>::min()) ||
        rounded > double(std::numeric_limits<std::int32_t>::max()))
        boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
    return static_cast<std::int32_t>(rounded);
}
inline std::int32_t quantize_endpoint(double origin, double prefix) {
    absolute_coordinate(origin); logical_extent(prefix);
    // Both operands already bounded. The double sum cannot overflow and still
    // must pass the coordinate bound before quantization or any integer cast.
    return quantize_absolute(origin + prefix);
}
inline std::int32_t endpoint_delta(std::int32_t current, std::int32_t next) {
    checked_component(current); checked_component(next);
    const std::int64_t delta = std::int64_t(next) - std::int64_t(current);
    if (delta < 0 || delta > transport_limit) boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
    return static_cast<std::int32_t>(delta);
}
inline std::uint8_t raster_phase(std::int32_t quantized) {
    checked_component(quantized);
    const auto remainder = quantized % 64;
    return static_cast<std::uint8_t>(remainder < 0 ? remainder + 64 : remainder);
}
inline void reserve_count(std::size_t& used, std::size_t count, std::size_t limit) {
    if (used > limit || count > limit - used) boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
    used += count; // subtraction guard also proves addition cannot overflow.
}
inline bool scalar_start(std::string_view source, std::uint32_t cluster) noexcept {
    if (cluster >= source.size()) return false;
    const auto byte = static_cast<unsigned char>(source[cluster]);
    return byte < 0x80 || (byte >= 0xc2 && byte <= 0xf4);
}
inline void validate_glyph(const text::Glyph& glyph, std::string_view admitted_utf8) {
    checked_component(glyph.advance_x); checked_component(glyph.advance_y);
    checked_component(glyph.offset_x); checked_component(glyph.offset_y);
    // Admission already validates source UTF-8. A continuation-byte cluster is
    // generated-evidence corruption, not an authored unsupported character.
    if (!scalar_start(admitted_utf8, glyph.cluster)) boundary_fail(AETHER_ERROR_INTERNAL);
}
inline void validate_run_transport(std::string_view admitted_utf8,
    std::span<const text::Glyph> glyphs, double width, double height, double baseline) {
    if (admitted_utf8.size() > text::max_text_bytes || glyphs.size() > text::max_glyphs)
        boundary_fail(AETHER_ERROR_LIMIT_EXCEEDED);
    if (admitted_utf8.empty() || (glyphs.empty() && admitted_utf8.find_first_not_of(' ') != std::string_view::npos))
        boundary_fail(AETHER_ERROR_INTERNAL);
    (void)checked_metrics(width,height,baseline);
    for (const auto& glyph : glyphs) validate_glyph(glyph,admitted_utf8);
}
} // namespace aether::rendering::html_ltr
