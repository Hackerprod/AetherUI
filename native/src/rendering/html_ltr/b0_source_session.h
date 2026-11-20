#pragma once
#include "session.h"
#include <aether/aether_b0_source.h>
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
struct B0SourceError { aether_status_t status; uint32_t stage,reason; uint64_t node_ordinal=AETHER_B0_NO_INDEX,command_index=AETHER_B0_NO_INDEX,source_byte_offset=AETHER_B0_NO_INDEX; };
class B0SourceSession final {
 struct State;
 std::unique_ptr<State> state_;
 static void install(aether_layout_engine_s*,std::shared_ptr<HtmlLtrMeasureBridge>);
public:
 B0SourceSession(std::string,std::vector<uint8_t>,std::vector<uint8_t>,Viewport);
 ~B0SourceSession();
 // Registro de imágenes (ciclo 01, propuesta A1): llamable antes de prepare().
 // Sin registro, todo background-image rechaza como antes.
 void set_image_registry(std::shared_ptr<const ImageRegistry> registry);
 uint32_t diagnostic_stage()const noexcept;
 uint64_t diagnostic_node()const noexcept;
 B0SourceSession(const B0SourceSession&)=delete;
 const preparation::PreparedFrame& prepare();
 const std::vector<std::shared_ptr<const text::GlyphMaskA8>>& masks() const noexcept;
};
}
