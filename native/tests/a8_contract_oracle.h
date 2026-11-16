#pragma once
// Specification-only oracle. No renderer/preparation/header or helper dependency.
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdint>
namespace oracle {
using Bytes=std::vector<std::uint8_t>;
struct Clip { long double l,t,r,b,radius; };
struct Draw { int x,y,w,h,stride; Bytes mask; std::vector<Clip> clips; std::array<long double,4> bgra; };
struct Op { int kind=0; Draw draw{}; long double opacity=1; }; // 0 draw, 1 begin, 2 end
inline unsigned Q(long double n){return unsigned(std::floor(std::clamp(n,0.L,1.L)*255+.5L));}
inline unsigned M(unsigned a,unsigned b){return (a*b+127)/255;}
inline bool inside(const Clip& c,long double x,long double y){
 if(x<c.l||x>c.r||y<c.t||y>c.b)return false;
 if(c.radius==0)return true;
 const long double radius=c.radius;
 const std::array<std::array<long double,2>,4> centers{{{c.l+radius,c.t+radius},{c.r-radius,c.t+radius},{c.r-radius,c.b-radius},{c.l+radius,c.b-radius}}};
 const std::array<bool,4> active{{x<=c.l+radius&&y<=c.t+radius,x>=c.r-radius&&y<=c.t+radius,x>=c.r-radius&&y>=c.b-radius,x<=c.l+radius&&y>=c.b-radius}};
 for(unsigned i=0;i<4;++i)if(active[i]){const auto dx=(x-centers[i][0])/radius,dy=(y-centers[i][1])/radius;if(dx*dx+dy*dy>1)return false;}
 return true;
}
inline long double coverage(const Draw& d,int x,int y,int w,int h){
 long double l=std::max({static_cast<long double>(0),static_cast<long double>(x),static_cast<long double>(d.x)});
 long double t=std::max({static_cast<long double>(0),static_cast<long double>(y),static_cast<long double>(d.y)});
 long double r=std::min({static_cast<long double>(w),static_cast<long double>(x+1),static_cast<long double>(d.x+d.w)});
 long double b=std::min({static_cast<long double>(h),static_cast<long double>(y+1),static_cast<long double>(d.y+d.h)});
 for(const auto& c:d.clips){l=std::max(l,c.l);t=std::max(t,c.t);r=std::min(r,c.r);b=std::min(b,c.b);}
 if(l>=r||t>=b)return 0;
 unsigned count=0;
 for(int sy=0;sy<16;++sy)for(int sx=0;sx<16;++sx){long double px=l+(sx+.5L)*(r-l)/16,py=t+(sy+.5L)*(b-t)/16;bool yes=true;for(const auto& c:d.clips)yes=yes&&inside(c,px,py);count+=yes;}
 return (r-l)*(b-t)*count/256;
}
inline void over(Bytes& dst,std::size_t i,const std::array<unsigned,4>& src){for(int c=0;c<4;++c)dst[i+c]=std::uint8_t(src[c]+M(dst[i+c],255-src[3]));}
inline Bytes render(int w,int h,const std::vector<Op>& ops){
 std::vector<Bytes> layers{Bytes(std::size_t(w)*h*4)};std::vector<unsigned> opacity;
 for(const auto& op:ops){
  if(op.kind==1){layers.emplace_back(std::size_t(w)*h*4,0);opacity.push_back(Q(op.opacity));continue;}
  if(op.kind==2){assert(layers.size()>1);auto src=std::move(layers.back());layers.pop_back();auto o=opacity.back();opacity.pop_back();for(std::size_t i=0;i<src.size();i+=4)over(layers.back(),i,{M(src[i],o),M(src[i+1],o),M(src[i+2],o),M(src[i+3],o)});continue;}
  const auto& d=op.draw;
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto cov=Q(coverage(d,x,y,w,h));if(!cov)continue;assert(x>=d.x&&y>=d.y&&x<d.x+d.w&&y<d.y+d.h);const unsigned glyph=M(d.mask[std::size_t(y-d.y)*d.stride+x-d.x],cov),a=M(Q(d.bgra[3]),glyph);over(layers.back(),(std::size_t(y)*w+x)*4,{M(Q(d.bgra[0]),a),M(Q(d.bgra[1]),a),M(Q(d.bgra[2]),a),a});}
 }
 assert(layers.size()==1);return layers[0];
}
inline void self_checks(){
 assert(Q(.5L)==128&&M(128,128)==64);
 const Clip c{0,0,8,8,3};assert(!inside(c,0,0));assert(inside(c,3,0));assert(inside(c,4,4));
 Draw d{0,0,8,8,8,Bytes(64,255),{c},{1,1,1,1}};
 assert(coverage(d,0,0,16,12)==3.L/128.L);assert(coverage(d,3,3,16,12)==1);assert(coverage(d,2,0,16,12)>0&&coverage(d,2,0,16,12)<1);
 // Rational circle boundary: radius 5, vector (3,4).
 assert(inside({0,0,10,10,5},2,1));assert(!inside({0,0,10,10,5},1,1));
}
}
