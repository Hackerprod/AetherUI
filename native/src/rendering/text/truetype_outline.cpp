#include "detail/font_kernels.h"
namespace aether::rendering::text::truetype {
FaceResult ValidatedFace::create(std::shared_ptr<const FontSource> source,uint32_t index,Cancellation* cancel,Work* work) noexcept {
 return detail::FontKernel::face<detail::LegacyFontStorage>(std::move(source),index,cancel,work);
}
OutlineResult ValidatedFace::outline(uint32_t glyph,Cancellation* cancel,Work* work) const noexcept {
 return detail::FontKernel::outline<detail::LegacyFontStorage>(*this,glyph,cancel,work);
}
}
