#pragma once
#include "real_font_metrics.h"
#include "shaped_run.h"
#include <limits>
namespace aether::rendering {class SelectedCaptureOperation;}
namespace aether::layout {class SelectedCaptureTestAccess;}
namespace aether::rendering::text {
// Real immutable measurement/outline product; no raster allocation until placement.
// Private operation must obtain cmap(source)->glyph and vertical metrics from this
// exact binding, then pass its authentic outline. No external product factory.
// Logical baselines outside [0,height] are TEXT_LINEBOX_UNSUPPORTED, not a
// claim of general CSS negative-leading support; ShapedRun bounds stay unchanged.
class SelectedTextProduct final {
 friend class aether::rendering::SelectedCaptureOperation;
 friend class aether::layout::SelectedCaptureTestAccess;
 SelectedTextProduct(std::shared_ptr<const truetype::RealFontBinding> binding,
  std::shared_ptr<const truetype::Outline> outline,std::vector<std::string> pieces,
  truetype::FontVerticalMetrics vertical,float height)
 :binding(std::move(binding)),outline(std::move(outline)),pieces(std::move(pieces)),
  vertical(vertical),run(make_run(this->binding,this->outline,this->pieces,vertical,height)) {}
 static std::shared_ptr<const ShapedRun> make_run(
  const std::shared_ptr<const truetype::RealFontBinding>& binding,
  const std::shared_ptr<const truetype::Outline>& outline,const std::vector<std::string>& pieces,
  truetype::FontVerticalMetrics vertical,float height) {
  if(!binding||!outline||outline->source()!=binding->font()->source||
     outline->units_per_em()!=vertical.units_per_em||!vertical.units_per_em||pieces.size()>128)
   throw std::invalid_argument("TEXT_PRODUCT_IDENTITY");
  std::string source;
  for(const auto& piece:pieces){if(piece.size()>1||source.size()+piece.size()>1)throw std::invalid_argument("TEXT_LAYOUT_UNSUPPORTED");source+=piece;}
  if(source.size()!=1||!((source[0]>='A'&&source[0]<='Z')||(source[0]>='a'&&source[0]<='z')))
   throw std::invalid_argument("TEXT_LAYOUT_UNSUPPORTED");
  const double scale=double(binding->font()->size_26_6)/(64.0*vertical.units_per_em);
  const double ascent=vertical.ascent*scale,descent=vertical.descent*scale;
  const double baseline=ascent+(double(height)-ascent-descent)/2.0;
  if(!std::isfinite(height)||height<=0||height>65536||!std::isfinite(baseline)||baseline<0||baseline>height)
   throw std::invalid_argument("TEXT_LINEBOX_UNSUPPORTED");
  const auto numerator=std::int64_t(outline->metrics.advance)*binding->font()->size_26_6;
  const auto advance=(numerator+vertical.units_per_em/2)/vertical.units_per_em;
  if(advance<0||advance>65536LL*64)throw std::length_error("TEXT_ADVANCE_LIMIT");
  std::vector<Glyph> glyphs{{outline->glyph_id,0,std::int32_t(advance),0,0,0}};
  return std::make_shared<const ShapedRun>(binding->font(),std::move(source),std::move(glyphs),
   aether_text_metrics_t{float(advance)/64.f,height,float(baseline)});
 }
public:
 SelectedTextProduct(const SelectedTextProduct&)=delete;
 SelectedTextProduct& operator=(const SelectedTextProduct&)=delete;
 const std::shared_ptr<const truetype::RealFontBinding> binding;
 const std::shared_ptr<const truetype::Outline> outline;
 const std::vector<std::string> pieces;
 const truetype::FontVerticalMetrics vertical;
 const std::shared_ptr<const ShapedRun> run;
};
struct SelectedRasterCoordinate {std::int32_t value26_6;std::int32_t pixel;unsigned phase;};
// New-path-only final absolute placement: nearest, ties away from zero.
// Does not change logical CSS boxes or relax the software renderer's grid guard.
inline SelectedRasterCoordinate selected_raster_coordinate(double logical) {
 if(!std::isfinite(logical)||std::abs(logical)>65536)throw std::invalid_argument("TEXT_RASTER_COORDINATE");
 const double rounded=std::round(logical*64.0);
 if(rounded<std::numeric_limits<std::int32_t>::min()||rounded>std::numeric_limits<std::int32_t>::max())
  throw std::overflow_error("TEXT_RASTER_COORDINATE");
 const auto q=static_cast<std::int32_t>(rounded);
 const auto pixel=q/64-(q%64<0);
 return {q,pixel,unsigned(q-pixel*64)};
}
}
