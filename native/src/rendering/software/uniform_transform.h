#pragma once
#include <cmath>
namespace aether::rendering::software::uniform_transform {
// Local backend state only: no layout, CSS resolution, or float-product reuse.
struct State { double scale{1}, x{}, y{}; };
inline bool coordinate(double n) noexcept { return std::isfinite(n) && std::abs(n)<=65536; }
inline bool scale(double n) noexcept { return std::isfinite(n) && n>=1.0/16 && n<=16; }
template<class Matrix> bool compose(State parent, Matrix m, State& out) noexcept {
 if(m.m12!=0 || m.m21!=0 || m.m11!=m.m22 || !scale(m.m11) ||
    !coordinate(m.dx) || !coordinate(m.dy))return false;
 State next{parent.scale*double(m.m11),parent.scale*double(m.dx)+parent.x,parent.scale*double(m.dy)+parent.y};
 if(!scale(next.scale)||!coordinate(next.x)||!coordinate(next.y))return false;
 out=next;return true;
}
template<class Rect> Rect apply(Rect r,State s) noexcept {
 return {s.scale*r.l+s.x,s.scale*r.t+s.y,s.scale*r.r+s.x,s.scale*r.b+s.y};
}
template<class Rect> bool bounded(Rect r) noexcept {
 return coordinate(r.l)&&coordinate(r.t)&&coordinate(r.r)&&coordinate(r.b);
}
template<class Radii> bool radii(Radii r,double s) noexcept {
 return coordinate(double(r.top_left_x)*s)&&coordinate(double(r.top_left_y)*s)&&
 coordinate(double(r.top_right_x)*s)&&coordinate(double(r.top_right_y)*s)&&
 coordinate(double(r.bottom_right_x)*s)&&coordinate(double(r.bottom_right_y)*s)&&
 coordinate(double(r.bottom_left_x)*s)&&coordinate(double(r.bottom_left_y)*s);
}
// Negative values are immutable event references, never geometric lengths.
inline double radius(double reference,double s) noexcept {return reference<0?reference:reference*s;}
}
