#pragma once
#include "owned_allocator.h"
#include "../text/shaped_run.h"
#include <span>
namespace aether::rendering::html_ltr::owned_text {
// Interaction-only storage. Algorithms consume views, never copying to a legacy
// default-allocator source/run/mask. Domain is retained by each allocation.
struct Source final {
 static std::span<const uint8_t> checked(std::span<const uint8_t> input,const OwnedBudget::Handle& domain){
  if(!domain||input.empty()||!input.data()||input.size()>text::max_font_bytes)throw Error{AETHER_ERROR_INVALID_ARGUMENT};
  return input;
 }
 static OwnedString hash(OwnedBudget::Handle domain,std::span<const uint8_t> bytes){
  OwnedString out(OwnedAllocator<char>(domain,OwnedCategory::inputs));
  text::detail::Sha256 h;h.update(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));h.finish_into(out);return out;
 }
 const OwnedVector<uint8_t> bytes;
 const OwnedString digest;
 Source(OwnedBudget::Handle domain,std::span<const uint8_t> input)
  :bytes(checked(input,domain).begin(),input.end(),OwnedAllocator<uint8_t>(domain,OwnedCategory::inputs)),digest(hash(domain,bytes)) {}
 std::span<const uint8_t> byte_view()const noexcept{return bytes;}
 OwnedBudget::Handle domain()const noexcept{return bytes.get_allocator().domain();}
};

// Allocates only the small derived adapter/control block. The font byte address
// is identical and the strong immutable source owner survives every adapter.
struct SourceBridge final {
 static std::shared_ptr<const text::FontSource> create(const std::shared_ptr<const Source>& source){
  if(!source)throw Error{AETHER_ERROR_INVALID_ARGUMENT};
  return std::allocate_shared<const text::FontSource>(OwnedAllocator<text::FontSource>(source->domain(),OwnedCategory::glyphs),text::FontSource::SharedBytes(source,source->byte_view()));
 }
};
inline std::shared_ptr<const text::FontSource> legacy_source(const std::shared_ptr<const Source>& source){return SourceBridge::create(source);}
struct Font final {
 std::shared_ptr<const Source> source;
 uint32_t face_index{},raster_flags{1};int32_t size_26_6{};
 Font(std::shared_ptr<const Source> s,int32_t size):source(std::move(s)),size_26_6(size){if(!source||size<1||size>8192)throw Error{AETHER_ERROR_INVALID_ARGUMENT};}
};
struct Run final {
 std::shared_ptr<const Font> font;
 OwnedString source;
 OwnedVector<text::Glyph> glyphs;
 aether_text_metrics_t metrics{};
 Run(OwnedBudget::Handle domain,std::shared_ptr<const Font> f):font(std::move(f)),source(OwnedAllocator<char>(domain,OwnedCategory::glyphs)),glyphs(OwnedAllocator<text::Glyph>(domain,OwnedCategory::glyphs)){}
};
struct Mask final {
 std::shared_ptr<const Font> font;
 uint32_t glyph_id{},flags{},width{},height{},stride{};
 uint8_t phase_x{},phase_y{};int32_t bearing_left{},bearing_top{};
 OwnedVector<uint8_t> bytes;
 Mask(OwnedBudget::Handle domain,std::shared_ptr<const Font> f):font(std::move(f)),bytes(OwnedAllocator<uint8_t>(domain,OwnedCategory::glyphs)){}
};
}
