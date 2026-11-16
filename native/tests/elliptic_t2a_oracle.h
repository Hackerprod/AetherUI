#pragma once
#include "elliptic_t1_oracle.h"
// Reference input is an explicit list of already-positioned shapes per draw.
// It does not interpret PreparedFrame, product transforms, or clip stacks.
namespace elliptic_composition_oracle {
using Shape=elliptic_oracle::Shape;using Bytes=std::vector<std::uint8_t>;
using Color=std::array<long double,4>; // BGRA straight
struct Paint {Shape shape;std::vector<Shape> clips;Color color;};
inline Bytes blank(int w,int h){return Bytes(std::size_t(w)*h*4);}
inline void paint(Bytes& dst,int w,int h,const Paint& p){
 using namespace elliptic_oracle;
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  long double l=std::max(p.shape.l,(long double)x),t=std::max(p.shape.t,(long double)y),r=std::min(p.shape.r,(long double)x+1),b=std::min(p.shape.b,(long double)y+1);
  for(const auto& c:p.clips){l=std::max(l,c.l);t=std::max(t,c.t);r=std::min(r,c.r);b=std::min(b,c.b);}
  if(l>=r||t>=b)continue;
  unsigned n=0;for(int j=0;j<16;++j)for(int i=0;i<16;++i){
   auto sx=l+(i+.5L)*(r-l)/16,sy=t+(j+.5L)*(b-t)/16;bool yes=inside(p.shape,sx,sy);
   for(const auto& c:p.clips)yes=yes&&inside(c,sx,sy);n+=yes;
  }
  auto a=M(Q(p.color[3]),Q((r-l)*(b-t)*n/256));auto at=(std::size_t(y)*w+x)*4;
  for(int c=0;c<3;++c)dst[at+c]=std::uint8_t(M(Q(p.color[c]),a)+M(dst[at+c],255-a));
  dst[at+3]=std::uint8_t(a+M(dst[at+3],255-a));
 }
}
inline Bytes render(int w,int h,const std::vector<Paint>& draws){auto b=blank(w,h);for(const auto& p:draws)paint(b,w,h,p);return b;}
inline void composite(Bytes& destination,const Bytes& source,long double opacity){
 using namespace elliptic_oracle;assert(source.size()==destination.size());auto q=Q(opacity);
 for(std::size_t i=0;i<source.size();i+=4){auto a=M(source[i+3],q);for(int c=0;c<4;++c)destination[i+c]=std::uint8_t(M(source[i+c],q)+M(destination[i+c],255-a));}
}
}
