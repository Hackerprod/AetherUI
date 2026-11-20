#pragma once
#include "../native_frame.h"
#include "../text/glyph_mask_a8.h"
#include <aether/rendering/preparation/paint_snapshot.hpp>
#include <memory>
#include <string>
#include <vector>
struct aether_layout_engine_s;
struct aether_layout_result_s;
namespace aether::rendering { class NativePaintList; }
namespace aether::rendering::html_ltr {
class HtmlLtrMeasureBridge;
class ImageRegistry;
class HtmlLtrSession final {
 struct State;
 std::unique_ptr<State> state_;
 static void install(aether_layout_engine_s*,std::shared_ptr<HtmlLtrMeasureBridge>);
public:
 HtmlLtrSession(std::string,std::vector<uint8_t>,std::vector<uint8_t>,Viewport,uint32_t profile=1);
 ~HtmlLtrSession();
 // Registro de imágenes (ciclo 02): llamable antes de prepare().
 // Sin registro, todo background-image rechaza como antes.
 void set_image_registry(std::shared_ptr<const ImageRegistry> registry);
 HtmlLtrSession(const HtmlLtrSession&)=delete;
 const preparation::PreparedFrame& prepare();
 const std::vector<std::shared_ptr<const text::GlyphMaskA8>>& masks() const noexcept;
};
}

#ifdef AETHER_HTML_LTR_TESTING
namespace aether::rendering::html_ltr::testing {
enum class Stage {html_copy,font_copy,inventory,runtime,layout,occurrence,masks,staging,snapshot,preparation,candidate,control_presentation,control_transport,control_clip,a8_pixels,font_product};
void stage(Stage);
void inspect(aether_layout_result_s*,aether::rendering::NativePaintList*);
}
#define HTML_LTR_STAGE(name) ::aether::rendering::html_ltr::testing::stage(::aether::rendering::html_ltr::testing::Stage::name)
#else
#define HTML_LTR_STAGE(name) ((void)0)
#endif
