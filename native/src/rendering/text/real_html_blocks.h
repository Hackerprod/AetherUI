#pragma once
#include "real_html_text.h"
namespace aether::rendering::text::truetype {
struct HtmlBlockText {
 std::string id;
 std::shared_ptr<const PreparedSimpleRun> run;
 std::shared_ptr<const HtmlNormalizationProof> normalization;
 std::vector<HtmlSourcePiece> source_map;
 HtmlBox text_box;
 HtmlTextWork work;
 std::size_t command_index{};
 bool has_command{};
};
struct HtmlBlocksWork {
 HtmlTextWork totals;
 std::size_t owners{}, admission_normalization_steps{}, proof_logical_bytes{}, normalized_scalars{};
 std::uint64_t outline_reserved{}, cmap_reserved{}, raster_reserved{}, mask_reserved{}, canvas_pixels_reserved{}, composite_visits_reserved{};
};
struct HtmlBlocksFrame {
 std::string profile="real_html_blocks_latin1_v1";
 Frame frame;
 // DOM order. No DOM identity, policy or mutable engine pointer escapes.
 std::vector<HtmlBlockText> owners;
 std::vector<HtmlBox> boxes;
 HtmlBlocksWork work;
};
using HtmlBlocksResult=std::variant<std::shared_ptr<const HtmlBlocksFrame>,HtmlTextError>;
// Restricted multi-block profile; one explicit font and LineBox, no fallback.
// Input IDs select owners; document order governs output and painting.
HtmlBlocksResult render_real_html_blocks(std::string_view,const std::vector<std::string>&,
 std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,Cancellation* =nullptr) noexcept;
}
