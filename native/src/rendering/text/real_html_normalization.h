#pragma once
#include "simple_ltr_run.h"
#include "../native_paint_list.h"
namespace aether::rendering::text::truetype {
enum class HtmlTextError;struct ViewportOffset;struct HtmlViewportFrame;using HtmlViewportResult=std::variant<std::shared_ptr<const HtmlViewportFrame>,HtmlTextError>;
HtmlViewportResult render_real_html_viewport(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,const NativeImageCatalog&,ViewportOffset,Cancellation*) noexcept;

enum class HtmlWhiteSpaceProfile { pre, nowrap };
struct HtmlSourcePiece { std::size_t ordinal{}, begin{}, end{}; };
struct HtmlNormalizationEntry {
 std::size_t piece_ordinal{}, local_input_begin{}, input_length{}, global_decoded_begin{}, output_begin{}, output_length{};
};
enum class HtmlTextError; struct HtmlTextFrame;
using HtmlTextResult=std::variant<std::shared_ptr<const HtmlTextFrame>,HtmlTextError>;
HtmlTextResult render_real_html_text(std::string_view,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,Cancellation*) noexcept;
struct HtmlBlocksFrame;
using HtmlBlocksResult=std::variant<std::shared_ptr<const HtmlBlocksFrame>,HtmlTextError>;
HtmlBlocksResult render_real_html_blocks(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,Cancellation*) noexcept;
struct HtmlBlocksImagesFrame;
using HtmlBlocksImagesResult=std::variant<std::shared_ptr<const HtmlBlocksImagesFrame>,HtmlTextError>;
HtmlBlocksImagesResult render_real_html_blocks_images(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,const NativeImageCatalog&,Cancellation*) noexcept;
// Issued only by the owning operation after exact raw/DOM admission. The
// immutable output is an authority for the same decoded pieces/font/profile,
// not an authority to render an arbitrary caller-supplied text/run pair.
class HtmlNormalizationProof final {
 friend HtmlTextResult render_real_html_text(std::string_view,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,Cancellation*) noexcept;
 friend HtmlBlocksResult render_real_html_blocks(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,Cancellation*) noexcept;
 friend HtmlBlocksImagesResult render_real_html_blocks_images(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,const NativeImageCatalog&,Cancellation*) noexcept;
 friend HtmlViewportResult render_real_html_viewport(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,const NativeImageCatalog&,ViewportOffset,Cancellation*) noexcept;
 struct Token {std::size_t piece,local,global,length;bool whitespace;};
 static std::shared_ptr<const HtmlNormalizationProof> create_budgeted(HtmlWhiteSpaceProfile,
  std::shared_ptr<const RealFontBinding>,const std::vector<std::string>&,std::size_t& steps,std::size_t& logical,Cancellation*);
 static std::shared_ptr<const HtmlNormalizationProof> create(HtmlWhiteSpaceProfile,
   std::shared_ptr<const RealFontBinding>,const std::vector<std::string>&,Cancellation*);
 HtmlNormalizationProof(HtmlWhiteSpaceProfile p,std::shared_ptr<const RealFontBinding> b,
   std::string input,std::vector<std::string> parts,std::vector<HtmlSourcePiece> sources,
   std::string output,std::vector<HtmlNormalizationEntry> mapping)
 :profile(p),binding(std::move(b)),decoded_input(std::move(input)),pieces(std::move(parts)),source_map(std::move(sources)),normalized_output(std::move(output)),map(std::move(mapping)){}
public:
 HtmlNormalizationProof(const HtmlNormalizationProof&)=delete;
 HtmlNormalizationProof(HtmlNormalizationProof&&)=delete;
 const HtmlWhiteSpaceProfile profile;
 const std::shared_ptr<const RealFontBinding> binding;
 const std::string decoded_input;
 const std::vector<std::string> pieces;
 const std::vector<HtmlSourcePiece> source_map;
 const std::string normalized_output;
 const std::vector<HtmlNormalizationEntry> map;
};
}
