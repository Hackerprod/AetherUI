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
#include "prepare_kernel.h"
#include "text_fields_view.h"

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

}
int command_tag(const Command& command) noexcept {
 static_assert(std::variant_size_v<Command> == 18, "command_tag must cover every Command alternative");
 constexpr std::array<int,18> tags{AETHER_PAINT_FILL_ROUNDED_RECT,AETHER_PAINT_STROKE_ROUNDED_RECT,
 AETHER_PAINT_DRAW_TEXT,AETHER_PAINT_PUSH_CLIP,AETHER_PAINT_POP_CLIP,AETHER_PAINT_PUSH_TRANSFORM,
 AETHER_PAINT_POP_TRANSFORM,AETHER_PAINT_BEGIN_PAINT_CHUNK,AETHER_PAINT_END_PAINT_CHUNK,AETHER_PAINT_DRAW_BOX_SHADOW,AETHER_PAINT_FILL_BORDER_SIDE,-100,-101,AETHER_PAINT_PUSH_SCROLL_OFFSET,AETHER_PAINT_POP_SCROLL_OFFSET,AETHER_PAINT_DRAW_SCROLLBAR_CHROME,AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT,AETHER_PAINT_DRAW_BACKGROUND_IMAGE};
 return command.valueless_by_exception() ? -1 : tags[command.index()];
}
std::optional<ErrorCode> validate_text_fields(TextFieldsView value, std::size_t& bytes,NativeTextCheck check,const void* native) noexcept {
   if (value.text.size() > max_text_bytes || value.font_family.size() >= 64) return ErrorCode::LIMIT_EXCEEDED;
   if (value.text.find('\0') != std::string_view::npos || value.font_family.find('\0') != std::string_view::npos) return ErrorCode::INVALID_PAYLOAD;
   if (!detail::valid_utf8(value.text) || !detail::valid_utf8(value.font_family)) return ErrorCode::INVALID_UTF8;
   // These are adapter restrictions, not guarantees made by the core ABI.
   if (!valid(value.rect) || value.font_family.empty() || !std::isfinite(value.font_size) ||
       value.font_size <= 0 || value.font_weight < 1 || value.font_weight > 1000 ||
       !align_valid(value.text_align) || !valid(value.color) ||
       (value.wrap != AETHER_TRUE && value.wrap != AETHER_FALSE)) return ErrorCode::INVALID_PAYLOAD;
   if(check)if(auto e=check(native))return e;
   const auto size = value.text.size() + value.font_family.size();
   if (bytes > max_string_bytes || size > max_string_bytes - bytes) return ErrorCode::LIMIT_EXCEEDED;
   bytes += size;
   return {};
}
std::optional<ErrorCode> validate_text(const Text& value,std::size_t& bytes) noexcept {
 return validate_text_fields(text_fields(value),bytes,[](const void* p)noexcept{return validate_native_text(*static_cast<const Text*>(p));},&value);
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
  } else if constexpr (std::is_same_v<T,LinearGradientFill>) {
   if(value.stops.size()<2 || value.stops.size()>max_gradient_stops)return ErrorCode::LIMIT_EXCEEDED;
   const auto bounded=[](Rect r){return valid(r)&&std::abs(double(r.x))<=65536&&std::abs(double(r.y))<=65536&&std::abs(double(r.x)+r.width)<=65536&&std::abs(double(r.y)+r.height)<=65536;};
   const double norm=std::hypot(double(value.direction_x),double(value.direction_y));
   if(!bounded(value.rect)||!bounded(value.gradient_rect)||!valid(value.radii)||
      value.gradient_rect.width<=0||value.gradient_rect.height<=0||
      !std::isfinite(norm)||norm<.9999||norm>1.0001||
      !nonnegative(value.opacity)||value.opacity>1)return ErrorCode::INVALID_PAYLOAD;
   float previous=0;
   for(const auto& stop:value.stops) {
    if(!nonnegative(stop.offset)||stop.offset>1||stop.offset<previous||!valid(stop.color))return ErrorCode::INVALID_PAYLOAD;
    previous=stop.offset;
   }
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
  } else if constexpr (std::is_same_v<T,Image>) {
   if (!value.image) return ErrorCode::INVALID_PAYLOAD;
   if (value.image->width < 1 || value.image->height < 1) return ErrorCode::INVALID_PAYLOAD;
   if (value.image->width > 8192 || value.image->height > 8192 ||
       value.image->pixels.size() > 16u * 1024u * 1024u) return ErrorCode::LIMIT_EXCEEDED;
   if (value.image->pixels.size() !=
       static_cast<std::uint64_t>(value.image->width) * static_cast<std::uint64_t>(value.image->height) * 4u)
    return ErrorCode::INVALID_PAYLOAD;
   const auto finite_rect = [](Rect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
           std::isfinite(r.height) && r.width > 0 && r.height > 0;
   };
   if (!finite_rect(value.destination) || !finite_rect(value.source) ||
       !std::isfinite(value.opacity) || value.opacity < 0 || value.opacity > 1)
    return ErrorCode::INVALID_PAYLOAD;
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
 kernel::Cursor cursor;
 try {
  std::vector<PreparedEvent> events;
  struct Scratch {
   std::vector<Matrix> transforms;std::vector<std::size_t> clips;std::vector<std::int64_t> chunks;
   std::vector<kernel::ScrollScope> scrolls;std::vector<kernel::Group> groups;
  };
  struct Validation {
   using Text=preparation::Text;
   NativeTextBudget native;
   int tag(const Command& c)const noexcept{return command_tag(c);}
   std::optional<ErrorCode> validate(const Command& c,std::size_t& b)const noexcept{return validate_command(c,b);}
   std::optional<ErrorCode> native_add(const Text& t){return native.add(t);}
  };
  struct State {Scratch scratch;Validation validation;};
  if(auto e=kernel::prepare_commands<Command,PreparedEvent,LinearGradientFill>(snapshot.commands,snapshot.native_text_required,events,[]{return State{};},cursor,cancellation))return *e;
  return PreparedFrame(std::move(events));
 }catch(const std::bad_alloc&){return Error{ErrorCode::OUT_OF_MEMORY,cursor.index,cursor.tag};}
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
