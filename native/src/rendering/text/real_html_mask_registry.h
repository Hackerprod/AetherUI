#pragma once
#include "real_html_text.h"
#include <algorithm>
#include <map>
#include <tuple>
namespace aether::rendering { class SelectedCaptureOperation; }
namespace aether::layout { class RealHtmlTextTestAccess; }
namespace aether::rendering::text::truetype {
// Operation-local registry. No exported API or persisted callback. The test
// friend is defined only in native/tests, like the real policy test seam.
class RealHtmlMaskRegistry final {
 friend class aether::rendering::SelectedCaptureOperation;
 friend class aether::layout::RealHtmlTextTestAccess;
 friend HtmlTextResult render_real_html_text(std::string_view,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,Cancellation*) noexcept;
 friend HtmlBlocksResult render_real_html_blocks(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,Cancellation*) noexcept;
 friend HtmlBlocksImagesResult render_real_html_blocks_images(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,const NativeImageCatalog&,Cancellation*) noexcept;
 friend HtmlViewportResult render_real_html_viewport(std::string_view,const std::vector<std::string>&,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,const NativeImageCatalog&,ViewportOffset,Cancellation*) noexcept;
 using Key=std::tuple<const FontInstance*,std::uint32_t,std::uint32_t,std::uint8_t,std::uint8_t>;
 std::map<Key,std::shared_ptr<const GlyphMaskA8>> entries_;
 std::vector<std::shared_ptr<const GlyphMaskA8>> masks_;
 RealHtmlMaskRegistry()=default;
 template<class Check> void add(const std::shared_ptr<const GlyphMaskA8>& m,Check&& check) {
  check();
  Key key{m->font.get(),m->glyph_id,m->flags,m->phase_x,m->phase_y};
  auto [it,added]=entries_.emplace(key,m);
  if(added) {
   masks_.push_back(m);
  } else {
   const auto& old=*it->second;
   if(old.width!=m->width || old.height!=m->height || old.stride!=m->stride ||
      old.bearing_left!=m->bearing_left || old.bearing_top!=m->bearing_top ||
      old.bytes.size()!=m->bytes.size())throw HtmlTextError::invalid_input;
   for(std::size_t offset=0;offset<m->bytes.size();offset+=256) {
    check();
    const auto end=std::min(offset+256,m->bytes.size());
    if(!std::equal(m->bytes.begin()+offset,m->bytes.begin()+end,old.bytes.begin()+offset))
     throw HtmlTextError::invalid_input;
   }
  }
 }
 std::size_t metadata_bytes() const noexcept {
  return masks_.size()*(sizeof(Key)+2*sizeof(std::shared_ptr<const GlyphMaskA8>));
 }
};
}
