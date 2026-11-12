#pragma once
#include <aether/rendering/preparation/paint_snapshot.hpp>
#include <array>
#include <cmath>
namespace aether::rendering::software::border_math {
namespace p=aether::rendering::preparation;
struct Point {double x,y;};
inline bool quad(const std::array<Point,4>& q,double x,double y) noexcept {
 for(unsigned i=0;i<4;++i){const auto a=q[i],b=q[(i+1)%4];if((b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x)<0)return false;}
 return true;
}
inline double thickness(const p::BorderGroup& g,int side) noexcept {
 const auto o=g.outer,i=g.inner;
 switch(side){case 0:return double(i.y)-o.y;case 1:return double(o.x)+o.width-i.x-i.width;case 2:return double(o.y)+o.height-i.y-i.height;default:return double(i.x)-o.x;}
}
inline int owner(const p::BorderGroup& g,double x,double y) noexcept {
 const auto o=g.outer,i=g.inner;
 const Point a[]{ {o.x,o.y},{double(o.x)+o.width,o.y},{double(o.x)+o.width,double(o.y)+o.height},{o.x,double(o.y)+o.height} };
 const Point b[]{ {i.x,i.y},{double(i.x)+i.width,i.y},{double(i.x)+i.width,double(i.y)+i.height},{i.x,double(i.y)+i.height} };
 // Fixed top,right,bottom,left tie precedence, including absent sides.
 for(int s=0;s<4;++s)if(thickness(g,s)>0 && quad({a[s],a[(s+1)%4],b[(s+1)%4],b[s]},x,y))return s;
 return -1;
}
inline bool dash(const p::BorderGroup& g,int side,double x,double y) noexcept {
 const double t=thickness(g,side),length=(side%2==0?g.outer.width:g.outer.height);
 const double n=std::max(1.0,std::floor(length/(6*t)+.5));
 const double period=length/n;
 const double u=side%2==0?x-g.outer.x:y-g.outer.y;
 const double phase=u/period;
 return std::abs(phase-std::floor(phase+.5))<.25;
}
}
