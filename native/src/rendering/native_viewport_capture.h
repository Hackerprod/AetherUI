#pragma once
#include "text/real_html_viewport.h"
#include "../layout/operation_pin.h"
#include "native_paint_list.h"
#include <thread>
namespace aether::layout { class RealHtmlTextTestAccess; }
namespace aether::rendering {
using ViewportChromeResult=std::variant<aether_scrollbar_geometry_t,text::truetype::HtmlTextError>;
// Pure opt-in overlay geometry. Does not change layout or the legacy resolver.
ViewportChromeResult resolve_native_viewport_chrome(std::uint32_t width,std::uint32_t height,
 double range_x,double range_y,text::truetype::ViewportOffset) noexcept;
// Internal operation capability, never part of a user result or public C ABI.
// Creation requires validated real-HTML wrapper; a layout pin prevents dangling
// layout storage. Supplied map handles must be live for accepts(), as elsewhere.
class NativeViewportAuthority final {
 friend class aether::layout::RealHtmlTextTestAccess;
 friend text::truetype::HtmlViewportResult text::truetype::render_real_html_viewport(std::string_view,const std::vector<std::string>&,std::shared_ptr<const text::truetype::RealFontBinding>,text::truetype::LineBox,std::uint32_t,std::uint32_t,const NativeImageCatalog&,text::truetype::ViewportOffset,text::truetype::Cancellation*) noexcept;
 std::thread::id thread_=std::this_thread::get_id();
 std::shared_ptr<node_identity_s> document_,root_identity_;
 std::uint64_t structure_{},mutation_{},style_{},nonstate_{};
 aether_doc_s* doc_{};aether_style_map_s* map_{};aether_layout_result_s* layout_{};
 aether_layout_box_s* root_{};
 aether::style::MapVersionRef version_;
 aether::style::FrozenStyleRef root_style_;
 std::unique_ptr<aether::layout::LayoutOperationPin> pin_;
 NativeScrollState state_{};aether_rectf viewport_{};
 text::truetype::ViewportDiagnostics diagnostics_{};
 NativeViewportAuthority()=default;
 static std::variant<NativeViewportAuthority,text::truetype::HtmlTextError> create(aether_doc_s*,aether_layout_result_s*,aether_style_map_s*,std::uint32_t,std::uint32_t,text::truetype::ViewportOffset) noexcept;
public:
 NativeViewportAuthority(const NativeViewportAuthority&)=delete;
 NativeViewportAuthority& operator=(const NativeViewportAuthority&)=delete;
 NativeViewportAuthority(NativeViewportAuthority&&) noexcept=default;
 NativeViewportAuthority& operator=(NativeViewportAuthority&&)=delete;
 bool accepts(aether_layout_result_s*,aether_style_map_s*) const noexcept;
 const NativeScrollState& state() const noexcept{return state_;}
 aether_rectf viewport() const noexcept{return viewport_;}
 const text::truetype::ViewportDiagnostics& diagnostics() const noexcept{return diagnostics_;}
 std::int64_t root_id() const noexcept{return state_.id;}
 bool scrolling() const noexcept{return diagnostics_.scrolling;}
};
aether_status_t build_native_viewport_capture(aether_display_list_builder_t,aether_layout_result_t,aether_style_map_t,const NativeViewportAuthority&,const NativeImageCatalog&,std::unique_ptr<NativePaintList>&) noexcept;
}
