#pragma once
#include "truetype_cmap.h"
#include "truetype_raster.h"

namespace aether::rendering::text::truetype {
enum class BindingError {
 invalid_input, malformed, unsupported, limit_exceeded, cancelled, out_of_memory
};
// Diagnostic phase progress, including failures. Not a time/process budget.
// Face retains its structural bounds; cmap retains its own 1M work limit.
struct BindingWork {
 Work face{};
 CmapWork cmap{};
 std::size_t units{}, cancel_checks{}; // checked face+cmap sum; factory checks only
 bool face_started{}, cmap_started{}, instance_started{}, binding_created{};
};
class RealFontBinding;
using BindingResult=std::variant<std::shared_ptr<const RealFontBinding>,BindingError>;
// Explicit source identity only. This is not a provider, font selector, shaper,
// HTML authorization, or a promise that every glyph is supported. Loading and
// hashing the FontSource happen outside this synchronous factory.
class RealFontBinding final {
 const std::shared_ptr<const ValidatedFace> face_;
 const std::shared_ptr<const UnicodeMap> map_;
 const std::shared_ptr<const FontInstance> font_;
 RealFontBinding(std::shared_ptr<const ValidatedFace> face,
                 std::shared_ptr<const UnicodeMap> map,
                 std::shared_ptr<const FontInstance> font) noexcept
  :face_(std::move(face)),map_(std::move(map)),font_(std::move(font)){}
public:
 RealFontBinding(const RealFontBinding&)=delete;
 RealFontBinding(RealFontBinding&&)=delete;
 RealFontBinding& operator=(const RealFontBinding&)=delete;
 RealFontBinding& operator=(RealFontBinding&&)=delete;
 // Face index is fixed to zero. No arbitrary face/map/font can be injected.
 // Only immutable owned values survive return, never cancellation callbacks.
 static BindingResult create(std::shared_ptr<const FontSource>,std::int32_t size_26_6,
   std::uint32_t flags=q1_unhinted,Cancellation* =nullptr,BindingWork* =nullptr) noexcept;
 const std::shared_ptr<const ValidatedFace>& face()const noexcept{return face_;}
 const std::shared_ptr<const UnicodeMap>& map()const noexcept{return map_;}
 const std::shared_ptr<const FontInstance>& font()const noexcept{return font_;}
 const std::shared_ptr<const FontSource>& source()const noexcept{return face_->source();}
 const std::string& digest()const noexcept{return source()->digest;}
};
}
