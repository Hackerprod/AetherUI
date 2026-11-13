#include "aether/rendering/preparation/paint_snapshot.hpp"
#include "aether/rendering/preparation/detail/utf8.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>
#include "native_text_validation.h"

namespace aether::rendering::preparation {
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
 constexpr std::array<int,16> tags{AETHER_PAINT_FILL_ROUNDED_RECT,AETHER_PAINT_STROKE_ROUNDED_RECT,
 AETHER_PAINT_DRAW_TEXT,AETHER_PAINT_PUSH_CLIP,AETHER_PAINT_POP_CLIP,AETHER_PAINT_PUSH_TRANSFORM,
 AETHER_PAINT_POP_TRANSFORM,AETHER_PAINT_BEGIN_PAINT_CHUNK,AETHER_PAINT_END_PAINT_CHUNK,AETHER_PAINT_DRAW_BOX_SHADOW,AETHER_PAINT_FILL_BORDER_SIDE,-100,-101,AETHER_PAINT_PUSH_SCROLL_OFFSET,AETHER_PAINT_POP_SCROLL_OFFSET,AETHER_PAINT_DRAW_SCROLLBAR_CHROME};
 return command.valueless_by_exception() ? -1 : tags[command.index()];
}
std::optional<ErrorCode> validate_text(const Text& value, std::size_t& bytes) noexcept {
   if (value.text.size() > max_text_bytes || value.font_family.size() >= 64) return ErrorCode::LIMIT_EXCEEDED;
   if (value.text.find('\0') != std::string::npos || value.font_family.find('\0') != std::string::npos) return ErrorCode::INVALID_PAYLOAD;
   if (!detail::valid_utf8(value.text) || !detail::valid_utf8(value.font_family)) return ErrorCode::INVALID_UTF8;
   // These are adapter restrictions, not guarantees made by the core ABI.
   if (!valid(value.rect) || value.font_family.empty() || !std::isfinite(value.font_size) ||
       value.font_size <= 0 || value.font_weight < 1 || value.font_weight > 1000 ||
       !align_valid(value.text_align) || !valid(value.color) ||
       (value.wrap != AETHER_TRUE && value.wrap != AETHER_FALSE)) return ErrorCode::INVALID_PAYLOAD;
   if(auto e=validate_native_text(value))return e;
   const auto size = value.text.size() + value.font_family.size();
   if (bytes > max_string_bytes || size > max_string_bytes - bytes) return ErrorCode::LIMIT_EXCEEDED;
   bytes += size;
   return {};
}
std::optional<ErrorCode> validate_command(const Command& command, std::size_t& bytes) noexcept {
 if (command.valueless_by_exception()) return ErrorCode::INVALID_PAYLOAD;
 return std::visit([&bytes](const auto& value) -> std::optional<ErrorCode> {
  using T = std::decay_t<decltype(value)>;
  if constexpr (std::is_same_v<T,PushScroll>) {
   if(value.id<=0 || !std::isfinite(value.x)||!std::isfinite(value.y)||value.x<0||value.y<0||value.x>65536||value.y>65536)return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,ScrollChrome>) {
   const auto& g=value.geometry;
   const auto bounded=[](Rect r){return valid(r)&&std::abs(double(r.x))<=65536&&std::abs(double(r.y))<=65536&&std::abs(double(r.x)+r.width)<=65536&&std::abs(double(r.y)+r.height)<=65536;};
   if((g.horizontal_visible!=0 && g.horizontal_visible!=1)||(g.vertical_visible!=0 && g.vertical_visible!=1)||!bounded(g.horizontal_track)||!bounded(g.vertical_track)||!bounded(g.horizontal_thumb)||!bounded(g.vertical_thumb)||!bounded(g.corner))return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,BeginOpacity>) {
   if(!value.id || !nonnegative(value.opacity)||value.opacity>1)return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,EndOpacity>) {
   if(!value.id)return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,Fill>) {
   if (!valid(value.rect) || !valid(value.radii) || !valid(value.color)) return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,Stroke>) {
   if (!valid(value.rect) || !valid(value.radii) || !nonnegative(value.stroke_width) || !valid(value.color)) return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,BorderGroup>) {
   if (!valid(value.outer) || !valid(value.inner) || !valid(value.outer_radii) || !valid(value.inner_radii)) return ErrorCode::INVALID_PAYLOAD;
   bool any=false;
   for(const auto& side:value.sides) if(side.present) {
    any=true; if(!valid(side.color) || side.style<AETHER_BORDER_STYLE_NONE || side.style>AETHER_BORDER_STYLE_OUTSET) return ErrorCode::INVALID_PAYLOAD;
   }
   if(!any) return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,BoxShadow>) {
   if (!valid(value.rect) || !valid(value.radii) || !valid(value.shadow.color) ||
       !std::isfinite(value.shadow.offset_x) || !std::isfinite(value.shadow.offset_y) ||
       !std::isfinite(value.shadow.spread_radius) || !nonnegative(value.shadow.blur_radius) ||
       (value.shadow.inset != AETHER_FALSE && value.shadow.inset != AETHER_TRUE)) return ErrorCode::INVALID_PAYLOAD;
  } else if constexpr (std::is_same_v<T,Text>) {
   return validate_text(value, bytes);
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
  struct ScrollScope { double x,y; std::size_t clips,transforms,chunks,groups; };
  std::vector<ScrollScope> scrolls; double scroll_x=0,scroll_y=0;
  struct Group { std::uint64_t id; std::size_t transforms,clips,chunks,scrolls; };
  std::vector<Group> groups;
  std::uint64_t last_id=0;
  Matrix current = identity;
  std::size_t bytes = 0;
  NativeTextBudget native_budget;
  for (std::size_t i=0; i<snapshot.commands.size(); ++i) {
   index = i;
   const auto& command = snapshot.commands[i];
   tag = command_tag(command);
   if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
   if (auto e = validate_command(command,bytes)) return error(*e);
   if(const auto* t=std::get_if<Text>(&command)) {
    if(snapshot.native_text_required && !t->native)return error(ErrorCode::INVALID_PAYLOAD);
    if(auto e=native_budget.add(*t))return error(*e);
   }
   const bool pushes = std::holds_alternative<PushTransform>(command) ||
    std::holds_alternative<PushClip>(command) || std::holds_alternative<BeginChunk>(command) || std::holds_alternative<PushScroll>(command);
   if (pushes && transforms.size()+clips.size()+chunks.size()+scrolls.size() >= max_scope_depth) return error(ErrorCode::LIMIT_EXCEEDED);
   if(const auto* push=std::get_if<PushScroll>(&command)) {
    scrolls.push_back({scroll_x,scroll_y,clips.size(),transforms.size(),chunks.size(),groups.size()});
    scroll_x-=push->x;scroll_y-=push->y;
    if(std::abs(scroll_x)>65536||std::abs(scroll_y)>65536)return error(ErrorCode::NUMERIC_RANGE);
   } else if(std::holds_alternative<PopScroll>(command)) {
    if(scrolls.empty())return error(ErrorCode::STACK_UNDERFLOW);
    const auto top=scrolls.back();
    if(top.clips!=clips.size()||top.transforms!=transforms.size()||top.chunks!=chunks.size()||top.groups!=groups.size())return error(ErrorCode::STACK_MISMATCH);
    scroll_x=top.x;scroll_y=top.y;scrolls.pop_back();
   } else if (const auto* begin=std::get_if<BeginOpacity>(&command)) {
    if(begin->id<=last_id || begin->parent!=(groups.empty()?0:groups.back().id))return error(ErrorCode::STACK_MISMATCH);
    last_id=begin->id;
    if(groups.size()>=8)return error(ErrorCode::LIMIT_EXCEEDED);
    groups.push_back({begin->id,transforms.size(),clips.size(),chunks.size(),scrolls.size()});
   } else if(const auto* end=std::get_if<EndOpacity>(&command)) {
    if(groups.empty())return error(ErrorCode::STACK_UNDERFLOW);
    const auto g=groups.back();
    if(g.id!=end->id || g.transforms!=transforms.size() || g.clips!=clips.size() || g.chunks!=chunks.size() || g.scrolls!=scrolls.size())return error(ErrorCode::STACK_MISMATCH);
    groups.pop_back();
   } else if (const auto* push = std::get_if<PushTransform>(&command)) {
    auto result = compose(push->matrix,current);
    if (!result) return error(ErrorCode::NUMERIC_RANGE);
    transforms.push_back(current);
    current = *result;
   } else if (std::holds_alternative<PopTransform>(command)) {
    if (transforms.empty()) return error(ErrorCode::STACK_UNDERFLOW);
    if(!scrolls.empty() && transforms.size()<=scrolls.back().transforms)return error(ErrorCode::STACK_MISMATCH);
    if(!groups.empty() && transforms.size()<=groups.back().transforms)return error(ErrorCode::STACK_MISMATCH);
    if (std::any_of(clips.begin(),clips.end(),[&](auto depth) { return depth >= transforms.size(); })) return error(ErrorCode::CLIP_TRANSFORM_CROSSING);
    current = transforms.back();
    transforms.pop_back();
   } else if (std::holds_alternative<PushClip>(command)) {
    clips.push_back(transforms.size());
   } else if (std::holds_alternative<PopClip>(command)) {
    if (clips.empty()) return error(ErrorCode::STACK_UNDERFLOW);
    if(!scrolls.empty() && clips.size()<=scrolls.back().clips)return error(ErrorCode::STACK_MISMATCH);
    if(!groups.empty() && clips.size()<=groups.back().clips)return error(ErrorCode::STACK_MISMATCH);
    clips.pop_back();
   } else if (const auto* begin = std::get_if<BeginChunk>(&command)) {
    chunks.push_back(begin->id);
   } else if (const auto* end = std::get_if<EndChunk>(&command)) {
    if (chunks.empty()) return error(ErrorCode::STACK_UNDERFLOW);
    if(!scrolls.empty() && chunks.size()<=scrolls.back().chunks)return error(ErrorCode::STACK_MISMATCH);
    if(!groups.empty() && chunks.size()<=groups.back().chunks)return error(ErrorCode::STACK_MISMATCH);
    if (chunks.back() != end->id) return error(ErrorCode::STACK_MISMATCH);
    chunks.pop_back();
   }
   if(const auto* chrome=std::get_if<ScrollChrome>(&command)) {
    const auto emit=[&](Rect r, bool rounded,float alpha){
     if(r.width==0||r.height==0)return;
     const float rad=rounded?std::min(r.width,r.height)/2:0;
     Radii radii{rad,rad,rad,rad,rad,rad,rad,rad};
     events.push_back({Fill{r,radii,{.5f,.5f,.5f,alpha}},current,transforms.size(),scroll_x,scroll_y,scrolls.size()});
    };
    if(chrome->geometry.horizontal_visible)emit(chrome->geometry.horizontal_thumb,true,.55f);
    if(chrome->geometry.vertical_visible)emit(chrome->geometry.vertical_thumb,true,.55f);
    emit(chrome->geometry.corner,false,.28f);
   }else events.push_back(PreparedEvent{command,current,transforms.size(),scroll_x,scroll_y,scrolls.size()});
   if(events.size()>max_commands)return error(ErrorCode::COMMAND_COUNT);
  }
  index.reset(); tag.reset();
  if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
  if (!scrolls.empty() || !groups.empty() || !transforms.empty() || !clips.empty() || !chunks.empty()) return error(ErrorCode::UNCLOSED_SCOPE);
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
} // namespace aether::rendering::preparation
