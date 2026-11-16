#pragma once
#include "real_html_text.h"
namespace aether::rendering {
struct SelectedCaptureFailure {
 text::truetype::HtmlTextError code;
 std::string reason; // Fixed diagnostic enum spelling, never source/path text.
};
struct SelectedCaptureWork {
 std::uint64_t parse_ns{},styles_ns{},measure_ns{},layout_ns{},paint_ns{},raster_ns{};
 std::uint64_t nodes{},elements{},selected_nodes{},text_runs{},glyph_commands{},unknown_rules{},mask_bytes{},known_bytes{};
 std::uint64_t outline_reserved{},cmap_reserved{},raster_reserved{};
};
struct SelectedTextDiagnostic {
 std::size_t ordinal{};
 std::string source,computed_family,font_metric_policy;
 aether_color_rgba color{};
 aether_rectf logical_rect{};
 float logical_baseline{},raster_baseline{},advance{};
 std::int64_t raster_x26_6{},raster_y26_6{};
 unsigned phase_x{},phase_y{};
};
struct SelectedBoxDiagnostic {
 std::size_t ordinal{};
 std::string tag,id;
 aether_rectf border{},content{};
};
struct SelectedCaptureFrame {
 Frame frame;
 // Unknown coverage never becomes RENDERED_PROFILE or verified CSS parity.
 bool coverage_complete=false;
 std::string source_hash,font_hash;
 SelectedCaptureWork work;
 std::vector<SelectedTextDiagnostic> text;
 std::vector<SelectedBoxDiagnostic> boxes;
};
using SelectedCaptureResult=std::variant<std::shared_ptr<const SelectedCaptureFrame>,SelectedCaptureFailure>;
SelectedCaptureResult capture_selected_html(std::string_view raw,std::string_view target_id,
 std::string_view descendant_selector,std::shared_ptr<const text::FontSource> font,
 std::uint32_t style_viewport_width,std::uint32_t style_viewport_height,
 std::uint32_t surface_width,std::uint32_t surface_height,
 text::truetype::Cancellation* cancel=nullptr) noexcept;
}
