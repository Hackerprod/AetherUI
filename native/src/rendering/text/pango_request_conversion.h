#pragma once
#include "text_request.h"
namespace aether::rendering::text {
enum class PangoError { unsupported_constraint, input_limit, numeric_error, wrong_thread,
 allocation_failed, native_failure, invalid_target, invalid_draw, wrong_purpose };
struct PangoInput { double absolute_size{}; int width{-1}; };
using PangoInputResult=std::variant<PangoInput,PangoError>;
PangoInputResult pango_input(const Request&,int scale) noexcept;
struct LogicalMetrics { double x{},y{},width{},height{},baseline{}; std::optional<float> used_line_height; };
using MetricsResult=std::variant<LogicalMetrics,PangoError>;
MetricsResult pango_metrics(int x,int y,int width,int height,int baseline,int scale,
                           std::optional<float> line) noexcept;
struct DrawPlacement { double x{},y{},width{},height{},layout_x{},layout_y{};bool empty{}; };
using PlacementResult=std::variant<DrawPlacement,PangoError>;
// Requires a request previously accepted by pango_input; this helper only
// validates placement/metrics/color, not provider capabilities or input limits.
PlacementResult pango_placement(const Request&,const LogicalMetrics&,double x,double y,
                                prep::Color) noexcept;
}
