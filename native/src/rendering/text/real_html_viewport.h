#pragma once
#include "real_html_blocks_images.h"
namespace aether::rendering::text::truetype {
struct ViewportOffset {std::uint32_t x{},y{};};
struct ViewportDiagnostics {
 ViewportOffset offset;
 std::uint32_t range_x{},range_y{},document_extent_w{},document_extent_h{};
 bool scrolling{};
 aether_rectf clip{};
 aether_scrollbar_geometry_t chrome{};
};
struct HtmlViewportFrame {
 std::string profile="real_html_viewport_latin1_v1";
 HtmlBlocksImagesFrame scene;
 ViewportDiagnostics viewport;
};
using HtmlViewportResult=std::variant<std::shared_ptr<const HtmlViewportFrame>,HtmlTextError>;
// Closed root-only viewport capture. No caller-selected scroll identity or clamping.
HtmlViewportResult render_real_html_viewport(std::string_view,const std::vector<std::string>&,
 std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,
 const NativeImageCatalog&,ViewportOffset,Cancellation* =nullptr) noexcept;
}
