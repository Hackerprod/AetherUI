#pragma once
#include "real_html_normalization.h"
#include "../native_frame.h"
#include "../software/software_renderer.h"
namespace aether::rendering::text::truetype {
enum class HtmlTextError { invalid_input, unsupported, limit_exceeded, cancelled, out_of_memory, layout_failed, preparation_failed, render_failed };
struct HtmlBox { std::string id; bool text{}; aether_rectf content{},border{}; };
struct HtmlTextWork {
 RunWork run{};
 std::uint64_t admission_ns{}, styles_ns{}, normalization_ns{}, run_ns{}, layout_ns{}, preparation_ns{}, render_ns{};
 std::size_t run_canvas_bytes{}, retained_mask_bytes{}, registry_entries{}, font_bytes{}, native_glyph_commands{}, image_commands{};
 std::size_t raw_bytes{}, source_map_bytes{}, known_layout_box_bytes{}, registry_metadata_bytes{};
 // Normalization logical byte estimate: headers plus payload sizes, not
 // allocator capacity, actual RSS/peak, or an aggregate pipeline cap.
 std::size_t decoded_input_bytes{}, normalized_output_bytes{}, normalization_entries{}, normalization_map_bytes{}, normalization_logical_bytes{};
 // Known retained categories, NOT total or peak process memory.
 software::Result software{};
};
struct HtmlTextFrame {
 std::string profile="real_html_simple_latin1_v2";
 Frame frame;
 std::shared_ptr<const PreparedSimpleRun> run;
 std::shared_ptr<const HtmlNormalizationProof> normalization;
 std::vector<HtmlSourcePiece> source_map;
 std::vector<HtmlBox> boxes;
 HtmlTextWork work;
};
using HtmlTextResult=std::variant<std::shared_ptr<const HtmlTextFrame>,HtmlTextError>;
// Restricted single-owner profile only. No font fallback; owns all returned data.
// Failure never mutates a previously returned frame. Raw DOM/policy stay local.
HtmlTextResult render_real_html_text(std::string_view,std::shared_ptr<const RealFontBinding>,LineBox,
 std::uint32_t width,std::uint32_t height,Cancellation* =nullptr) noexcept;
}
