#pragma once
#include "shaped_run.h"
namespace aether::rendering::text {
// Explicit immutable bitmap transport, not a font provider or a shaping API.
class GlyphMaskA8 final {
public:
 const std::shared_ptr<const FontInstance> font;
 const std::uint32_t glyph_id, flags;
 const std::uint8_t phase_x, phase_y;
 const std::uint32_t width, height, stride;
 const std::int32_t bearing_left, bearing_top;
 const std::vector<std::uint8_t> bytes;
 GlyphMaskA8(std::shared_ptr<const FontInstance> f, std::uint32_t id,
   std::uint32_t fl, unsigned px, unsigned py, std::uint32_t w,
   std::uint32_t h, std::uint32_t s, std::int32_t left, std::int32_t top,
   std::vector<std::uint8_t> b)
 : font(std::move(f)), glyph_id(id), flags(fl), phase_x(px), phase_y(py),
   width(w),height(h),stride(s),bearing_left(left),bearing_top(top),bytes(std::move(b)) {
  if(!font || fl!=font->raster_flags || px>63 || py>63 || w>8192 || h>8192 ||
     std::abs(std::int64_t(left))>65536 || std::abs(std::int64_t(top))>65536 ||
     (w==0 || h==0 ? (w!=0 || h!=0 || s!=0 || !bytes.empty() || left!=0 || top!=0) :
       (s<w || std::uint64_t(s)*h>max_mask_bytes || bytes.size()!=std::uint64_t(s)*h)))
   throw std::invalid_argument("A8 mask bounds");
 }
};
}
