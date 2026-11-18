#include "interaction_preparation.h"
#include "../preparation/prepare_kernel.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace aether::rendering::html_ltr {
namespace {
using Font=owned_text::Font;
std::optional<prep::ErrorCode> native_check(const void* raw)noexcept {
 const auto& t=*static_cast<const InteractionText*>(raw);
 if(!t.native)return {};
 const auto& n=*t.native;
 if(!n.run||!n.run->font||!n.run->font->source)return prep::ErrorCode::INVALID_PAYLOAD;
 const auto& r=*n.run;
 if(r.source!=t.text||!std::isfinite(n.baseline)||n.baseline!=t.rect.y+r.metrics.baseline||float(r.font->size_26_6)/64.f!=t.font_size)return prep::ErrorCode::INVALID_PAYLOAD;
 // Owned transport has no paragraph alternative and cannot hide a callback.
 if(r.source.empty()||r.source.size()>text::max_text_bytes||r.glyphs.size()>text::max_glyphs||
    (r.glyphs.empty()&&r.source.find_first_not_of(' ')!=OwnedString::npos)||
    !std::isfinite(r.metrics.width)||!std::isfinite(r.metrics.height)||!std::isfinite(r.metrics.baseline)||
    r.metrics.width<0||r.metrics.width>65536||r.metrics.height<0||r.metrics.height>65536||r.metrics.baseline<0||r.metrics.baseline>r.metrics.height)return prep::ErrorCode::INVALID_PAYLOAD;
 for(const auto& g:r.glyphs)if(g.cluster>=r.source.size()||std::abs(std::int64_t(g.advance_x))>65536*64||std::abs(std::int64_t(g.advance_y))>65536*64||std::abs(std::int64_t(g.offset_x))>65536*64||std::abs(std::int64_t(g.offset_y))>65536*64)return prep::ErrorCode::INVALID_PAYLOAD;
 return {};
}
struct Validation {
 using Text=InteractionText;
 OwnedBudget::Handle domain;
 std::size_t glyphs{},font_bytes{};
 std::unordered_set<const Font*,std::hash<const Font*>,std::equal_to<const Font*>,OwnedAllocator<const Font*>> fonts;
 explicit Validation(OwnedBudget::Handle d):domain(d),fonts(0,std::hash<const Font*>{},std::equal_to<const Font*>{},OwnedAllocator<const Font*>(d,OwnedCategory::temporary)){}
 int tag(const InteractionCommand& c)const noexcept {
  constexpr int tags[]{AETHER_PAINT_FILL_ROUNDED_RECT,AETHER_PAINT_STROKE_ROUNDED_RECT,AETHER_PAINT_DRAW_TEXT,AETHER_PAINT_PUSH_CLIP,AETHER_PAINT_POP_CLIP,AETHER_PAINT_PUSH_TRANSFORM,AETHER_PAINT_POP_TRANSFORM,AETHER_PAINT_BEGIN_PAINT_CHUNK,AETHER_PAINT_END_PAINT_CHUNK,AETHER_PAINT_DRAW_BOX_SHADOW,AETHER_PAINT_FILL_BORDER_SIDE,-100,-101,AETHER_PAINT_PUSH_SCROLL_OFFSET,AETHER_PAINT_POP_SCROLL_OFFSET,AETHER_PAINT_DRAW_SCROLLBAR_CHROME,AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT};
  return c.valueless_by_exception()?-1:tags[c.index()];
 }
 std::optional<prep::ErrorCode> validate(const InteractionCommand& c,std::size_t& bytes)const noexcept {
  if(c.valueless_by_exception())return prep::ErrorCode::INVALID_PAYLOAD;
  return std::visit([&](const auto& v)->std::optional<prep::ErrorCode>{
   using T=std::decay_t<decltype(v)>;
   if constexpr(std::is_same_v<T,InteractionText>){
    if(!v.text.get_allocator().domain().same_domain(domain)||!v.font_family.get_allocator().domain().same_domain(domain))return prep::ErrorCode::INVALID_PAYLOAD;
    if(v.native&&v.native->run){
     const auto& run=*v.native->run;
     if(!run.source.get_allocator().domain().same_domain(domain)||!run.glyphs.get_allocator().domain().same_domain(domain))return prep::ErrorCode::INVALID_PAYLOAD;
     if(run.font&&run.font->source&&!run.font->source->domain().same_domain(domain))return prep::ErrorCode::INVALID_PAYLOAD;
    }
    return validate_interaction_text(v,bytes);
   }else if constexpr(std::is_same_v<T,InteractionGradient>||std::is_same_v<T,prep::BoxShadow>){return prep::ErrorCode::UNSUPPORTED_TAG;}
   else return prep::validate_command(prep::Command{v},bytes); // POD only, no allocation
  },c);
 }
 std::optional<prep::ErrorCode> native_add(const InteractionText& t){
  if(auto e=native_check(&t))return e;
  if(!t.native)return {};
  const auto& run=*t.native->run;
  if(run.glyphs.size()>text::max_frame_glyphs-glyphs)return prep::ErrorCode::LIMIT_EXCEEDED;
  glyphs+=run.glyphs.size();
  if(fonts.insert(run.font.get()).second){const auto n=run.font->source->bytes.size();if(n>64u*1024u*1024u-font_bytes)return prep::ErrorCode::LIMIT_EXCEEDED;font_bytes+=n;}
  return {};
 }
};
struct Scratch {
 OwnedVector<prep::Matrix> transforms;OwnedVector<std::size_t> clips;OwnedVector<std::int64_t> chunks;
 OwnedVector<prep::kernel::ScrollScope> scrolls;OwnedVector<prep::kernel::Group> groups;
 explicit Scratch(OwnedBudget::Handle d):transforms(OwnedAllocator<prep::Matrix>(d,OwnedCategory::temporary)),clips(OwnedAllocator<std::size_t>(d,OwnedCategory::temporary)),chunks(OwnedAllocator<std::int64_t>(d,OwnedCategory::temporary)),scrolls(OwnedAllocator<prep::kernel::ScrollScope>(d,OwnedCategory::temporary)),groups(OwnedAllocator<prep::kernel::Group>(d,OwnedCategory::temporary)){}
};
prep::Error owned_error(const Error& e,const prep::kernel::Cursor& c={})noexcept{return {e.status==AETHER_ERROR_LIMIT_EXCEEDED?prep::ErrorCode::LIMIT_EXCEEDED:e.status==AETHER_ERROR_OUT_OF_MEMORY?prep::ErrorCode::OUT_OF_MEMORY:prep::ErrorCode::INVALID_PAYLOAD,c.index,c.tag};}
}
InteractionText::InteractionText(OwnedBudget::Handle d,const InteractionTextView& v)
 :text(v.text.begin(),v.text.end(),OwnedAllocator<char>(d,OwnedCategory::paint)),rect(v.rect),font_family(v.font_family.begin(),v.font_family.end(),OwnedAllocator<char>(d,OwnedCategory::paint)),font_size(v.font_size),font_weight(v.font_weight),text_align(v.text_align),color(v.color),wrap(v.wrap),native(v.native){}
std::optional<prep::ErrorCode> validate_interaction_text(const InteractionText& t,std::size_t& bytes)noexcept{return prep::validate_text_fields(prep::text_fields(t),bytes,native_check,&t);}
void InteractionSnapshot::reserve(std::size_t n){if(n>prep::max_commands)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};commands_.reserve(n);}
void InteractionSnapshot::append(const InteractionCommandView& c){
 if(commands_.size()==prep::max_commands)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
 std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,InteractionTextView>)commands_.emplace_back(InteractionText(domain(),v));else commands_.emplace_back(v);},c);
}
InteractionSnapshotResult interaction_snapshot(OwnedBudget& b,std::span<const InteractionCommandView> input,bool required)noexcept {
 prep::kernel::Cursor cursor;
 try {
  InteractionSnapshot out(b.handle(),required);out.reserve(input.size());Validation validation(b.handle());std::size_t bytes=0;
  for(std::size_t i=0;i<input.size();++i){cursor.index=i;out.append(input[i]);const auto& c=out.commands().back();cursor.tag=validation.tag(c);if(auto e=validation.validate(c,bytes))return prep::Error{*e,cursor.index,cursor.tag};if(const auto* t=std::get_if<InteractionText>(&c)){if(required&&!t->native)return prep::Error{prep::ErrorCode::INVALID_PAYLOAD,cursor.index,cursor.tag};if(auto e=validation.native_add(*t))return prep::Error{*e,cursor.index,cursor.tag};}}
  return out;
 }catch(const Error& e){return owned_error(e,cursor);}catch(const std::bad_alloc&){return prep::Error{prep::ErrorCode::OUT_OF_MEMORY,cursor.index,cursor.tag};}catch(...){return prep::Error{prep::ErrorCode::INVALID_PAYLOAD,cursor.index,cursor.tag};}
}
InteractionPrepareResult interaction_prepare(OwnedBudget& b,const InteractionSnapshot& snapshot,prep::Cancellation* cancel)noexcept {
 prep::kernel::Cursor cursor;
 try {
  if(!snapshot.domain().same_domain(b.handle()))return prep::Error{prep::ErrorCode::INVALID_PAYLOAD};
  OwnedVector<InteractionPreparedEvent> events(OwnedAllocator<InteractionPreparedEvent>(b,OwnedCategory::paint));
  struct State {Scratch scratch;Validation validation;explicit State(OwnedBudget::Handle d):scratch(d),validation(d){}};
  if(auto e=prep::kernel::prepare_commands<InteractionCommand,InteractionPreparedEvent,InteractionGradient>(snapshot.commands(),snapshot.native_text_required(),events,[&]{return State(b.handle());},cursor,cancel))return *e;
  return InteractionPreparedFrame(std::move(events));
 }catch(const Error& e){return owned_error(e,cursor);}catch(const std::bad_alloc&){return prep::Error{prep::ErrorCode::OUT_OF_MEMORY,cursor.index,cursor.tag};}catch(...){return prep::Error{prep::ErrorCode::INVALID_PAYLOAD,cursor.index,cursor.tag};}
}
}
