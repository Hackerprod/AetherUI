#include "aether/rendering/preparation/aether_command_source.hpp"
#include <new>
#include <utility>
#include <span>
#include "../border_groups.h"

namespace aether::rendering::preparation {
namespace {
bool cancelled(Cancellation* cancellation) noexcept {
 return cancellation && cancellation->cancelled();
}
struct CommandGuard {
 const AetherApi& api;
 aether_paint_command_t& command;
 ~CommandGuard() noexcept { api.free_command(&command); }
};
std::optional<std::size_t> bounded_length(const char* text, std::size_t capacity) noexcept {
 for (std::size_t i = 0; i < capacity; ++i) {
  if (text[i] == '\0') return i;
 }
 return std::nullopt;
}
using CopyResult = std::variant<Command, ErrorCode>;
CopyResult copy_command(const aether_paint_command_t& source, std::size_t string_bytes) {
 switch (source.tag) {
 case AETHER_PAINT_FILL_ROUNDED_RECT: {
  const auto& p = source.data.fill_rounded_rect;
  return Command{Fill{p.rect, p.radii, p.color}};
 }
 case AETHER_PAINT_STROKE_ROUNDED_RECT: {
  const auto& p = source.data.stroke_rounded_rect;
  return Command{Stroke{p.rect, p.radii, p.stroke_width, p.color}};
 }
 case AETHER_PAINT_DRAW_TEXT: {
  const auto& p = source.data.draw_text;
  if (!p.text) return ErrorCode::INVALID_PAYLOAD;
  const auto text_length = bounded_length(p.text, max_text_bytes + 1);
  if (!text_length) return ErrorCode::LIMIT_EXCEEDED;
  const auto family_length = bounded_length(p.font_family, sizeof(p.font_family));
  if (!family_length) return ErrorCode::INVALID_PAYLOAD;
  // Check the budget before allocating either string. Subtractions cannot wrap.
  if (string_bytes > max_string_bytes || *text_length > max_string_bytes - string_bytes ||
      *family_length > max_string_bytes - string_bytes - *text_length)
   return ErrorCode::LIMIT_EXCEEDED;
  return Command{Text{std::string(p.text, *text_length), p.rect,
   std::string(p.font_family, *family_length), p.font_size, p.font_weight,
   p.text_align, p.color, p.wrap}};
 }
 case AETHER_PAINT_PUSH_CLIP: {
  const auto& p = source.data.push_clip;
  return Command{PushClip{p.rect, p.radii}};
 }
 case AETHER_PAINT_POP_CLIP: return Command{PopClip{}};
 case AETHER_PAINT_PUSH_TRANSFORM:
  return Command{PushTransform{source.data.push_transform.matrix}};
 case AETHER_PAINT_POP_TRANSFORM: return Command{PopTransform{}};
 case AETHER_PAINT_BEGIN_PAINT_CHUNK:
  return Command{BeginChunk{source.data.paint_chunk.style_node_id}};
 case AETHER_PAINT_END_PAINT_CHUNK:
  return Command{EndChunk{source.data.paint_chunk.style_node_id}};
 case AETHER_PAINT_DRAW_BOX_SHADOW: {
  const auto& p = source.data.draw_box_shadow;
  return Command{BoxShadow{p.rect,p.radii,p.shadow}};
 }
 case AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT:
 case AETHER_PAINT_DRAW_TEXT_GRADIENT:
 case AETHER_PAINT_FILL_BORDER_SIDE:
 case AETHER_PAINT_DRAW_TEXT_SHADOW:
 case AETHER_PAINT_DRAW_BACKGROUND_IMAGE:
 case AETHER_PAINT_APPLY_BACKDROP_FILTER:
 case AETHER_PAINT_PUSH_SCROLL_OFFSET:
 case AETHER_PAINT_POP_SCROLL_OFFSET:
 case AETHER_PAINT_DRAW_SCROLLBAR_CHROME:
  return ErrorCode::UNSUPPORTED_TAG;
 default: return ErrorCode::UNKNOWN_TAG;
 }
}
} // namespace

SnapshotResult snapshot_with_border_groups(const AetherApi& api, aether_display_list_t list,
                                 std::span<const aether_border_group_range> groups, Cancellation* cancellation) noexcept {
 std::optional<std::size_t> index;
 std::optional<int> tag;
 try {
  if (cancelled(cancellation)) return Error{ErrorCode::CANCELLED};
  if (!api.abi_version || !api.abi_capabilities || !api.paint_command_size ||
      !api.get_count || !api.get_command || !api.free_command)
   return Error{ErrorCode::INVALID_API};
  if (!list) return Error{ErrorCode::INVALID_LIST};
  if (api.abi_version() != AETHER_ABI_VERSION_CURRENT) return Error{ErrorCode::ABI_VERSION};
  if ((api.abi_capabilities() & AETHER_CAPABILITY_DISPLAY_LIST) == 0)
   return Error{ErrorCode::ABI_CAPABILITY};
  if (api.paint_command_size() != sizeof(aether_paint_command_t)) return Error{ErrorCode::ABI_SIZE};
  const auto count = api.get_count(list);
  if (count < 0 || static_cast<std::size_t>(count) > max_commands)
   return Error{ErrorCode::COMMAND_COUNT};
  std::size_t previous_end=0;
  for(const auto& g:groups) {
   if(!g.count || g.count>4 || g.first<previous_end || g.first>std::size_t(count) || g.count>std::size_t(count)-g.first)
    return Error{ErrorCode::INVALID_PAYLOAD};
   previous_end=g.first+g.count;
  }
  std::size_t group_index=0;
  Snapshot snapshot;
  snapshot.commands.reserve(static_cast<std::size_t>(count));
  std::size_t string_bytes = 0;
  for (std::int32_t i = 0; i < count; ++i) {
   index = static_cast<std::size_t>(i);
   tag.reset();
   if (cancelled(cancellation)) return Error{ErrorCode::CANCELLED, index};
   if(group_index<groups.size() && groups[group_index].first==std::size_t(i)) {
    const auto g=groups[group_index++]; BorderGroup border;
    const auto same_rect=[](Rect a,Rect b){return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;};
    const auto same_radii=[](Radii a,Radii b){return a.top_left_x==b.top_left_x && a.top_left_y==b.top_left_y && a.top_right_x==b.top_right_x && a.top_right_y==b.top_right_y && a.bottom_left_x==b.bottom_left_x && a.bottom_left_y==b.bottom_left_y && a.bottom_right_x==b.bottom_right_x && a.bottom_right_y==b.bottom_right_y;};
    for(std::size_t j=0;j<g.count;++j) {
     if(cancelled(cancellation))return Error{ErrorCode::CANCELLED,index};
     aether_paint_command_t item{};
     if(api.get_command(list,i+std::int32_t(j),&item)!=AETHER_OK)return Error{ErrorCode::GET_COMMAND_FAILED,index};
     const CommandGuard item_guard{api,item};
     if(item.tag!=AETHER_PAINT_FILL_BORDER_SIDE)return Error{ErrorCode::INVALID_PAYLOAD,index};
     const auto& s=item.data.fill_border_side;
     if(s.side<0 || s.side>3 || border.sides[s.side].present)return Error{ErrorCode::INVALID_PAYLOAD,index};
     if(j==0){border.outer=s.outer_rect;border.inner=s.inner_rect;border.outer_radii=s.outer_radii;border.inner_radii=s.inner_radii;}
     else if(!same_rect(border.outer,s.outer_rect) || !same_rect(border.inner,s.inner_rect) || !same_radii(border.outer_radii,s.outer_radii) || !same_radii(border.inner_radii,s.inner_radii))return Error{ErrorCode::INVALID_PAYLOAD,index};
     border.sides[s.side]={true,s.style,s.color};
    }
    Command cmd{border}; if(auto e=validate_command(cmd,string_bytes))return Error{*e,index};
    snapshot.commands.push_back(std::move(cmd));i+=std::int32_t(g.count)-1;continue;
   }
   aether_paint_command_t source{};
   if (api.get_command(list, i, &source) != AETHER_OK)
    return Error{ErrorCode::GET_COMMAND_FAILED, index};
   const CommandGuard guard{api, source};
   tag = static_cast<int>(source.tag);
   if (cancelled(cancellation)) return Error{ErrorCode::CANCELLED, index, tag};
   auto copied = copy_command(source, string_bytes);
   if (const auto* error = std::get_if<ErrorCode>(&copied)) return Error{*error, index, tag};
   auto& command = std::get<Command>(copied);
   if (const auto error = validate_command(command, string_bytes)) return Error{*error, index, tag};
   snapshot.commands.push_back(std::move(command));
  }
  if (cancelled(cancellation)) return Error{ErrorCode::CANCELLED, index, tag};
  return snapshot;
 } catch (const std::bad_alloc&) {
  return Error{ErrorCode::OUT_OF_MEMORY, index, tag};
 }
}
SnapshotResult snapshot_commands(const AetherApi& api,aether_display_list_t list,Cancellation* c) noexcept {
 return snapshot_with_border_groups(api,list,{},c);
}
} // namespace aether::rendering::preparation
