#include "text_request.h"
#include <aether/rendering/preparation/detail/utf8.hpp>
#include <cmath>
#include <new>
namespace aether::rendering::text {
Request::Request(std::string_view text,std::string_view family,Purpose purpose,float size,
 std::int32_t weight,bool wrap,std::optional<float> width,std::optional<float> height,
 std::optional<float> line,std::optional<std::int32_t> alignment)
 :text_(text),family_(family),purpose_(purpose),size_(size),weight_(weight),wrap_(wrap),
 width_(width),height_(height),used_line_height_(line),alignment_(alignment) {}
Result paint(const prep::Text& text) noexcept {
 std::size_t budget=0;
 if(auto error=prep::validate_text(text,budget)) return *error;
 try {
  return Request{text.text,text.font_family,Purpose::paint,text.font_size,text.font_weight,
   text.wrap==AETHER_TRUE,text.rect.width,text.rect.height,std::nullopt,text.text_align};
 } catch(const std::bad_alloc&) {return prep::ErrorCode::OUT_OF_MEMORY;}
}
Result intrinsic(const IntrinsicInput& in) noexcept {
 using E=prep::ErrorCode;
 if(in.text.size()>prep::max_text_bytes || in.family.size()>=64) return E::LIMIT_EXCEEDED;
 if(in.text.find('\0')!=std::string_view::npos || in.family.find('\0')!=std::string_view::npos)
  return E::INVALID_PAYLOAD;
 if(!prep::detail::valid_utf8(in.text)||!prep::detail::valid_utf8(in.family)) return E::INVALID_UTF8;
 if(in.family.empty()||!std::isfinite(in.font_size)||in.font_size<=0||in.weight<1||in.weight>1000||
    !std::isfinite(in.used_line_height)||in.used_line_height<0||
    (in.max_width && (!std::isfinite(*in.max_width)||*in.max_width<0))) return E::INVALID_PAYLOAD;
 try {
  return Request{in.text,in.family,Purpose::intrinsic,in.font_size,in.weight,false,
   in.max_width,std::nullopt,in.used_line_height,std::nullopt};
 } catch(const std::bad_alloc&) {return E::OUT_OF_MEMORY;}
}
}
