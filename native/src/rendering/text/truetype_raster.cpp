#include "truetype_raster.h"
#include "detail/raster_kernel.h"
namespace aether::rendering::text::truetype {
namespace {
struct LegacyRasterStorage {
 using Outline=truetype::Outline;using Font=FontInstance;using Result=RasterResult;using Output=RasterOutput;
 template<class T> auto vector()const{return std::vector<T>{};}
 auto bytes()const{return std::vector<std::uint8_t>{};}
 bool identity(const Outline& o,const Font& f)const{return f.source==o.source()&&f.source_hash==o.digest();}
 auto face_index(const Outline& o)const{return o.face_index();}
 auto mask(std::shared_ptr<const Font> f,std::uint32_t id,std::uint32_t flags,unsigned px,unsigned py,std::uint32_t w,std::uint32_t h,std::uint32_t stride,std::int32_t left,std::int32_t top,std::vector<std::uint8_t> bytes)const {
  return std::make_shared<const GlyphMaskA8>(std::move(f),id,flags,px,py,w,h,stride,left,top,std::move(bytes));
 }
 RasterError unexpected()const noexcept{return RasterError::numeric_failure;}
};
}
RasterResult rasterize(std::shared_ptr<const Outline> o,std::shared_ptr<const FontInstance> f,unsigned x,unsigned y,Cancellation* c,RasterWork* w,AggregateBudget* a) noexcept {
 return raster_kernel::rasterize_impl(LegacyRasterStorage{},std::move(o),std::move(f),x,y,c,w,a,nullptr);
}
RasterResult rasterize_budgeted(std::shared_ptr<const Outline> o,std::shared_ptr<const FontInstance> f,unsigned x,unsigned y,AggregateBudget& a,AggregateBudget& m,Cancellation* c,RasterWork* w) noexcept {
 return raster_kernel::rasterize_impl(LegacyRasterStorage{},std::move(o),std::move(f),x,y,c,w,&a,&m);
}
RasterResult rasterize_document_budgeted(std::shared_ptr<const Outline> o,std::shared_ptr<const FontInstance> f,unsigned x,unsigned y,AggregateBudget& a,AggregateBudget& m,Cancellation* c,RasterWork* w) noexcept {
 return raster_kernel::rasterize_impl(LegacyRasterStorage{},std::move(o),std::move(f),x,y,c,w,&a,&m,true);
}

}
