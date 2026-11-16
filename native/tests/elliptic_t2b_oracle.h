#pragma once
#include "elliptic_t2a_oracle.h"
namespace elliptic_paint_oracle {
using namespace elliptic_oracle;
using Bytes=std::vector<std::uint8_t>;using Color=std::array<long double,4>;
struct Bounds{long double l,t,r,b;};
inline Bounds bounds(const Shape& s,const std::vector<Shape>& clips,int x,int y){
 Bounds b{std::max(s.l,(long double)x),std::max(s.t,(long double)y),std::min(s.r,(long double)x+1),std::min(s.b,(long double)y+1)};
 for(const auto& c:clips){b.l=std::max(b.l,c.l);b.t=std::max(b.t,c.t);b.r=std::min(b.r,c.r);b.b=std::min(b.b,c.b);}return b;
}
template<class F>inline long double samples(const Shape& s,const std::vector<Shape>& clips,Bounds b,F accept){
 if(b.l>=b.r||b.t>=b.b)return 0;
 unsigned n=0;
 for(int j=0;j<16;++j)for(int i=0;i<16;++i){auto x=b.l+(i+.5L)*(b.r-b.l)/16,y=b.t+(j+.5L)*(b.b-b.t)/16;bool yes=inside(s,x,y);for(const auto& c:clips){yes=yes&&inside(c,x,y);}if(yes){++n;accept(x,y);}}
 return (b.r-b.l)*(b.b-b.t)*n/256;
}
struct Stop{long double at;Color bgra;};
struct Gradient{Shape shape;std::vector<Shape> clips;Bounds ramp;bool vertical;long double opacity;std::vector<Stop> stops;};
inline Color ramp(const Gradient& g,long double x,long double y){
 const auto t=g.vertical?(y-g.ramp.t)/(g.ramp.b-g.ramp.t):(x-g.ramp.l)/(g.ramp.r-g.ramp.l);
 std::size_t a=0,b=0;
 if(t<g.stops.front().at)a=b=0;
 else if(t>=g.stops.back().at)a=b=g.stops.size()-1;
 else for(std::size_t k=0;k+1<g.stops.size();++k)if(g.stops[k].at<=t&&t<g.stops[k+1].at){a=k;b=k+1;break;}
 const auto u=a==b?0.L:(t-g.stops[a].at)/(g.stops[b].at-g.stops[a].at);Color out{};
 for(int c=0;c<3;++c)out[c]=(g.stops[a].bgra[c]*g.stops[a].bgra[3]*(1-u)+g.stops[b].bgra[c]*g.stops[b].bgra[3]*u)*g.opacity;
 out[3]=(g.stops[a].bgra[3]*(1-u)+g.stops[b].bgra[3]*u)*g.opacity;return out;
}
inline Bytes gradient(int w,int h,const Gradient& g){
 Bytes out(w*h*4);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto b=bounds(g.shape,g.clips,x,y);if(b.l>=b.r||b.t>=b.b)continue;Color sum{};samples(g.shape,g.clips,b,[&](auto sx,auto sy){auto c=ramp(g,sx,sy);for(int k=0;k<4;++k)sum[k]+=c[k];});auto scale=(b.r-b.l)*(b.b-b.t)/256;auto i=(y*w+x)*4;auto alpha=Q(sum[3]*scale);for(int k=0;k<3;++k)out[i+k]=std::min(alpha,Q(sum[k]*scale));out[i+3]=alpha;}return out;
}
struct Image{int width,height;Bytes bytes;Bounds source;Shape destination;std::vector<Shape> clips;bool full;long double opacity;};
inline Color image_sample(const Image& im,long double x,long double y){
 auto lo_x=im.full?0.L:im.source.l,hi_x=im.full?(long double)im.width-1:im.source.r-1,lo_y=im.full?0.L:im.source.t,hi_y=im.full?(long double)im.height-1:im.source.b-1;
 auto u=std::clamp(im.source.l+(x-im.destination.l)/(im.destination.r-im.destination.l)*(im.source.r-im.source.l)-.5L,lo_x,hi_x),v=std::clamp(im.source.t+(y-im.destination.t)/(im.destination.b-im.destination.t)*(im.source.b-im.source.t)-.5L,lo_y,hi_y);
 int a=int(std::floor(u)),b=int(std::floor(v)),a1=std::min(a+1,int(hi_x)),b1=std::min(b+1,int(hi_y));auto fx=u-a,fy=v-b;Color out{};
 for(int k=0;k<4;++k){auto at=[&](int xx,int yy){return im.bytes[(yy*im.width+xx)*4+k];};out[k]=(at(a,b)*(1-fx)*(1-fy)+at(a1,b)*fx*(1-fy)+at(a,b1)*(1-fx)*fy+at(a1,b1)*fx*fy)*im.opacity/255;}return out;
}
inline Bytes image(int w,int h,const Image& im){Bytes out(w*h*4);for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto b=bounds(im.destination,im.clips,x,y);auto coverage=samples(im.destination,im.clips,b,[](auto,auto){});auto color=image_sample(im,x+.5L,y+.5L);auto alpha=Q(color[3]*coverage);auto i=(y*w+x)*4;for(int k=0;k<3;++k)out[i+k]=std::min(alpha,Q(color[k]*coverage));out[i+3]=alpha;}return out;}
inline Bytes mask(int w,int h,int left,int top,const Bytes& bytes,const std::vector<Shape>& clips,Color color){
 Shape s{(long double)left,(long double)top,(long double)left+8,(long double)top+8,{}};Bytes out(w*h*4);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){if(x<left||x>=left+8||y<top||y>=top+8)continue;auto b=bounds(s,clips,x,y);auto coverage=samples(s,clips,b,[](auto,auto){});auto glyph=M(bytes[(y-top)*10+x-left],Q(coverage));auto alpha=M(Q(color[3]),glyph);auto i=(y*w+x)*4;for(int k=0;k<3;++k)out[i+k]=M(Q(color[k]),alpha);out[i+3]=alpha;}return out;
}
}
