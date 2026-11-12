#pragma once
#include "../native_frame.h"
#include <aether/rendering/preparation/paint_snapshot.hpp>
#include <array>
namespace aether::rendering::cairo_adapter {
namespace prep = aether::rendering::preparation;
enum class MatrixKind { valid, singular, invalid };
struct Matrix { double xx{}, yx{}, xy{}, yy{}, x0{}, y0{}; };
struct ClassifiedMatrix { MatrixKind kind{MatrixKind::invalid}; Matrix value{}; };
ClassifiedMatrix classify(prep::Matrix matrix, float dpi) noexcept;
enum class SegmentKind { move, line, curve, close };
struct Segment { SegmentKind kind{}; std::array<double,6> coordinates{}; };
struct Path { std::array<Segment,10> segments{}; std::size_t count{}; };
std::optional<Path> rounded_path(prep::Rect, prep::Radii) noexcept;
enum class Action { fill, stroke, push_clip, empty_clip, pop_clip, none };
struct Operation { Action action{Action::none}; Matrix matrix{}; Path path{};
 prep::Color color{}; double width{}; };
enum class ErrorCode { unsupported_command, invalid_geometry, invalid_matrix, invalid_color,
 allocation_failed };
struct Error { ErrorCode code; std::size_t index{}; int tag{-1}; };
using Plan = std::vector<Operation>;
using PlanResult = std::variant<Plan,Error>;
PlanResult preflight(const prep::PreparedFrame&, float dpi, prep::Color clear) noexcept;
}
