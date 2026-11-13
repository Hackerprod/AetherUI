#pragma once
#include <aether/aether.h>
#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace aether::rendering::text { class ShapedRun; }
namespace aether::rendering::preparation {
inline constexpr std::size_t max_commands = 65536;
inline constexpr std::size_t max_scope_depth = 256;
inline constexpr std::size_t max_text_bytes = 65536;
inline constexpr std::size_t max_string_bytes = 8 * 1024 * 1024;
enum class ErrorCode { INVALID_API, INVALID_LIST, ABI_VERSION, ABI_CAPABILITY,
 ABI_SIZE, COMMAND_COUNT, GET_COMMAND_FAILED, UNKNOWN_TAG, UNSUPPORTED_TAG,
 LIMIT_EXCEEDED, INVALID_PAYLOAD, INVALID_UTF8, STACK_UNDERFLOW, STACK_MISMATCH,
 CLIP_TRANSFORM_CROSSING, UNCLOSED_SCOPE, NUMERIC_RANGE, CANCELLED,
 OUT_OF_MEMORY, SINK_FAILED, UnsupportedValidationDomain };
// Allocation-free errors never include document content.
struct Error { ErrorCode code; std::optional<std::size_t> index{}; std::optional<int> tag{}; };
// Implementations must not throw. Synchronous callbacks cannot be interrupted.
struct Cancellation { virtual ~Cancellation() = default; virtual bool cancelled() noexcept = 0; };
using Rect = aether_rectf;
using Radii = aether_corner_radii;
using Color = aether_color_rgba;
using Matrix = aether_transform_matrix;
inline constexpr Matrix identity{1,0,0,1,0,0};
inline constexpr std::size_t max_gradient_stops = 64;
inline constexpr std::size_t max_frame_gradient_stops = 65536;
struct GradientStop { float offset{}; Color color{}; };
// Owning private payload: only the trusted native adapter admits CSS evidence.
struct LinearGradientFill { Rect rect{}, gradient_rect{}; Radii radii{}; float direction_x{}, direction_y{}, opacity{1}; std::vector<GradientStop> stops; };
struct Fill { Rect rect{}; Radii radii{}; Color color{}; };
struct Stroke { Rect rect{}; Radii radii{}; float stroke_width{}; Color color{}; };
struct NativeText {
 std::shared_ptr<const aether::rendering::text::ShapedRun> run;
 float baseline{}; // absolute content-space Y, before transforms and scroll
};
struct Text { std::string text; Rect rect{}; std::string font_family; float font_size{};
 std::int32_t font_weight{}; std::int32_t text_align{}; Color color{}; aether_bool_t wrap{}; std::optional<NativeText> native; };
struct BorderSide { bool present{}; int style{}; Color color{}; };
struct BorderGroup { Rect outer{}, inner{}; Radii outer_radii{}, inner_radii{}; std::array<BorderSide,4> sides{}; };
struct BoxShadow { Rect rect{}; Radii radii{}; aether_shadow shadow{}; };
struct PushClip { Rect rect{}; Radii radii{}; };
struct PopClip {};
struct PushTransform { Matrix matrix{identity}; };
struct PopTransform {};
struct BeginChunk { std::int64_t id{}; };
struct EndChunk { std::int64_t id{}; };
struct BeginOpacity { std::uint64_t id{}, parent{}; float opacity{1}; };
struct EndOpacity { std::uint64_t id{}; };
struct PushScroll { std::int64_t id{}; double x{},y{}; };
struct PopScroll {};
struct ScrollChrome { aether_scrollbar_geometry_t geometry{}; };
using Command = std::variant<Fill, Stroke, Text, PushClip, PopClip, PushTransform,
 PopTransform, BeginChunk, EndChunk, BoxShadow, BorderGroup, BeginOpacity, EndOpacity, PushScroll, PopScroll, ScrollChrome, LinearGradientFill>;
struct Snapshot { std::vector<Command> commands; bool native_text_required{}; };
int command_tag(const Command&) noexcept;
// Shared field validation; updates string budget only on success. Does not check scopes.
std::optional<ErrorCode> validate_text(const Text&, std::size_t& string_bytes) noexcept;
std::optional<ErrorCode> validate_command(const Command&, std::size_t& string_bytes) noexcept;
struct PreparedEvent { Command command; Matrix matrix{identity}; std::size_t transform_depth{}; double scroll_x{}, scroll_y{}; std::size_t scroll_depth{}; };
class PreparedFrame;
using PrepareResult = std::variant<PreparedFrame, Error>;
class PreparedFrame {
 std::vector<PreparedEvent> events_;
 explicit PreparedFrame(std::vector<PreparedEvent>&& events) noexcept;
 friend PrepareResult prepare(const Snapshot&, Cancellation*) noexcept;
 public:
 PreparedFrame(const PreparedFrame&) = delete;
 PreparedFrame& operator=(const PreparedFrame&) = delete;
 PreparedFrame(PreparedFrame&&) noexcept = default;
 PreparedFrame& operator=(PreparedFrame&&) = delete;
 const std::vector<PreparedEvent>& events() const noexcept { return events_; }
};
PrepareResult prepare(const Snapshot&, Cancellation* = nullptr) noexcept;
// A sink may allocate/throw: bad_alloc maps to OUT_OF_MEMORY; other exceptions
// and false map to SINK_FAILED. References are borrowed for this call only.
struct CommandSink { virtual ~CommandSink() = default; virtual bool accept(const PreparedEvent&) = 0; };
// No rollback: failure/cancellation can leave a partially populated sink.
std::optional<Error> replay(const PreparedFrame&, CommandSink&, Cancellation* = nullptr) noexcept;
} // namespace aether::rendering::preparation
