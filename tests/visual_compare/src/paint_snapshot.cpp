#include "aether_visual_compare/paint_snapshot.hpp"
#include "aether_visual_compare/resource_manifest.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace avc::preparation {
namespace {
bool nonnegative(float v) noexcept { return std::isfinite(v) && v >= 0; }
bool valid(Rect r) noexcept {
 return std::isfinite(r.x) && std::isfinite(r.y) && nonnegative(r.width) && nonnegative(r.height);
}
bool valid(Radii r) noexcept {
 return nonnegative(r.top_left_x) && nonnegative(r.top_left_y) &&
 nonnegative(r.top_right_x) && nonnegative(r.top_right_y) &&
 nonnegative(r.bottom_right_x) && nonnegative(r.bottom_right_y) &&
 nonnegative(r.bottom_left_x) && nonnegative(r.bottom_left_y);
}
bool valid(Color c) noexcept {
 return nonnegative(c.r) && c.r <= 1 && nonnegative(c.g) && c.g <= 1 &&
 nonnegative(c.b) && c.b <= 1 && nonnegative(c.a) && c.a <= 1;
}
bool valid(Matrix m) noexcept {
 return std::isfinite(m.m11) && std::isfinite(m.m12) && std::isfinite(m.m21) &&
 std::isfinite(m.m22) && std::isfinite(m.dx) && std::isfinite(m.dy);
}
bool align_valid(std::int32_t a) noexcept {
 switch (a) {
 case AETHER_TEXT_ALIGN_START: case AETHER_TEXT_ALIGN_CENTER: case AETHER_TEXT_ALIGN_END:
 case AETHER_TEXT_ALIGN_LEFT: case AETHER_TEXT_ALIGN_RIGHT: case AETHER_TEXT_ALIGN_JUSTIFY: return true;
 default: return false;
 }
}
bool cancelled(Cancellation* c) noexcept { return c && c->cancelled(); }
std::optional<Matrix> compose(Matrix m, Matrix c) noexcept {
 const auto mul = [](float a, float b) { return static_cast<double>(a) * b; };
 const std::array<double,6> v{
 mul(m.m11,c.m11)+mul(m.m12,c.m21), mul(m.m11,c.m12)+mul(m.m12,c.m22),
 mul(m.m21,c.m11)+mul(m.m22,c.m21), mul(m.m21,c.m12)+mul(m.m22,c.m22),
 mul(m.dx,c.m11)+mul(m.dy,c.m21)+c.dx, mul(m.dx,c.m12)+mul(m.dy,c.m22)+c.dy};
 for (double n : v) {
  if (!std::isfinite(n) || n > std::numeric_limits<float>::max() || n < -std::numeric_limits<float>::max()) return {};
 }
 return Matrix{static_cast<float>(v[0]),static_cast<float>(v[1]),static_cast<float>(v[2]),
 static_cast<float>(v[3]),static_cast<float>(v[4]),static_cast<float>(v[5])};
}
}
int command_tag(const Command& command) noexcept {
 constexpr std::array<int,9> tags{AETHER_PAINT_FILL_ROUNDED_RECT,AETHER_PAINT_STROKE_ROUNDED_RECT,
 AETHER_PAINT_DRAW_TEXT,AETHER_PAINT_PUSH_CLIP,AETHER_PAINT_POP_CLIP,AETHER_PAINT_PUSH_TRANSFORM,
 AETHER_PAINT_POP_TRANSFORM,AETHER_PAINT_BEGIN_PAINT_CHUNK,AETHER_PAINT_END_PAINT_CHUNK};
 return command.valueless_by_exception() ? -1 : tags[command.index()];
}
std::optional<ErrorCode> validate_command(const Command& command, std::size_t& bytes) noexcept {
 if (command.valueless_by_exception()) return ErrorCode::INVALID_PAYLOAD;
 return std::visit([&bytes](const auto& value) -> std::optional<ErrorCode> {
  using T = std::decay_t<decltype(value)>;
  if constexpr (std::is_same_v<T,Fill>) {
   if (!valid(value.rect) || !valid(value.radii) || !valid(value.color)) return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,Stroke>) {
   if (!valid(value.rect) || !valid(value.radii) || !nonnegative(value.stroke_width) || !valid(value.color)) return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,Text>) {
   if (value.text.size() > max_text_bytes || value.font_family.size() >= 64) return ErrorCode::LIMIT_EXCEEDED;
   if (value.text.find('\0') != std::string::npos || value.font_family.find('\0') != std::string::npos) return ErrorCode::INVALID_PAYLOAD;
   if (!avc::valid_utf8(value.text) || !avc::valid_utf8(value.font_family)) return ErrorCode::INVALID_UTF8;
   // These are adapter restrictions, not guarantees made by the core ABI.
   if (!valid(value.rect) || value.font_family.empty() || !std::isfinite(value.font_size) ||
       value.font_size <= 0 || value.font_weight < 1 || value.font_weight > 1000 ||
       !align_valid(value.text_align) || !valid(value.color) ||
       (value.wrap != AETHER_TRUE && value.wrap != AETHER_FALSE)) return ErrorCode::INVALID_PAYLOAD;
   const auto size = value.text.size() + value.font_family.size();
   if (bytes > max_string_bytes || size > max_string_bytes - bytes) return ErrorCode::LIMIT_EXCEEDED;
   bytes += size;
  } else if constexpr (std::is_same_v<T,PushClip>) {
   if (!valid(value.rect) || !valid(value.radii)) return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,PushTransform>) {
   if (!valid(value.matrix)) return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,BeginChunk> || std::is_same_v<T,EndChunk>) {
   if (value.id <= 0) return ErrorCode::INVALID_PAYLOAD;
  }
  return {};
 }, command);
}
PreparedFrame::PreparedFrame(std::vector<PreparedEvent>&& events) noexcept : events_(std::move(events)) {}
PrepareResult prepare(const Snapshot& snapshot, Cancellation* cancellation) noexcept {
 std::optional<std::size_t> index;
 std::optional<int> tag;
 const auto error = [&](ErrorCode code) { return Error{code,index,tag}; };
 try {
  if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
  if (snapshot.commands.size() > max_commands) return error(ErrorCode::COMMAND_COUNT);
  std::vector<PreparedEvent> events;
  events.reserve(snapshot.commands.size());
  std::vector<Matrix> transforms;
  std::vector<std::size_t> clips;
  std::vector<std::int64_t> chunks;
  Matrix current = identity;
  std::size_t bytes = 0;
  for (std::size_t i=0; i<snapshot.commands.size(); ++i) {
   index = i;
   const auto& command = snapshot.commands[i];
   tag = command_tag(command);
   if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
   if (auto e = validate_command(command,bytes)) return error(*e);
   const bool pushes = std::holds_alternative<PushTransform>(command) ||
    std::holds_alternative<PushClip>(command) || std::holds_alternative<BeginChunk>(command);
   if (pushes && transforms.size()+clips.size()+chunks.size() >= max_scope_depth) return error(ErrorCode::LIMIT_EXCEEDED);
   if (const auto* push = std::get_if<PushTransform>(&command)) {
    auto result = compose(push->matrix,current);
    if (!result) return error(ErrorCode::NUMERIC_RANGE);
    transforms.push_back(current);
    current = *result;
   } else if (std::holds_alternative<PopTransform>(command)) {
    if (transforms.empty()) return error(ErrorCode::STACK_UNDERFLOW);
    if (std::any_of(clips.begin(),clips.end(),[&](auto depth) { return depth >= transforms.size(); })) return error(ErrorCode::CLIP_TRANSFORM_CROSSING);
    current = transforms.back();
    transforms.pop_back();
   } else if (std::holds_alternative<PushClip>(command)) {
    clips.push_back(transforms.size());
   } else if (std::holds_alternative<PopClip>(command)) {
    if (clips.empty()) return error(ErrorCode::STACK_UNDERFLOW);
    clips.pop_back();
   } else if (const auto* begin = std::get_if<BeginChunk>(&command)) {
    chunks.push_back(begin->id);
   } else if (const auto* end = std::get_if<EndChunk>(&command)) {
    if (chunks.empty()) return error(ErrorCode::STACK_UNDERFLOW);
    if (chunks.back() != end->id) return error(ErrorCode::STACK_MISMATCH);
    chunks.pop_back();
   }
   events.push_back(PreparedEvent{command,current,transforms.size()});
  }
  index.reset(); tag.reset();
  if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
  if (!transforms.empty() || !clips.empty() || !chunks.empty()) return error(ErrorCode::UNCLOSED_SCOPE);
  return PreparedFrame(std::move(events));
 } catch (const std::bad_alloc&) { return error(ErrorCode::OUT_OF_MEMORY); }
}
std::optional<Error> replay(const PreparedFrame& frame, CommandSink& sink, Cancellation* cancellation) noexcept {
 std::optional<std::size_t> index;
 std::optional<int> tag;
 const auto error = [&](ErrorCode code) { return Error{code,index,tag}; };
 try {
  if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
  for (std::size_t i=0; i<frame.events().size(); ++i) {
   index = i; tag = command_tag(frame.events()[i].command);
   if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
   if (!sink.accept(frame.events()[i])) return error(ErrorCode::SINK_FAILED);
  }
  index.reset(); tag.reset();
  if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
  return {};
 } catch (const std::bad_alloc&) { return error(ErrorCode::OUT_OF_MEMORY); }
 catch (...) { return error(ErrorCode::SINK_FAILED); }
}
} // namespace avc::preparation
