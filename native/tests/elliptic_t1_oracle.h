#pragma once
// Independent specification oracle: no product headers or geometry helpers.
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cassert>
namespace elliptic_oracle {
struct Shape {long double l,t,r,b;std::array<std::array<long double,2>,4> radii;};
inline bool inside(const Shape& s,long double x,long double y){
 if(x<s.l||x>s.r||y<s.t||y>s.b)return false;
 for(unsigned corner=0;corner<4;++corner){
  auto rx=s.radii[corner][0],ry=s.radii[corner][1];if(rx==0||ry==0)continue;
  const bool left=corner==0||corner==3,top=corner<2;
  const long double cx=left?s.l+rx:s.r-rx,cy=top?s.t+ry:s.b-ry;
  if((left?x<=cx:x>=cx)&&(top?y<=cy:y>=cy)){
   const long double u=(x-cx)/rx,v=(y-cy)/ry;if(u*u+v*v>1)return false;
  }
 }
 return true;
}
inline unsigned Q(long double x){return unsigned(std::floor(std::clamp(x,0.L,1.L)*255+.5L));}
inline unsigned M(unsigned a,unsigned b){return (a*b+127)/255;}
inline unsigned count(const Shape& s,long double l,long double t,long double r,long double b){unsigned n=0;for(int sy=0;sy<16;++sy)for(int sx=0;sx<16;++sx)n+=inside(s,l+(sx+.5L)*(r-l)/16,t+(sy+.5L)*(b-t)/16);return n;}
inline std::vector<std::uint8_t> solid(const Shape& s,int w,int h,std::array<long double,4> bgra){
 std::vector<std::uint8_t> out(std::size_t(w)*h*4);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  const long double l=std::max(s.l,static_cast<long double>(x)),t=std::max(s.t,static_cast<long double>(y)),r=std::min(s.r,static_cast<long double>(x+1)),b=std::min(s.b,static_cast<long double>(y+1));
  if(l>=r||t>=b)continue;const auto coverage=(r-l)*(b-t)*count(s,l,t,r,b)/256;const unsigned a=M(Q(bgra[3]),Q(coverage));const auto i=(std::size_t(y)*w+x)*4;for(int c=0;c<3;++c)out[i+c]=M(Q(bgra[c]),a);out[i+3]=a;
 }
 return out;
}
inline void controls(){
 Shape diagonal{0,0,100,100,{{{90,90},{0,0},{90,90},{0,0}}}};
 assert(!inside(diagonal,80,80));assert(!inside(diagonal,20,20));assert(inside(diagonal,20,80));assert(inside(diagonal,50,50));
 Shape fractional{.25L,.25L,2.25L,2.25L,{{{1,.5L},{0,0},{0,0},{0,0}}}};
 assert(count(fractional,.25L,.25L,1,1)==207);assert(Q(.75L*.75L*207/256)==116);
 auto bytes=solid(fractional,4,4,{1,1,1,1});for(int c=0;c<4;++c)assert(bytes[c]==116);
 // Exact ellipse boundary, and a degenerate corner that must be rectangular.
 Shape one{0,0,8,8,{{{4,2},{0,0},{0,0},{0,0}}}};assert(inside(one,0,2));assert(!inside(one,0,0));one.radii[0][1]=0;assert(inside(one,0,0));
}
}
