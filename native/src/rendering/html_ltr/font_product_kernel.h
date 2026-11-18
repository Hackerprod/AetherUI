#pragma once
#include "raster_coordinate.h"
#include "../text/real_font_metrics.h"
namespace aether::rendering::html_ltr {
// Allocation-free shared shaping arithmetic. Storage and admission/work charging
// remain caller-owned; callback ordering is identical to the legacy product loop.
template<class Glyphs,class Prefixes,class Decode,class Advance>
aether_text_metrics_t shape_font_product(std::string_view source,Glyphs& glyphs,Prefixes& prefixes,
 const text::truetype::FontVerticalMetrics& vm,int size,int line_height,Decode&& decode,Advance&& advance){
 if(!vm.units_per_em)throw Error{AETHER_ERROR_INTERNAL};
 const double scale=double(size)/vm.units_per_em;
 const double baseline=double(vm.ascent)*scale+(double(line_height)-(double(vm.ascent)+vm.descent)*scale)/2;
 if(!std::isfinite(baseline)||baseline<0||baseline>line_height)throw Error{AETHER_ERROR_UNSUPPORTED};
 uint64_t sum=0;
 for(size_t at=0;at<source.size();){
  const auto cluster=at;const auto cp=decode(source,at);const auto [glyph,delta]=advance(cp);
  sum=checked_design_add(sum,delta);const double width=scaled_design_prefix(sum,scale);(void)width;
  const auto q=quantize_absolute(scaled_design_prefix(delta,scale));
  prefixes.push_back(sum);glyphs.push_back({glyph,uint32_t(cluster),int32_t(q),0,0,0});
 }
 const double width=scaled_design_prefix(sum,scale);auto metrics=checked_metrics(width,line_height,baseline);
 if(!source.empty())validate_run_transport(source,glyphs,width,line_height,baseline);
 return metrics;
}
}
