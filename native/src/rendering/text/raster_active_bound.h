#pragma once
#include "truetype_raster.h"
#include <cmath>
namespace aether::rendering::text::truetype::active_bound_detail {
// Pure bounded arithmetic for the new document reservation only. Tick is a
// synchronous caller-owned accounting callback; it is never retained.
// Sample expression has the same operation order/types as the existing raster.
inline double sample(std::uint64_t j,std::int32_t top){return -double(top)+(j/64)+((j%64)+.5)/64;}
inline unsigned search_bound(std::uint64_t scans){if(scans>64ULL*2048)throw RasterError::numeric_failure;unsigned k=1;for(std::uint64_t n=1;n<scans+1;n*=2)++k;return k;}
template<class Tick>
std::uint64_t count(std::int32_t top,std::uint64_t scans,double low,double high,Tick&& tick){
 if(scans>64ULL*2048||top < -65536||top>65536||!std::isfinite(low)||!std::isfinite(high)||low>high)throw RasterError::numeric_failure;
 auto lower=[&](double target){tick();std::uint64_t first=0,last=scans;while(first<last){tick();const auto middle=first+(last-first)/2;if(sample(middle,top)<target)first=middle+1;else last=middle;}return first;};
 const auto first=lower(low),last=lower(high);if(last<first)throw RasterError::numeric_failure;return last-first;
}
}
