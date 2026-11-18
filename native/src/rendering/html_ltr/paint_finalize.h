#pragma once
#include "../native_paint_list.h"
#include "font_provider.h"
#include "control_presentation.h"
#include <map>
namespace aether::rendering::html_ltr {
struct FinalText {
 std::shared_ptr<const text::ShapedRun> original;
 std::shared_ptr<const FontProduct> product;
 aether_rectf rect;
 aether_color_rgba color{};bool verify_color=false;
};
struct FinalControl {AtomicControlPlacement placement;aether_rectf clip,rect;};
using FinalControls=std::map<aether_node_s*,FinalControl>;
using FinalTexts=std::map<const text::ShapedRun*,FinalText>;
std::vector<std::shared_ptr<const text::GlyphMaskA8>> finalize_paint(NativePaintList&,const FinalTexts&,FontProvider&,const FinalControls& = {},uint32_t profile=1,const char* expected_family="AetherLTR");
}
