#pragma once
#include "real_html_blocks.h"
#include "../native_paint_list.h"
namespace aether::rendering::text::truetype {
struct HtmlImageTarget {
 std::string id,key;
 preparation::ImageRef resource;
 aether_rectf crop{},destination{};
 std::size_t command_index{};
};
struct HtmlBlocksImagesWork {
 std::size_t catalog_entries{},catalog_key_bytes{},unique_images{},unique_image_bytes{},image_targets{},image_commands{},logical_image_metadata_bytes{};
};
struct HtmlBlocksImagesFrame {
 std::string profile="real_html_blocks_images_latin1_v1";
 HtmlBlocksFrame blocks;
 std::shared_ptr<const preparation::ImageSet> images;
 std::vector<HtmlImageTarget> targets;
 HtmlBlocksImagesWork work;
};
using HtmlBlocksImagesResult=std::variant<std::shared_ptr<const HtmlBlocksImagesFrame>,HtmlTextError>;
// Closed profile: one no-repeat, unscaled intrinsic image per unique target key.
// Catalog resources are already validated immutable BGRA8; no decoder or I/O.
HtmlBlocksImagesResult render_real_html_blocks_images(std::string_view,const std::vector<std::string>&,
 std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,
 const NativeImageCatalog&,Cancellation* =nullptr) noexcept;
}
