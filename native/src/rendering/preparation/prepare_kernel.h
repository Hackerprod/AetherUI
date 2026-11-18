#pragma once
#include <aether/rendering/preparation/paint_snapshot.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <type_traits>
namespace aether::rendering::preparation::kernel {
struct Cursor {std::optional<std::size_t> index;std::optional<int> tag;};
struct ScrollScope {double x,y;std::size_t clips,transforms,chunks,groups;};
struct Group {std::uint64_t id;std::size_t transforms,clips,chunks,scrolls;};
inline bool cancelled(Cancellation* c)noexcept{return c&&c->cancelled();}
inline std::optional<Matrix> compose(Matrix m, Matrix c) noexcept {
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
// One algorithm, instantiated with legacy or interaction-owned storage.
// Allocation exceptions are mapped by the entry point using the same cursor.
template<class CommandType,class Event,class Gradient,class Events,class StateFactory>
std::optional<Error> prepare_commands(std::span<const CommandType> commands,bool native_text_required,
 Events& events,StateFactory make_state,Cursor& cursor,Cancellation* cancellation){
 auto& index=cursor.index;auto& tag=cursor.tag;
 const auto error=[&](ErrorCode code){return Error{code,index,tag};};
  if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
  if (commands.size() > max_commands) return error(ErrorCode::COMMAND_COUNT);
  // Bound the total deep-copy work before allocating/copying any event payload.
  std::size_t stop_count=0;
  for(const auto& command:commands) {
   if(cancelled(cancellation))return error(ErrorCode::CANCELLED);
   if(const auto* g=std::get_if<Gradient>(&command)) {
    if(g->stops.size()>max_gradient_stops || g->stops.size()>max_frame_gradient_stops-stop_count)return error(ErrorCode::LIMIT_EXCEEDED);
    stop_count+=g->stops.size();
   }
  }
  events.reserve(commands.size());
  // Construct scope storage and native-budget state only after the legacy
  // preflight and events reserve, preserving allocation/validation ordering.
  auto state=make_state();auto& scratch=state.scratch;auto& validation=state.validation;
  using Validation=std::decay_t<decltype(validation)>;
  auto& transforms=scratch.transforms;auto& clips=scratch.clips;auto& chunks=scratch.chunks;
  auto& scrolls=scratch.scrolls;auto& groups=scratch.groups;
  double scroll_x=0,scroll_y=0;
  std::uint64_t last_id=0;
  Matrix current = identity;
  std::size_t bytes = 0;
  for (std::size_t i=0; i<commands.size(); ++i) {
   index = i;
   const auto& command = commands[i];
   tag = validation.tag(command);
   if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
   if (auto e = validation.validate(command,bytes)) return error(*e);
   if(const auto* t=std::get_if<typename Validation::Text>(&command)) {
    if(native_text_required && !t->native)return error(ErrorCode::INVALID_PAYLOAD);
    if(auto e=validation.native_add(*t))return error(*e);
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
   }else events.push_back(Event{command,current,transforms.size(),scroll_x,scroll_y,scrolls.size()});
   if(events.size()>max_commands)return error(ErrorCode::COMMAND_COUNT);
  }
  index.reset(); tag.reset();
  if (cancelled(cancellation)) return error(ErrorCode::CANCELLED);
  if (!scrolls.empty() || !groups.empty() || !transforms.empty() || !clips.empty() || !chunks.empty()) return error(ErrorCode::UNCLOSED_SCOPE);
  return {};
}
}
