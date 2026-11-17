#pragma once
#include "../native_paint_list.h"
#include "font_provider.h"
#include <map>
namespace aether::rendering::html_ltr {
struct FinalText {
 std::shared_ptr<const text::ShapedRun> original;
 std::shared_ptr<const FontProduct> product;
 aether_rectf rect;
};
using FinalTexts=std::map<const text::ShapedRun*,FinalText>;
std::vector<std::shared_ptr<const text::GlyphMaskA8>> finalize_paint(NativePaintList&,const FinalTexts&,FontProvider&);
}
