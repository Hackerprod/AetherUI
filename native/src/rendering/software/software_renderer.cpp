#include "software_renderer.h"
#include "render_kernel.h"
namespace aether::rendering::software {
namespace {
struct LegacyRenderStorage {
 using Store=FrameStore;using PreparedFrame=prep::PreparedFrame;using Mask=text::GlyphMaskA8;using Font=text::FontInstance;using Text=prep::Text;using Gradient=prep::LinearGradientFill;
 template<class T> auto vector()const{return std::vector<T>{};}
 template<class K,class V> auto map()const{return std::map<K,V>{};}
 template<class K> auto set()const{return std::set<K>{};}
 auto layers(std::size_t n)const{return std::vector<std::vector<std::uint8_t>>(n);}
 bool valid_mask(const Mask&)const noexcept{return true;}
 std::size_t mask_metadata()const noexcept{return sizeof(text::GlyphMaskA8)+256;}
 std::size_t font_bytes(const Font& f)const noexcept{return f.byte_view().size();}
 [[noreturn]] Result unexpected(std::size_t,std::uint64_t)const noexcept{std::terminate();}
};
}
Result render(FrameStore& store,const prep::PreparedFrame& frame,prep::Cancellation* cancel) noexcept {
 return render_kernel::render_impl(LegacyRenderStorage{},store,frame,nullptr,cancel);
}
Result render_a8(FrameStore& store,const prep::PreparedFrame& frame,
 std::span<const std::shared_ptr<const text::GlyphMaskA8>> masks,prep::Cancellation* cancel) noexcept {
 return render_kernel::render_impl(LegacyRenderStorage{},store,frame,&masks,cancel);
}
}
