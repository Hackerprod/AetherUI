#pragma once
#include "owned_font_decoder.h"
#include "../text/detail/raster_kernel.h"
namespace aether::rendering::html_ltr {
struct OwnedRasterOutput {std::shared_ptr<const owned_text::Mask> mask;double advance_px{};std::int32_t advance_26_6{};};
using OwnedRasterResult=std::variant<OwnedRasterOutput,text::truetype::RasterError>;
namespace owned_raster_detail {
namespace tt=text::truetype;
struct Storage {
 using Outline=owned_text::Outline;using Font=owned_text::Font;using Result=OwnedRasterResult;using Output=OwnedRasterOutput;
 OwnedBudget::Handle domain;
 template<class T> auto vector()const{return OwnedVector<T>(OwnedAllocator<T>(domain,OwnedCategory::temporary));}
 auto bytes()const{return OwnedVector<std::uint8_t>(OwnedAllocator<std::uint8_t>(domain,OwnedCategory::glyphs));}
 bool identity(const Outline& o,const Font& f)const{return f.source==o.source()&&f.source&&domain.same_domain(f.source->domain());}
 unsigned face_index(const Outline&)const noexcept{return 0;}
 auto mask(std::shared_ptr<const Font> font,std::uint32_t id,std::uint32_t flags,unsigned px,unsigned py,std::uint32_t w,std::uint32_t h,std::uint32_t stride,std::int32_t left,std::int32_t top,OwnedVector<std::uint8_t> bytes)const {
  auto result=std::allocate_shared<owned_text::Mask>(OwnedAllocator<owned_text::Mask>(domain,OwnedCategory::glyphs),domain,std::move(font));
  result->glyph_id=id;result->flags=flags;result->phase_x=px;result->phase_y=py;result->width=w;result->height=h;result->stride=stride;result->bearing_left=left;result->bearing_top=top;
  result->bytes=std::move(bytes);
  return std::shared_ptr<const owned_text::Mask>(std::move(result));
 }
 tt::RasterError unexpected()const noexcept {
  try{throw;}catch(const Error& e){return e.status==AETHER_ERROR_LIMIT_EXCEEDED?tt::RasterError::budget_exceeded:tt::RasterError::numeric_failure;}catch(...){return tt::RasterError::numeric_failure;}
 }
};
}
// Same document raster algorithm and work reservations as R3; only physical
// output/scratch ownership differs. No legacy vector or mask is manufactured.
inline OwnedRasterResult rasterize_owned_document(OwnedBudget& budget,std::shared_ptr<const owned_text::Outline> outline,std::shared_ptr<const owned_text::Font> font,unsigned x,unsigned y,text::truetype::AggregateBudget& raster,text::truetype::AggregateBudget& mask_bytes,text::truetype::Cancellation* cancel=nullptr,text::truetype::RasterWork* work=nullptr)noexcept{
 return text::truetype::raster_kernel::rasterize_impl(owned_raster_detail::Storage{budget.handle()},std::move(outline),std::move(font),x,y,cancel,work,&raster,&mask_bytes,true);
}
}
