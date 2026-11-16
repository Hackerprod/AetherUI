#pragma once
#include "real_html_blocks_admission.h"
namespace aether::rendering::text::truetype {
struct HtmlBlockImageTarget {
 std::string id;
 std::size_t node_index{};
 std::string key;
};
// Q2f opt-in source proof. No decode, I/O, catalog lookup or geometry implied.
// Inherits Q2e caps. Images are in DOM order, at most eight, unique keys.
// A target is a div with a nonempty globally unique ID and four explicit
// zero border widths. Only the four closed background properties are added.
// HTML entities are decoded before the strict url(catalog_token) grammar;
// actual DOM attributes and effective resource URLs must still be verified.
// Q2e admission and rendering remain separate and unchanged.
struct HtmlBlocksImagesAdmission {
 HtmlBlocksAdmission blocks;
 std::vector<HtmlBlockImageTarget> images;
};
using BlocksImagesAdmissionResult=std::variant<HtmlBlocksImagesAdmission,HtmlAdmissionError>;
BlocksImagesAdmissionResult admit_real_html_blocks_images(std::string_view,const std::vector<std::string>&,std::int32_t,LineBox,Cancellation* =nullptr,std::size_t initial_logical=0) noexcept;
}
