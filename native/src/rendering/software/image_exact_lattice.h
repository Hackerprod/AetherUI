#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Private Image arithmetic. No allocation, persistent state, or admission changes.
namespace aether::rendering::software::image_exact_lattice {
struct Context {
 std::int32_t source[4], destination[4], clipped[4], bounds[4];
 std::uint32_t opacity;
 std::uint64_t vx,vy;
};
static_assert(sizeof(Context)<=96);
inline bool coordinate(double value,std::int32_t& result) noexcept {
 if(!std::isfinite(value)||value<=-32768||value>=32768)return false;
 const double scaled=value*256;
 if(std::floor(scaled)!=scaled)return false;
 result=static_cast<std::int32_t>(scaled);return true;
}
inline bool prepare(Context& out,const double* source,const double* destination,
 const double* clipped,double opacity,std::uint32_t width,std::uint32_t height,
 bool full,std::size_t clip_depth) noexcept {
 // Eligibility is arithmetic only; the caller supplies the joint coverage count.
 (void)clip_depth;
#if defined(__SIZEOF_INT128__)
 if(width==0||height==0||width>16384||height>16384||
    !std::isfinite(opacity)||opacity<0||opacity>1)return false;
 const double scaled=opacity*65536;
 if(std::floor(scaled)!=scaled)return false;
 for(unsigned j=0;j<4;++j)if(!coordinate(source[j],out.source[j])||
    !coordinate(destination[j],out.destination[j])||!coordinate(clipped[j],out.clipped[j]))return false;
 const std::int64_t dx=std::int64_t(out.destination[2])-out.destination[0];
 const std::int64_t dy=std::int64_t(out.destination[3])-out.destination[1];
 if(dx<=0||dy<=0||out.clipped[2]<=out.clipped[0]||out.clipped[3]<=out.clipped[1]||
    out.source[0]<0||out.source[1]<0||std::int64_t(out.source[2])-out.source[0]<256||
    std::int64_t(out.source[3])-out.source[1]<256||
    std::int64_t(out.source[2])>std::int64_t(width)*256||
    std::int64_t(out.source[3])>std::int64_t(height)*256)return false;
 if(!full)for(unsigned j=0;j<4;++j)if(out.source[j]%256!=0)return false;
 out.bounds[0]=full?0:out.source[0]/256;
 out.bounds[1]=full?0:out.source[1]/256;
 out.bounds[2]=full?std::int32_t(width)-1:out.source[2]/256-1;
 out.bounds[3]=full?std::int32_t(height)-1:out.source[3]/256-1;
 out.opacity=static_cast<std::uint32_t>(scaled);
 out.vx=static_cast<std::uint64_t>(dx)*256;
 out.vy=static_cast<std::uint64_t>(dy)*256;
 return true;
#else
 (void)out;(void)source;(void)destination;(void)clipped;(void)opacity;
 (void)width;(void)height;(void)full;(void)clip_depth;return false;
#endif
}
#if defined(__SIZEOF_INT128__)
using Wide=unsigned __int128;
inline unsigned half_up(Wide numerator,Wide denominator) noexcept {
 // 0 <= numerator / denominator <= 255; denominator > 0 and < 2^104.
 // Numerator < 2^112; largest denominator shift (7) is < 2^111.
 unsigned result=0;
 for(int bit=7;bit>=0;--bit){
  const Wide shifted=denominator<<bit;
  if(numerator>=shifted){numerator-=shifted;result|=1u<<bit;}
 }
 return result+unsigned(numerator>=(denominator>>1)+(denominator&1));
}
struct Axis { std::uint64_t fraction; std::size_t lo,hi; };
inline Axis axis(const Context& ctx,unsigned k,std::int64_t center,std::uint64_t denominator) noexcept {
 const std::int64_t d=std::int64_t(ctx.destination[k+2])-ctx.destination[k];
 const std::int64_t s=std::int64_t(ctx.source[k+2])-ctx.source[k];
 std::int64_t u=std::int64_t(ctx.source[k])*d+(center-ctx.destination[k])*s-128*d;
 const std::int64_t low=std::int64_t(ctx.bounds[k])*std::int64_t(denominator);
 const std::int64_t high=std::int64_t(ctx.bounds[k+2])*std::int64_t(denominator);
 u=std::clamp(u,low,high);
 const auto positive=static_cast<std::uint64_t>(u);
 const auto lo=positive/denominator;
 return {positive%denominator,static_cast<std::size_t>(lo),
         static_cast<std::size_t>(std::min(lo+1,std::uint64_t(ctx.bounds[k+2])))};
}
#endif
inline void sample(const Context& ctx,const std::uint8_t* pixels,std::size_t stride,
 int x,int y,unsigned* out,unsigned count=256) noexcept {
 // Exact arithmetic conditioned on the existing sampler, not exact curve membership.
 assert(count<=256);
#if defined(__SIZEOF_INT128__)
 // Caller iterates the retained clipped rect; x/y are legal frame pixel indices.
 const std::int64_t px=std::int64_t(x)*256,py=std::int64_t(y)*256;
 const auto ax=axis(ctx,0,px+128,ctx.vx),ay=axis(ctx,1,py+128,ctx.vy);
 const std::int64_t width=std::min(std::int64_t(ctx.clipped[2]),px+256)-std::max(std::int64_t(ctx.clipped[0]),px);
 const std::int64_t height=std::min(std::int64_t(ctx.clipped[3]),py+256)-std::max(std::int64_t(ctx.clipped[1]),py);
 if(width<=0||height<=0){for(unsigned c=0;c<4;++c)out[c]=0;return;}
 const auto area=std::uint64_t(width)*std::uint64_t(height);
 const Wide weights[]{Wide(ctx.vx-ax.fraction)*(ctx.vy-ay.fraction),
  Wide(ax.fraction)*(ctx.vy-ay.fraction),Wide(ctx.vx-ax.fraction)*ay.fraction,
  Wide(ax.fraction)*ay.fraction};
 // T < 2^64; promotion precedes the shift. count is unsigned, including 256.
 const Wide denominator=(Wide(ctx.vx)*ctx.vy)<<40;
 for(unsigned c=0;c<4;++c){
  const Wide numerator=Wide(pixels[ay.lo*stride+ax.lo*4+c])*weights[0]+
   Wide(pixels[ay.lo*stride+ax.hi*4+c])*weights[1]+
   Wide(pixels[ay.hi*stride+ax.lo*4+c])*weights[2]+
   Wide(pixels[ay.hi*stride+ax.hi*4+c])*weights[3];
  // Every partial product is Wide: < 2^88, < 2^104, then < 2^112.
  out[c]=half_up(numerator*area*ctx.opacity*count,denominator);
 }
#else
 (void)ctx;(void)pixels;(void)stride;(void)x;(void)y;(void)count;
 for(unsigned c=0;c<4;++c)out[c]=0; // Unreachable: prepare always returns false.
#endif
}
} // namespace aether::rendering::software::image_exact_lattice
