#pragma once
#include "real_font_binding.h"

namespace aether::rendering::text::truetype {
enum class VerticalMetricPolicy { hhea, os2_typo };
// Design units, not pixels. Descent is a positive distance; line gap stays signed.
// Explicit CSS line-height must not add line_gap a second time.
struct FontVerticalMetrics {
 std::uint16_t units_per_em{};
 std::int32_t ascent{}, descent{}, line_gap{};
 VerticalMetricPolicy policy{VerticalMetricPolicy::hhea};
 bool os2_present{}, use_typo_metrics_bit{};
 std::uint16_t os2_version{};
};
using FontVerticalMetricsResult=std::variant<FontVerticalMetrics,BindingError>;
// Bounded, allocation-free reading from the immutable, validated binding source.
// Unknown OS/2 versions are unsupported, truncated present tables are malformed.
FontVerticalMetricsResult read_vertical_metrics(
 const std::shared_ptr<const RealFontBinding>& binding) noexcept;
}
