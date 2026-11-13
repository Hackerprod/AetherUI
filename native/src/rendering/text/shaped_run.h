#pragma once
#include <aether/aether.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace aether::rendering::text {
// Transport only. No HB/FT objects or callbacks may be hidden in these values.
// Consequently use and last release are safe on any thread. A future provider
// must own/destroy its mutable handles on its owner thread, outside these objects.
inline constexpr std::size_t max_font_bytes = 16 * 1024 * 1024;
inline constexpr std::size_t max_text_bytes = 1024 * 1024;
inline constexpr std::size_t max_glyphs = 65536;
inline constexpr std::size_t max_frame_glyphs = 1024 * 1024;
inline constexpr std::size_t max_mask_bytes = 64 * 1024 * 1024;
inline constexpr std::size_t max_mask_operations = 16 * 1024 * 1024;
class FontInstance final {
public:
 const std::vector<std::uint8_t> bytes;
 const std::string source_hash; // identity metadata; not a verified digest
 const std::uint32_t face_index;
 const std::int32_t size_26_6;
 const std::uint32_t raster_flags;
 FontInstance(std::vector<std::uint8_t> b, std::string hash, std::uint32_t face,
              std::int32_t size, std::uint32_t flags)
 : bytes(std::move(b)), source_hash(std::move(hash)), face_index(face),
   size_26_6(size), raster_flags(flags) {
  if(bytes.empty() || bytes.size()>max_font_bytes || source_hash.empty() ||
     source_hash.size()>128 || size<=0 || size>65536*64)
   throw std::invalid_argument("font transport bounds");
 }
};
struct Glyph {
 std::uint32_t id{}, cluster{};
 std::int32_t advance_x{}, advance_y{}, offset_x{}, offset_y{};
};
class ShapedRun final {
public:
 const std::shared_ptr<const FontInstance> font;
 const std::string source;
 const std::vector<Glyph> glyphs;
 const aether_text_metrics_t metrics;
 ShapedRun(std::shared_ptr<const FontInstance> f, std::string s,
           std::vector<Glyph> g, aether_text_metrics_t m)
 : font(std::move(f)), source(std::move(s)), glyphs(std::move(g)), metrics(m) {
  if(!font || source.empty() || source.size()>max_text_bytes ||
     glyphs.empty() || glyphs.size()>max_glyphs ||
     !std::isfinite(m.width)||!std::isfinite(m.height)||!std::isfinite(m.baseline)||
     m.width<0||m.width>65536||m.height<0||m.height>65536||
     m.baseline<0||m.baseline>m.height)
   throw std::invalid_argument("run transport bounds");
  for(const auto& glyph:glyphs) if(glyph.cluster>=source.size() ||
      std::abs(static_cast<std::int64_t>(glyph.advance_x))>65536*64 ||
      std::abs(static_cast<std::int64_t>(glyph.advance_y))>65536*64 ||
      std::abs(static_cast<std::int64_t>(glyph.offset_x))>65536*64 ||
      std::abs(static_cast<std::int64_t>(glyph.offset_y))>65536*64)
   throw std::invalid_argument("run cluster bounds");
 }
};
struct Measurement {
 aether_text_metrics_t metrics{};
 std::shared_ptr<const ShapedRun> run;
};
struct Placement {
 std::shared_ptr<const ShapedRun> run;
 float baseline{}; // relative to content top, not bitmap height
};
} // namespace
