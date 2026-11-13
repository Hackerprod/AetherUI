#pragma once
#include "aether/rendering/preparation/paint_snapshot.hpp"
#include "../text/shaped_run.h"
#include "../text/native_inline_paragraph.h"
#include <unordered_set>
#include <cmath>
namespace aether::rendering::preparation {
// Bound retained resources independently of command/string limits. Font identity
// is the immutable instance pointer, not source_hash (which is not verified).
inline constexpr std::size_t max_frame_font_bytes = 64 * 1024 * 1024;
inline std::optional<ErrorCode> validate_native_text(const Text& t) noexcept {
 if(!t.native)return {};
 const auto& n=*t.native;
 if(!n.run || !n.run->font || n.run->source!=t.text ||
    !std::isfinite(n.baseline) ||
    n.baseline!=t.rect.y+n.run->metrics.baseline ||
    float(n.run->font->size_26_6)/64.f!=t.font_size)
  return ErrorCode::INVALID_PAYLOAD;
 if(n.run->paragraph) {
  try {
   text::validate_contextual_measurement(n.run->paragraph,
     {n.run->paragraph,0,n.run->source.size(),{n.run->metrics,n.run}});
  } catch(...) {return ErrorCode::INVALID_PAYLOAD;}
 }
 return {};
}
struct NativeTextBudget {
 std::size_t glyphs{},font_bytes{};
 std::unordered_set<const text::FontInstance*> fonts;
 std::optional<ErrorCode> add(const Text& t) {
  if(auto e=validate_native_text(t))return e;
  if(!t.native)return {};
  const auto& run=*t.native->run;
  if(run.glyphs.size()>text::max_frame_glyphs-glyphs)return ErrorCode::LIMIT_EXCEEDED;
  glyphs+=run.glyphs.size();
  if(fonts.insert(run.font.get()).second){
   if(run.font->bytes.size()>max_frame_font_bytes-font_bytes)return ErrorCode::LIMIT_EXCEEDED;
   font_bytes+=run.font->bytes.size();
  }
  return {};
 }
};
}
