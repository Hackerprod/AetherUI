#pragma once
#include <aether/rendering/preparation/paint_snapshot.hpp>
#include <string_view>
namespace aether::rendering::preparation {
// Internal nonowning text fields; callers retain backing strings/resources.
struct TextFieldsView {
 std::string_view text;Rect rect;std::string_view font_family;float font_size;
 std::int32_t font_weight,text_align;Color color;aether_bool_t wrap;
};
using NativeTextCheck=std::optional<ErrorCode>(*)(const void*) noexcept;
std::optional<ErrorCode> validate_text_fields(TextFieldsView,std::size_t&,NativeTextCheck,const void*) noexcept;
template<class TextType> TextFieldsView text_fields(const TextType& t) noexcept {
 return {{t.text.data(),t.text.size()},t.rect,{t.font_family.data(),t.font_family.size()},t.font_size,t.font_weight,t.text_align,t.color,t.wrap};
}
}
