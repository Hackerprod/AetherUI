#include "detail/font_kernels.h"
namespace aether::rendering::text::truetype {
CmapResult UnicodeMap::create(std::shared_ptr<const ValidatedFace> face,Cancellation* cancel,CmapWork* work) noexcept {
 return detail::FontKernel::map<detail::LegacyFontStorage>(std::move(face),cancel,work);
}
LookupResult UnicodeMap::lookup(uint32_t scalar,CmapWork* work) const noexcept {
 return detail::FontKernel::lookup(*this,scalar,work);
}
}
