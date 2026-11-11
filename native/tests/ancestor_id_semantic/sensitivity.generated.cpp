// Generated exhaustive leaf sensitivity controls. No engine mutation.
#include <aether/aether.h>
#include <filesystem>
#include <cstring>
#include <sstream>
#include <iostream>
#include "snapshot.h"
template<class T> std::string dump(const T& v) { std::ostringstream s; Writer w{s}; value(w,"value",v); return s.str(); }
int main() { int checks=0; int failures=0;
{
aether_rectf v{};
const auto original=dump(v);
{
auto old=v.x;
v.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x\n"; }
v.x=old;
}
{
auto old=v.y;
v.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y\n"; }
v.y=old;
}
{
auto old=v.width;
v.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.width\n"; }
v.width=old;
}
{
auto old=v.height;
v.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.height\n"; }
v.height=old;
}
}
{
aether_color_rgba v{};
const auto original=dump(v);
{
auto old=v.r;
v.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.r\n"; }
v.r=old;
}
{
auto old=v.g;
v.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.g\n"; }
v.g=old;
}
{
auto old=v.b;
v.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.b\n"; }
v.b=old;
}
{
auto old=v.a;
v.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.a\n"; }
v.a=old;
}
}
{
aether_color_edges v{};
const auto original=dump(v);
{
auto old=v.left.r;
v.left.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.r\n"; }
v.left.r=old;
}
{
auto old=v.left.g;
v.left.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.g\n"; }
v.left.g=old;
}
{
auto old=v.left.b;
v.left.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.b\n"; }
v.left.b=old;
}
{
auto old=v.left.a;
v.left.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.a\n"; }
v.left.a=old;
}
{
auto old=v.top.r;
v.top.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.r\n"; }
v.top.r=old;
}
{
auto old=v.top.g;
v.top.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.g\n"; }
v.top.g=old;
}
{
auto old=v.top.b;
v.top.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.b\n"; }
v.top.b=old;
}
{
auto old=v.top.a;
v.top.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.a\n"; }
v.top.a=old;
}
{
auto old=v.right.r;
v.right.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.r\n"; }
v.right.r=old;
}
{
auto old=v.right.g;
v.right.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.g\n"; }
v.right.g=old;
}
{
auto old=v.right.b;
v.right.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.b\n"; }
v.right.b=old;
}
{
auto old=v.right.a;
v.right.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.a\n"; }
v.right.a=old;
}
{
auto old=v.bottom.r;
v.bottom.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.r\n"; }
v.bottom.r=old;
}
{
auto old=v.bottom.g;
v.bottom.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.g\n"; }
v.bottom.g=old;
}
{
auto old=v.bottom.b;
v.bottom.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.b\n"; }
v.bottom.b=old;
}
{
auto old=v.bottom.a;
v.bottom.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.a\n"; }
v.bottom.a=old;
}
}
{
aether_border_style_edges v{};
const auto original=dump(v);
{
auto old=v.left;
v.left=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left\n"; }
v.left=old;
}
{
auto old=v.top;
v.top=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top\n"; }
v.top=old;
}
{
auto old=v.right;
v.right=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right\n"; }
v.right=old;
}
{
auto old=v.bottom;
v.bottom=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom\n"; }
v.bottom=old;
}
}
{
aether_corner_radii v{};
const auto original=dump(v);
{
auto old=v.top_left_x;
v.top_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left_x\n"; }
v.top_left_x=old;
}
{
auto old=v.top_left_y;
v.top_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left_y\n"; }
v.top_left_y=old;
}
{
auto old=v.top_right_x;
v.top_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right_x\n"; }
v.top_right_x=old;
}
{
auto old=v.top_right_y;
v.top_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right_y\n"; }
v.top_right_y=old;
}
{
auto old=v.bottom_right_x;
v.bottom_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right_x\n"; }
v.bottom_right_x=old;
}
{
auto old=v.bottom_right_y;
v.bottom_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right_y\n"; }
v.bottom_right_y=old;
}
{
auto old=v.bottom_left_x;
v.bottom_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left_x\n"; }
v.bottom_left_x=old;
}
{
auto old=v.bottom_left_y;
v.bottom_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left_y\n"; }
v.bottom_left_y=old;
}
}
{
aether_shadow v{};
const auto original=dump(v);
{
auto old=v.offset_x;
v.offset_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.offset_x\n"; }
v.offset_x=old;
}
{
auto old=v.offset_y;
v.offset_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.offset_y\n"; }
v.offset_y=old;
}
{
auto old=v.blur_radius;
v.blur_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.blur_radius\n"; }
v.blur_radius=old;
}
{
auto old=v.spread_radius;
v.spread_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.spread_radius\n"; }
v.spread_radius=old;
}
{
auto old=v.color.r;
v.color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.r\n"; }
v.color.r=old;
}
{
auto old=v.color.g;
v.color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.g\n"; }
v.color.g=old;
}
{
auto old=v.color.b;
v.color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.b\n"; }
v.color.b=old;
}
{
auto old=v.color.a;
v.color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.a\n"; }
v.color.a=old;
}
{
auto old=v.inset;
v.inset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset\n"; }
v.inset=old;
}
}
{
aether_shadow_list v{};
aether_shadow storage1[1]{}; v.items=storage1;
v.count=1;
const auto original=dump(v);
{
auto old=v.items[0].offset_x;
v.items[0].offset_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].offset_x\n"; }
v.items[0].offset_x=old;
}
{
auto old=v.items[0].offset_y;
v.items[0].offset_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].offset_y\n"; }
v.items[0].offset_y=old;
}
{
auto old=v.items[0].blur_radius;
v.items[0].blur_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].blur_radius\n"; }
v.items[0].blur_radius=old;
}
{
auto old=v.items[0].spread_radius;
v.items[0].spread_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].spread_radius\n"; }
v.items[0].spread_radius=old;
}
{
auto old=v.items[0].color.r;
v.items[0].color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].color.r\n"; }
v.items[0].color.r=old;
}
{
auto old=v.items[0].color.g;
v.items[0].color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].color.g\n"; }
v.items[0].color.g=old;
}
{
auto old=v.items[0].color.b;
v.items[0].color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].color.b\n"; }
v.items[0].color.b=old;
}
{
auto old=v.items[0].color.a;
v.items[0].color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].color.a\n"; }
v.items[0].color.a=old;
}
{
auto old=v.items[0].inset;
v.items[0].inset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].inset\n"; }
v.items[0].inset=old;
}
{
auto old=v.count;
v.count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.count\n"; }
v.count=old;
}
}
{
aether_css_length v{};
const auto original=dump(v);
{
auto old=v.value;
v.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.value\n"; }
v.value=old;
}
{
auto old=v.unit;
v.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.unit\n"; }
v.unit=old;
}
{
auto old=v.linear_percent;
v.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.linear_percent\n"; }
v.linear_percent=old;
}
{
auto old=v.linear_em;
v.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.linear_em\n"; }
v.linear_em=old;
}
{
auto old=v.linear_rem;
v.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.linear_rem\n"; }
v.linear_rem=old;
}
{
auto old=v.linear_vw;
v.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.linear_vw\n"; }
v.linear_vw=old;
}
{
auto old=v.linear_vh;
v.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.linear_vh\n"; }
v.linear_vh=old;
}
}
{
aether_gradient_stop v{};
const auto original=dump(v);
{
auto old=v.color.r;
v.color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.r\n"; }
v.color.r=old;
}
{
auto old=v.color.g;
v.color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.g\n"; }
v.color.g=old;
}
{
auto old=v.color.b;
v.color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.b\n"; }
v.color.b=old;
}
{
auto old=v.color.a;
v.color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.a\n"; }
v.color.a=old;
}
{
auto old=v.position.value;
v.position.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position.value\n"; }
v.position.value=old;
}
{
auto old=v.position.unit;
v.position.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position.unit\n"; }
v.position.unit=old;
}
{
auto old=v.position.linear_percent;
v.position.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position.linear_percent\n"; }
v.position.linear_percent=old;
}
{
auto old=v.position.linear_em;
v.position.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position.linear_em\n"; }
v.position.linear_em=old;
}
{
auto old=v.position.linear_rem;
v.position.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position.linear_rem\n"; }
v.position.linear_rem=old;
}
{
auto old=v.position.linear_vw;
v.position.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position.linear_vw\n"; }
v.position.linear_vw=old;
}
{
auto old=v.position.linear_vh;
v.position.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position.linear_vh\n"; }
v.position.linear_vh=old;
}
{
auto old=v.offset;
v.offset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.offset\n"; }
v.offset=old;
}
{
auto old=v.has_position;
v.has_position=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.has_position\n"; }
v.has_position=old;
}
}
{
aether_linear_gradient v{};
aether_gradient_stop storage2[1]{}; v.stops=storage2;
v.stop_count=1;
const auto original=dump(v);
{
auto old=v.direction_x;
v.direction_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.direction_x\n"; }
v.direction_x=old;
}
{
auto old=v.direction_y;
v.direction_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.direction_y\n"; }
v.direction_y=old;
}
{
auto old=v.stops[0].color.r;
v.stops[0].color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].color.r\n"; }
v.stops[0].color.r=old;
}
{
auto old=v.stops[0].color.g;
v.stops[0].color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].color.g\n"; }
v.stops[0].color.g=old;
}
{
auto old=v.stops[0].color.b;
v.stops[0].color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].color.b\n"; }
v.stops[0].color.b=old;
}
{
auto old=v.stops[0].color.a;
v.stops[0].color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].color.a\n"; }
v.stops[0].color.a=old;
}
{
auto old=v.stops[0].position.value;
v.stops[0].position.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].position.value\n"; }
v.stops[0].position.value=old;
}
{
auto old=v.stops[0].position.unit;
v.stops[0].position.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].position.unit\n"; }
v.stops[0].position.unit=old;
}
{
auto old=v.stops[0].position.linear_percent;
v.stops[0].position.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].position.linear_percent\n"; }
v.stops[0].position.linear_percent=old;
}
{
auto old=v.stops[0].position.linear_em;
v.stops[0].position.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].position.linear_em\n"; }
v.stops[0].position.linear_em=old;
}
{
auto old=v.stops[0].position.linear_rem;
v.stops[0].position.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].position.linear_rem\n"; }
v.stops[0].position.linear_rem=old;
}
{
auto old=v.stops[0].position.linear_vw;
v.stops[0].position.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].position.linear_vw\n"; }
v.stops[0].position.linear_vw=old;
}
{
auto old=v.stops[0].position.linear_vh;
v.stops[0].position.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].position.linear_vh\n"; }
v.stops[0].position.linear_vh=old;
}
{
auto old=v.stops[0].offset;
v.stops[0].offset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].offset\n"; }
v.stops[0].offset=old;
}
{
auto old=v.stops[0].has_position;
v.stops[0].has_position=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stops[0].has_position\n"; }
v.stops[0].has_position=old;
}
{
auto old=v.stop_count;
v.stop_count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.stop_count\n"; }
v.stop_count=old;
}
}
{
aether_flex_basis v{};
const auto original=dump(v);
{
auto old=v.kind;
v.kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.kind\n"; }
v.kind=old;
}
{
auto old=v.length.value;
v.length.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length.value\n"; }
v.length.value=old;
}
{
auto old=v.length.unit;
v.length.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length.unit\n"; }
v.length.unit=old;
}
{
auto old=v.length.linear_percent;
v.length.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length.linear_percent\n"; }
v.length.linear_percent=old;
}
{
auto old=v.length.linear_em;
v.length.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length.linear_em\n"; }
v.length.linear_em=old;
}
{
auto old=v.length.linear_rem;
v.length.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length.linear_rem\n"; }
v.length.linear_rem=old;
}
{
auto old=v.length.linear_vw;
v.length.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length.linear_vw\n"; }
v.length.linear_vw=old;
}
{
auto old=v.length.linear_vh;
v.length.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length.linear_vh\n"; }
v.length.linear_vh=old;
}
}
{
aether_corner_radius v{};
const auto original=dump(v);
{
auto old=v.horizontal.value;
v.horizontal.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.horizontal.value\n"; }
v.horizontal.value=old;
}
{
auto old=v.horizontal.unit;
v.horizontal.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.horizontal.unit\n"; }
v.horizontal.unit=old;
}
{
auto old=v.horizontal.linear_percent;
v.horizontal.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.horizontal.linear_percent\n"; }
v.horizontal.linear_percent=old;
}
{
auto old=v.horizontal.linear_em;
v.horizontal.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.horizontal.linear_em\n"; }
v.horizontal.linear_em=old;
}
{
auto old=v.horizontal.linear_rem;
v.horizontal.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.horizontal.linear_rem\n"; }
v.horizontal.linear_rem=old;
}
{
auto old=v.horizontal.linear_vw;
v.horizontal.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.horizontal.linear_vw\n"; }
v.horizontal.linear_vw=old;
}
{
auto old=v.horizontal.linear_vh;
v.horizontal.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.horizontal.linear_vh\n"; }
v.horizontal.linear_vh=old;
}
{
auto old=v.vertical.value;
v.vertical.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.vertical.value\n"; }
v.vertical.value=old;
}
{
auto old=v.vertical.unit;
v.vertical.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.vertical.unit\n"; }
v.vertical.unit=old;
}
{
auto old=v.vertical.linear_percent;
v.vertical.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.vertical.linear_percent\n"; }
v.vertical.linear_percent=old;
}
{
auto old=v.vertical.linear_em;
v.vertical.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.vertical.linear_em\n"; }
v.vertical.linear_em=old;
}
{
auto old=v.vertical.linear_rem;
v.vertical.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.vertical.linear_rem\n"; }
v.vertical.linear_rem=old;
}
{
auto old=v.vertical.linear_vw;
v.vertical.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.vertical.linear_vw\n"; }
v.vertical.linear_vw=old;
}
{
auto old=v.vertical.linear_vh;
v.vertical.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.vertical.linear_vh\n"; }
v.vertical.linear_vh=old;
}
}
{
aether_border_radii v{};
const auto original=dump(v);
{
auto old=v.top_left.horizontal.value;
v.top_left.horizontal.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.horizontal.value\n"; }
v.top_left.horizontal.value=old;
}
{
auto old=v.top_left.horizontal.unit;
v.top_left.horizontal.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.horizontal.unit\n"; }
v.top_left.horizontal.unit=old;
}
{
auto old=v.top_left.horizontal.linear_percent;
v.top_left.horizontal.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.horizontal.linear_percent\n"; }
v.top_left.horizontal.linear_percent=old;
}
{
auto old=v.top_left.horizontal.linear_em;
v.top_left.horizontal.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.horizontal.linear_em\n"; }
v.top_left.horizontal.linear_em=old;
}
{
auto old=v.top_left.horizontal.linear_rem;
v.top_left.horizontal.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.horizontal.linear_rem\n"; }
v.top_left.horizontal.linear_rem=old;
}
{
auto old=v.top_left.horizontal.linear_vw;
v.top_left.horizontal.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.horizontal.linear_vw\n"; }
v.top_left.horizontal.linear_vw=old;
}
{
auto old=v.top_left.horizontal.linear_vh;
v.top_left.horizontal.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.horizontal.linear_vh\n"; }
v.top_left.horizontal.linear_vh=old;
}
{
auto old=v.top_left.vertical.value;
v.top_left.vertical.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.vertical.value\n"; }
v.top_left.vertical.value=old;
}
{
auto old=v.top_left.vertical.unit;
v.top_left.vertical.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.vertical.unit\n"; }
v.top_left.vertical.unit=old;
}
{
auto old=v.top_left.vertical.linear_percent;
v.top_left.vertical.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.vertical.linear_percent\n"; }
v.top_left.vertical.linear_percent=old;
}
{
auto old=v.top_left.vertical.linear_em;
v.top_left.vertical.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.vertical.linear_em\n"; }
v.top_left.vertical.linear_em=old;
}
{
auto old=v.top_left.vertical.linear_rem;
v.top_left.vertical.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.vertical.linear_rem\n"; }
v.top_left.vertical.linear_rem=old;
}
{
auto old=v.top_left.vertical.linear_vw;
v.top_left.vertical.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.vertical.linear_vw\n"; }
v.top_left.vertical.linear_vw=old;
}
{
auto old=v.top_left.vertical.linear_vh;
v.top_left.vertical.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_left.vertical.linear_vh\n"; }
v.top_left.vertical.linear_vh=old;
}
{
auto old=v.top_right.horizontal.value;
v.top_right.horizontal.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.horizontal.value\n"; }
v.top_right.horizontal.value=old;
}
{
auto old=v.top_right.horizontal.unit;
v.top_right.horizontal.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.horizontal.unit\n"; }
v.top_right.horizontal.unit=old;
}
{
auto old=v.top_right.horizontal.linear_percent;
v.top_right.horizontal.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.horizontal.linear_percent\n"; }
v.top_right.horizontal.linear_percent=old;
}
{
auto old=v.top_right.horizontal.linear_em;
v.top_right.horizontal.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.horizontal.linear_em\n"; }
v.top_right.horizontal.linear_em=old;
}
{
auto old=v.top_right.horizontal.linear_rem;
v.top_right.horizontal.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.horizontal.linear_rem\n"; }
v.top_right.horizontal.linear_rem=old;
}
{
auto old=v.top_right.horizontal.linear_vw;
v.top_right.horizontal.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.horizontal.linear_vw\n"; }
v.top_right.horizontal.linear_vw=old;
}
{
auto old=v.top_right.horizontal.linear_vh;
v.top_right.horizontal.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.horizontal.linear_vh\n"; }
v.top_right.horizontal.linear_vh=old;
}
{
auto old=v.top_right.vertical.value;
v.top_right.vertical.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.vertical.value\n"; }
v.top_right.vertical.value=old;
}
{
auto old=v.top_right.vertical.unit;
v.top_right.vertical.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.vertical.unit\n"; }
v.top_right.vertical.unit=old;
}
{
auto old=v.top_right.vertical.linear_percent;
v.top_right.vertical.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.vertical.linear_percent\n"; }
v.top_right.vertical.linear_percent=old;
}
{
auto old=v.top_right.vertical.linear_em;
v.top_right.vertical.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.vertical.linear_em\n"; }
v.top_right.vertical.linear_em=old;
}
{
auto old=v.top_right.vertical.linear_rem;
v.top_right.vertical.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.vertical.linear_rem\n"; }
v.top_right.vertical.linear_rem=old;
}
{
auto old=v.top_right.vertical.linear_vw;
v.top_right.vertical.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.vertical.linear_vw\n"; }
v.top_right.vertical.linear_vw=old;
}
{
auto old=v.top_right.vertical.linear_vh;
v.top_right.vertical.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top_right.vertical.linear_vh\n"; }
v.top_right.vertical.linear_vh=old;
}
{
auto old=v.bottom_right.horizontal.value;
v.bottom_right.horizontal.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.horizontal.value\n"; }
v.bottom_right.horizontal.value=old;
}
{
auto old=v.bottom_right.horizontal.unit;
v.bottom_right.horizontal.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.horizontal.unit\n"; }
v.bottom_right.horizontal.unit=old;
}
{
auto old=v.bottom_right.horizontal.linear_percent;
v.bottom_right.horizontal.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.horizontal.linear_percent\n"; }
v.bottom_right.horizontal.linear_percent=old;
}
{
auto old=v.bottom_right.horizontal.linear_em;
v.bottom_right.horizontal.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.horizontal.linear_em\n"; }
v.bottom_right.horizontal.linear_em=old;
}
{
auto old=v.bottom_right.horizontal.linear_rem;
v.bottom_right.horizontal.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.horizontal.linear_rem\n"; }
v.bottom_right.horizontal.linear_rem=old;
}
{
auto old=v.bottom_right.horizontal.linear_vw;
v.bottom_right.horizontal.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.horizontal.linear_vw\n"; }
v.bottom_right.horizontal.linear_vw=old;
}
{
auto old=v.bottom_right.horizontal.linear_vh;
v.bottom_right.horizontal.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.horizontal.linear_vh\n"; }
v.bottom_right.horizontal.linear_vh=old;
}
{
auto old=v.bottom_right.vertical.value;
v.bottom_right.vertical.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.vertical.value\n"; }
v.bottom_right.vertical.value=old;
}
{
auto old=v.bottom_right.vertical.unit;
v.bottom_right.vertical.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.vertical.unit\n"; }
v.bottom_right.vertical.unit=old;
}
{
auto old=v.bottom_right.vertical.linear_percent;
v.bottom_right.vertical.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.vertical.linear_percent\n"; }
v.bottom_right.vertical.linear_percent=old;
}
{
auto old=v.bottom_right.vertical.linear_em;
v.bottom_right.vertical.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.vertical.linear_em\n"; }
v.bottom_right.vertical.linear_em=old;
}
{
auto old=v.bottom_right.vertical.linear_rem;
v.bottom_right.vertical.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.vertical.linear_rem\n"; }
v.bottom_right.vertical.linear_rem=old;
}
{
auto old=v.bottom_right.vertical.linear_vw;
v.bottom_right.vertical.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.vertical.linear_vw\n"; }
v.bottom_right.vertical.linear_vw=old;
}
{
auto old=v.bottom_right.vertical.linear_vh;
v.bottom_right.vertical.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_right.vertical.linear_vh\n"; }
v.bottom_right.vertical.linear_vh=old;
}
{
auto old=v.bottom_left.horizontal.value;
v.bottom_left.horizontal.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.horizontal.value\n"; }
v.bottom_left.horizontal.value=old;
}
{
auto old=v.bottom_left.horizontal.unit;
v.bottom_left.horizontal.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.horizontal.unit\n"; }
v.bottom_left.horizontal.unit=old;
}
{
auto old=v.bottom_left.horizontal.linear_percent;
v.bottom_left.horizontal.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.horizontal.linear_percent\n"; }
v.bottom_left.horizontal.linear_percent=old;
}
{
auto old=v.bottom_left.horizontal.linear_em;
v.bottom_left.horizontal.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.horizontal.linear_em\n"; }
v.bottom_left.horizontal.linear_em=old;
}
{
auto old=v.bottom_left.horizontal.linear_rem;
v.bottom_left.horizontal.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.horizontal.linear_rem\n"; }
v.bottom_left.horizontal.linear_rem=old;
}
{
auto old=v.bottom_left.horizontal.linear_vw;
v.bottom_left.horizontal.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.horizontal.linear_vw\n"; }
v.bottom_left.horizontal.linear_vw=old;
}
{
auto old=v.bottom_left.horizontal.linear_vh;
v.bottom_left.horizontal.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.horizontal.linear_vh\n"; }
v.bottom_left.horizontal.linear_vh=old;
}
{
auto old=v.bottom_left.vertical.value;
v.bottom_left.vertical.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.vertical.value\n"; }
v.bottom_left.vertical.value=old;
}
{
auto old=v.bottom_left.vertical.unit;
v.bottom_left.vertical.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.vertical.unit\n"; }
v.bottom_left.vertical.unit=old;
}
{
auto old=v.bottom_left.vertical.linear_percent;
v.bottom_left.vertical.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.vertical.linear_percent\n"; }
v.bottom_left.vertical.linear_percent=old;
}
{
auto old=v.bottom_left.vertical.linear_em;
v.bottom_left.vertical.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.vertical.linear_em\n"; }
v.bottom_left.vertical.linear_em=old;
}
{
auto old=v.bottom_left.vertical.linear_rem;
v.bottom_left.vertical.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.vertical.linear_rem\n"; }
v.bottom_left.vertical.linear_rem=old;
}
{
auto old=v.bottom_left.vertical.linear_vw;
v.bottom_left.vertical.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.vertical.linear_vw\n"; }
v.bottom_left.vertical.linear_vw=old;
}
{
auto old=v.bottom_left.vertical.linear_vh;
v.bottom_left.vertical.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom_left.vertical.linear_vh\n"; }
v.bottom_left.vertical.linear_vh=old;
}
}
{
aether_css_edges v{};
const auto original=dump(v);
{
auto old=v.left.value;
v.left.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.value\n"; }
v.left.value=old;
}
{
auto old=v.left.unit;
v.left.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.unit\n"; }
v.left.unit=old;
}
{
auto old=v.left.linear_percent;
v.left.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.linear_percent\n"; }
v.left.linear_percent=old;
}
{
auto old=v.left.linear_em;
v.left.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.linear_em\n"; }
v.left.linear_em=old;
}
{
auto old=v.left.linear_rem;
v.left.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.linear_rem\n"; }
v.left.linear_rem=old;
}
{
auto old=v.left.linear_vw;
v.left.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.linear_vw\n"; }
v.left.linear_vw=old;
}
{
auto old=v.left.linear_vh;
v.left.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.left.linear_vh\n"; }
v.left.linear_vh=old;
}
{
auto old=v.top.value;
v.top.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.value\n"; }
v.top.value=old;
}
{
auto old=v.top.unit;
v.top.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.unit\n"; }
v.top.unit=old;
}
{
auto old=v.top.linear_percent;
v.top.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.linear_percent\n"; }
v.top.linear_percent=old;
}
{
auto old=v.top.linear_em;
v.top.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.linear_em\n"; }
v.top.linear_em=old;
}
{
auto old=v.top.linear_rem;
v.top.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.linear_rem\n"; }
v.top.linear_rem=old;
}
{
auto old=v.top.linear_vw;
v.top.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.linear_vw\n"; }
v.top.linear_vw=old;
}
{
auto old=v.top.linear_vh;
v.top.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.top.linear_vh\n"; }
v.top.linear_vh=old;
}
{
auto old=v.right.value;
v.right.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.value\n"; }
v.right.value=old;
}
{
auto old=v.right.unit;
v.right.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.unit\n"; }
v.right.unit=old;
}
{
auto old=v.right.linear_percent;
v.right.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.linear_percent\n"; }
v.right.linear_percent=old;
}
{
auto old=v.right.linear_em;
v.right.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.linear_em\n"; }
v.right.linear_em=old;
}
{
auto old=v.right.linear_rem;
v.right.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.linear_rem\n"; }
v.right.linear_rem=old;
}
{
auto old=v.right.linear_vw;
v.right.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.linear_vw\n"; }
v.right.linear_vw=old;
}
{
auto old=v.right.linear_vh;
v.right.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.right.linear_vh\n"; }
v.right.linear_vh=old;
}
{
auto old=v.bottom.value;
v.bottom.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.value\n"; }
v.bottom.value=old;
}
{
auto old=v.bottom.unit;
v.bottom.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.unit\n"; }
v.bottom.unit=old;
}
{
auto old=v.bottom.linear_percent;
v.bottom.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.linear_percent\n"; }
v.bottom.linear_percent=old;
}
{
auto old=v.bottom.linear_em;
v.bottom.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.linear_em\n"; }
v.bottom.linear_em=old;
}
{
auto old=v.bottom.linear_rem;
v.bottom.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.linear_rem\n"; }
v.bottom.linear_rem=old;
}
{
auto old=v.bottom.linear_vw;
v.bottom.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.linear_vw\n"; }
v.bottom.linear_vw=old;
}
{
auto old=v.bottom.linear_vh;
v.bottom.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.bottom.linear_vh\n"; }
v.bottom.linear_vh=old;
}
}
{
aether_background_layer v{};
aether_gradient_stop storage3[1]{}; v.gradient.stops=storage3;
v.gradient.stop_count=1;
char storage4[]="a"; v.image_source=storage4;
const auto original=dump(v);
{
auto old=v.kind;
v.kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.kind\n"; }
v.kind=old;
}
{
auto old=v.gradient.direction_x;
v.gradient.direction_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.direction_x\n"; }
v.gradient.direction_x=old;
}
{
auto old=v.gradient.direction_y;
v.gradient.direction_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.direction_y\n"; }
v.gradient.direction_y=old;
}
{
auto old=v.gradient.stops[0].color.r;
v.gradient.stops[0].color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].color.r\n"; }
v.gradient.stops[0].color.r=old;
}
{
auto old=v.gradient.stops[0].color.g;
v.gradient.stops[0].color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].color.g\n"; }
v.gradient.stops[0].color.g=old;
}
{
auto old=v.gradient.stops[0].color.b;
v.gradient.stops[0].color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].color.b\n"; }
v.gradient.stops[0].color.b=old;
}
{
auto old=v.gradient.stops[0].color.a;
v.gradient.stops[0].color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].color.a\n"; }
v.gradient.stops[0].color.a=old;
}
{
auto old=v.gradient.stops[0].position.value;
v.gradient.stops[0].position.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].position.value\n"; }
v.gradient.stops[0].position.value=old;
}
{
auto old=v.gradient.stops[0].position.unit;
v.gradient.stops[0].position.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].position.unit\n"; }
v.gradient.stops[0].position.unit=old;
}
{
auto old=v.gradient.stops[0].position.linear_percent;
v.gradient.stops[0].position.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].position.linear_percent\n"; }
v.gradient.stops[0].position.linear_percent=old;
}
{
auto old=v.gradient.stops[0].position.linear_em;
v.gradient.stops[0].position.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].position.linear_em\n"; }
v.gradient.stops[0].position.linear_em=old;
}
{
auto old=v.gradient.stops[0].position.linear_rem;
v.gradient.stops[0].position.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].position.linear_rem\n"; }
v.gradient.stops[0].position.linear_rem=old;
}
{
auto old=v.gradient.stops[0].position.linear_vw;
v.gradient.stops[0].position.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].position.linear_vw\n"; }
v.gradient.stops[0].position.linear_vw=old;
}
{
auto old=v.gradient.stops[0].position.linear_vh;
v.gradient.stops[0].position.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].position.linear_vh\n"; }
v.gradient.stops[0].position.linear_vh=old;
}
{
auto old=v.gradient.stops[0].offset;
v.gradient.stops[0].offset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].offset\n"; }
v.gradient.stops[0].offset=old;
}
{
auto old=v.gradient.stops[0].has_position;
v.gradient.stops[0].has_position=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stops[0].has_position\n"; }
v.gradient.stops[0].has_position=old;
}
{
auto old=v.gradient.stop_count;
v.gradient.stop_count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gradient.stop_count\n"; }
v.gradient.stop_count=old;
}
{
auto old=v.image_source[0];
v.image_source[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.image_source[0]\n"; }
v.image_source[0]=old;
}
{
auto old=v.position_x.value;
v.position_x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_x.value\n"; }
v.position_x.value=old;
}
{
auto old=v.position_x.unit;
v.position_x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_x.unit\n"; }
v.position_x.unit=old;
}
{
auto old=v.position_x.linear_percent;
v.position_x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_x.linear_percent\n"; }
v.position_x.linear_percent=old;
}
{
auto old=v.position_x.linear_em;
v.position_x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_x.linear_em\n"; }
v.position_x.linear_em=old;
}
{
auto old=v.position_x.linear_rem;
v.position_x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_x.linear_rem\n"; }
v.position_x.linear_rem=old;
}
{
auto old=v.position_x.linear_vw;
v.position_x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_x.linear_vw\n"; }
v.position_x.linear_vw=old;
}
{
auto old=v.position_x.linear_vh;
v.position_x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_x.linear_vh\n"; }
v.position_x.linear_vh=old;
}
{
auto old=v.position_y.value;
v.position_y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_y.value\n"; }
v.position_y.value=old;
}
{
auto old=v.position_y.unit;
v.position_y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_y.unit\n"; }
v.position_y.unit=old;
}
{
auto old=v.position_y.linear_percent;
v.position_y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_y.linear_percent\n"; }
v.position_y.linear_percent=old;
}
{
auto old=v.position_y.linear_em;
v.position_y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_y.linear_em\n"; }
v.position_y.linear_em=old;
}
{
auto old=v.position_y.linear_rem;
v.position_y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_y.linear_rem\n"; }
v.position_y.linear_rem=old;
}
{
auto old=v.position_y.linear_vw;
v.position_y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_y.linear_vw\n"; }
v.position_y.linear_vw=old;
}
{
auto old=v.position_y.linear_vh;
v.position_y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position_y.linear_vh\n"; }
v.position_y.linear_vh=old;
}
{
auto old=v.size_kind;
v.size_kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_kind\n"; }
v.size_kind=old;
}
{
auto old=v.size_x.value;
v.size_x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_x.value\n"; }
v.size_x.value=old;
}
{
auto old=v.size_x.unit;
v.size_x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_x.unit\n"; }
v.size_x.unit=old;
}
{
auto old=v.size_x.linear_percent;
v.size_x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_x.linear_percent\n"; }
v.size_x.linear_percent=old;
}
{
auto old=v.size_x.linear_em;
v.size_x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_x.linear_em\n"; }
v.size_x.linear_em=old;
}
{
auto old=v.size_x.linear_rem;
v.size_x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_x.linear_rem\n"; }
v.size_x.linear_rem=old;
}
{
auto old=v.size_x.linear_vw;
v.size_x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_x.linear_vw\n"; }
v.size_x.linear_vw=old;
}
{
auto old=v.size_x.linear_vh;
v.size_x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_x.linear_vh\n"; }
v.size_x.linear_vh=old;
}
{
auto old=v.size_y.value;
v.size_y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_y.value\n"; }
v.size_y.value=old;
}
{
auto old=v.size_y.unit;
v.size_y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_y.unit\n"; }
v.size_y.unit=old;
}
{
auto old=v.size_y.linear_percent;
v.size_y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_y.linear_percent\n"; }
v.size_y.linear_percent=old;
}
{
auto old=v.size_y.linear_em;
v.size_y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_y.linear_em\n"; }
v.size_y.linear_em=old;
}
{
auto old=v.size_y.linear_rem;
v.size_y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_y.linear_rem\n"; }
v.size_y.linear_rem=old;
}
{
auto old=v.size_y.linear_vw;
v.size_y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_y.linear_vw\n"; }
v.size_y.linear_vw=old;
}
{
auto old=v.size_y.linear_vh;
v.size_y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.size_y.linear_vh\n"; }
v.size_y.linear_vh=old;
}
{
auto old=v.repeat_x;
v.repeat_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.repeat_x\n"; }
v.repeat_x=old;
}
{
auto old=v.repeat_y;
v.repeat_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.repeat_y\n"; }
v.repeat_y=old;
}
}
{
aether_background_layer_list v{};
aether_background_layer storage5[1]{}; v.items=storage5;
aether_gradient_stop storage6[1]{}; v.items[0].gradient.stops=storage6;
v.items[0].gradient.stop_count=1;
char storage7[]="a"; v.items[0].image_source=storage7;
v.count=1;
const auto original=dump(v);
{
auto old=v.items[0].kind;
v.items[0].kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].kind\n"; }
v.items[0].kind=old;
}
{
auto old=v.items[0].gradient.direction_x;
v.items[0].gradient.direction_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.direction_x\n"; }
v.items[0].gradient.direction_x=old;
}
{
auto old=v.items[0].gradient.direction_y;
v.items[0].gradient.direction_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.direction_y\n"; }
v.items[0].gradient.direction_y=old;
}
{
auto old=v.items[0].gradient.stops[0].color.r;
v.items[0].gradient.stops[0].color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].color.r\n"; }
v.items[0].gradient.stops[0].color.r=old;
}
{
auto old=v.items[0].gradient.stops[0].color.g;
v.items[0].gradient.stops[0].color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].color.g\n"; }
v.items[0].gradient.stops[0].color.g=old;
}
{
auto old=v.items[0].gradient.stops[0].color.b;
v.items[0].gradient.stops[0].color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].color.b\n"; }
v.items[0].gradient.stops[0].color.b=old;
}
{
auto old=v.items[0].gradient.stops[0].color.a;
v.items[0].gradient.stops[0].color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].color.a\n"; }
v.items[0].gradient.stops[0].color.a=old;
}
{
auto old=v.items[0].gradient.stops[0].position.value;
v.items[0].gradient.stops[0].position.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].position.value\n"; }
v.items[0].gradient.stops[0].position.value=old;
}
{
auto old=v.items[0].gradient.stops[0].position.unit;
v.items[0].gradient.stops[0].position.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].position.unit\n"; }
v.items[0].gradient.stops[0].position.unit=old;
}
{
auto old=v.items[0].gradient.stops[0].position.linear_percent;
v.items[0].gradient.stops[0].position.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].position.linear_percent\n"; }
v.items[0].gradient.stops[0].position.linear_percent=old;
}
{
auto old=v.items[0].gradient.stops[0].position.linear_em;
v.items[0].gradient.stops[0].position.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].position.linear_em\n"; }
v.items[0].gradient.stops[0].position.linear_em=old;
}
{
auto old=v.items[0].gradient.stops[0].position.linear_rem;
v.items[0].gradient.stops[0].position.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].position.linear_rem\n"; }
v.items[0].gradient.stops[0].position.linear_rem=old;
}
{
auto old=v.items[0].gradient.stops[0].position.linear_vw;
v.items[0].gradient.stops[0].position.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].position.linear_vw\n"; }
v.items[0].gradient.stops[0].position.linear_vw=old;
}
{
auto old=v.items[0].gradient.stops[0].position.linear_vh;
v.items[0].gradient.stops[0].position.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].position.linear_vh\n"; }
v.items[0].gradient.stops[0].position.linear_vh=old;
}
{
auto old=v.items[0].gradient.stops[0].offset;
v.items[0].gradient.stops[0].offset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].offset\n"; }
v.items[0].gradient.stops[0].offset=old;
}
{
auto old=v.items[0].gradient.stops[0].has_position;
v.items[0].gradient.stops[0].has_position=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stops[0].has_position\n"; }
v.items[0].gradient.stops[0].has_position=old;
}
{
auto old=v.items[0].gradient.stop_count;
v.items[0].gradient.stop_count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].gradient.stop_count\n"; }
v.items[0].gradient.stop_count=old;
}
{
auto old=v.items[0].image_source[0];
v.items[0].image_source[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].image_source[0]\n"; }
v.items[0].image_source[0]=old;
}
{
auto old=v.items[0].position_x.value;
v.items[0].position_x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_x.value\n"; }
v.items[0].position_x.value=old;
}
{
auto old=v.items[0].position_x.unit;
v.items[0].position_x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_x.unit\n"; }
v.items[0].position_x.unit=old;
}
{
auto old=v.items[0].position_x.linear_percent;
v.items[0].position_x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_x.linear_percent\n"; }
v.items[0].position_x.linear_percent=old;
}
{
auto old=v.items[0].position_x.linear_em;
v.items[0].position_x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_x.linear_em\n"; }
v.items[0].position_x.linear_em=old;
}
{
auto old=v.items[0].position_x.linear_rem;
v.items[0].position_x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_x.linear_rem\n"; }
v.items[0].position_x.linear_rem=old;
}
{
auto old=v.items[0].position_x.linear_vw;
v.items[0].position_x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_x.linear_vw\n"; }
v.items[0].position_x.linear_vw=old;
}
{
auto old=v.items[0].position_x.linear_vh;
v.items[0].position_x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_x.linear_vh\n"; }
v.items[0].position_x.linear_vh=old;
}
{
auto old=v.items[0].position_y.value;
v.items[0].position_y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_y.value\n"; }
v.items[0].position_y.value=old;
}
{
auto old=v.items[0].position_y.unit;
v.items[0].position_y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_y.unit\n"; }
v.items[0].position_y.unit=old;
}
{
auto old=v.items[0].position_y.linear_percent;
v.items[0].position_y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_y.linear_percent\n"; }
v.items[0].position_y.linear_percent=old;
}
{
auto old=v.items[0].position_y.linear_em;
v.items[0].position_y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_y.linear_em\n"; }
v.items[0].position_y.linear_em=old;
}
{
auto old=v.items[0].position_y.linear_rem;
v.items[0].position_y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_y.linear_rem\n"; }
v.items[0].position_y.linear_rem=old;
}
{
auto old=v.items[0].position_y.linear_vw;
v.items[0].position_y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_y.linear_vw\n"; }
v.items[0].position_y.linear_vw=old;
}
{
auto old=v.items[0].position_y.linear_vh;
v.items[0].position_y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].position_y.linear_vh\n"; }
v.items[0].position_y.linear_vh=old;
}
{
auto old=v.items[0].size_kind;
v.items[0].size_kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_kind\n"; }
v.items[0].size_kind=old;
}
{
auto old=v.items[0].size_x.value;
v.items[0].size_x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_x.value\n"; }
v.items[0].size_x.value=old;
}
{
auto old=v.items[0].size_x.unit;
v.items[0].size_x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_x.unit\n"; }
v.items[0].size_x.unit=old;
}
{
auto old=v.items[0].size_x.linear_percent;
v.items[0].size_x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_x.linear_percent\n"; }
v.items[0].size_x.linear_percent=old;
}
{
auto old=v.items[0].size_x.linear_em;
v.items[0].size_x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_x.linear_em\n"; }
v.items[0].size_x.linear_em=old;
}
{
auto old=v.items[0].size_x.linear_rem;
v.items[0].size_x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_x.linear_rem\n"; }
v.items[0].size_x.linear_rem=old;
}
{
auto old=v.items[0].size_x.linear_vw;
v.items[0].size_x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_x.linear_vw\n"; }
v.items[0].size_x.linear_vw=old;
}
{
auto old=v.items[0].size_x.linear_vh;
v.items[0].size_x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_x.linear_vh\n"; }
v.items[0].size_x.linear_vh=old;
}
{
auto old=v.items[0].size_y.value;
v.items[0].size_y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_y.value\n"; }
v.items[0].size_y.value=old;
}
{
auto old=v.items[0].size_y.unit;
v.items[0].size_y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_y.unit\n"; }
v.items[0].size_y.unit=old;
}
{
auto old=v.items[0].size_y.linear_percent;
v.items[0].size_y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_y.linear_percent\n"; }
v.items[0].size_y.linear_percent=old;
}
{
auto old=v.items[0].size_y.linear_em;
v.items[0].size_y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_y.linear_em\n"; }
v.items[0].size_y.linear_em=old;
}
{
auto old=v.items[0].size_y.linear_rem;
v.items[0].size_y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_y.linear_rem\n"; }
v.items[0].size_y.linear_rem=old;
}
{
auto old=v.items[0].size_y.linear_vw;
v.items[0].size_y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_y.linear_vw\n"; }
v.items[0].size_y.linear_vw=old;
}
{
auto old=v.items[0].size_y.linear_vh;
v.items[0].size_y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].size_y.linear_vh\n"; }
v.items[0].size_y.linear_vh=old;
}
{
auto old=v.items[0].repeat_x;
v.items[0].repeat_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].repeat_x\n"; }
v.items[0].repeat_x=old;
}
{
auto old=v.items[0].repeat_y;
v.items[0].repeat_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].repeat_y\n"; }
v.items[0].repeat_y=old;
}
{
auto old=v.count;
v.count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.count\n"; }
v.count=old;
}
}
{
aether_backdrop_filter v{};
const auto original=dump(v);
{
auto old=v.kind;
v.kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.kind\n"; }
v.kind=old;
}
{
auto old=v.length_amount.value;
v.length_amount.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length_amount.value\n"; }
v.length_amount.value=old;
}
{
auto old=v.length_amount.unit;
v.length_amount.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length_amount.unit\n"; }
v.length_amount.unit=old;
}
{
auto old=v.length_amount.linear_percent;
v.length_amount.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length_amount.linear_percent\n"; }
v.length_amount.linear_percent=old;
}
{
auto old=v.length_amount.linear_em;
v.length_amount.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length_amount.linear_em\n"; }
v.length_amount.linear_em=old;
}
{
auto old=v.length_amount.linear_rem;
v.length_amount.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length_amount.linear_rem\n"; }
v.length_amount.linear_rem=old;
}
{
auto old=v.length_amount.linear_vw;
v.length_amount.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length_amount.linear_vw\n"; }
v.length_amount.linear_vw=old;
}
{
auto old=v.length_amount.linear_vh;
v.length_amount.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.length_amount.linear_vh\n"; }
v.length_amount.linear_vh=old;
}
{
auto old=v.scalar_amount;
v.scalar_amount=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.scalar_amount\n"; }
v.scalar_amount=old;
}
}
{
aether_backdrop_filter_list v{};
aether_backdrop_filter storage8[1]{}; v.items=storage8;
v.count=1;
const auto original=dump(v);
{
auto old=v.items[0].kind;
v.items[0].kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].kind\n"; }
v.items[0].kind=old;
}
{
auto old=v.items[0].length_amount.value;
v.items[0].length_amount.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].length_amount.value\n"; }
v.items[0].length_amount.value=old;
}
{
auto old=v.items[0].length_amount.unit;
v.items[0].length_amount.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].length_amount.unit\n"; }
v.items[0].length_amount.unit=old;
}
{
auto old=v.items[0].length_amount.linear_percent;
v.items[0].length_amount.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].length_amount.linear_percent\n"; }
v.items[0].length_amount.linear_percent=old;
}
{
auto old=v.items[0].length_amount.linear_em;
v.items[0].length_amount.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].length_amount.linear_em\n"; }
v.items[0].length_amount.linear_em=old;
}
{
auto old=v.items[0].length_amount.linear_rem;
v.items[0].length_amount.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].length_amount.linear_rem\n"; }
v.items[0].length_amount.linear_rem=old;
}
{
auto old=v.items[0].length_amount.linear_vw;
v.items[0].length_amount.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].length_amount.linear_vw\n"; }
v.items[0].length_amount.linear_vw=old;
}
{
auto old=v.items[0].length_amount.linear_vh;
v.items[0].length_amount.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].length_amount.linear_vh\n"; }
v.items[0].length_amount.linear_vh=old;
}
{
auto old=v.items[0].scalar_amount;
v.items[0].scalar_amount=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].scalar_amount\n"; }
v.items[0].scalar_amount=old;
}
{
auto old=v.count;
v.count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.count\n"; }
v.count=old;
}
}
{
aether_transform_operation v{};
const auto original=dump(v);
{
auto old=v.kind;
v.kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.kind\n"; }
v.kind=old;
}
{
auto old=v.x.value;
v.x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.value\n"; }
v.x.value=old;
}
{
auto old=v.x.unit;
v.x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.unit\n"; }
v.x.unit=old;
}
{
auto old=v.x.linear_percent;
v.x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.linear_percent\n"; }
v.x.linear_percent=old;
}
{
auto old=v.x.linear_em;
v.x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.linear_em\n"; }
v.x.linear_em=old;
}
{
auto old=v.x.linear_rem;
v.x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.linear_rem\n"; }
v.x.linear_rem=old;
}
{
auto old=v.x.linear_vw;
v.x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.linear_vw\n"; }
v.x.linear_vw=old;
}
{
auto old=v.x.linear_vh;
v.x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.linear_vh\n"; }
v.x.linear_vh=old;
}
{
auto old=v.y.value;
v.y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.value\n"; }
v.y.value=old;
}
{
auto old=v.y.unit;
v.y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.unit\n"; }
v.y.unit=old;
}
{
auto old=v.y.linear_percent;
v.y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.linear_percent\n"; }
v.y.linear_percent=old;
}
{
auto old=v.y.linear_em;
v.y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.linear_em\n"; }
v.y.linear_em=old;
}
{
auto old=v.y.linear_rem;
v.y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.linear_rem\n"; }
v.y.linear_rem=old;
}
{
auto old=v.y.linear_vw;
v.y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.linear_vw\n"; }
v.y.linear_vw=old;
}
{
auto old=v.y.linear_vh;
v.y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.linear_vh\n"; }
v.y.linear_vh=old;
}
{
auto old=v.scalar_x;
v.scalar_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.scalar_x\n"; }
v.scalar_x=old;
}
{
auto old=v.scalar_y;
v.scalar_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.scalar_y\n"; }
v.scalar_y=old;
}
{
auto old=v.angle_radians;
v.angle_radians=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.angle_radians\n"; }
v.angle_radians=old;
}
{
auto old=v.matrix_a;
v.matrix_a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.matrix_a\n"; }
v.matrix_a=old;
}
{
auto old=v.matrix_b;
v.matrix_b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.matrix_b\n"; }
v.matrix_b=old;
}
{
auto old=v.matrix_c;
v.matrix_c=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.matrix_c\n"; }
v.matrix_c=old;
}
{
auto old=v.matrix_d;
v.matrix_d=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.matrix_d\n"; }
v.matrix_d=old;
}
{
auto old=v.matrix_e;
v.matrix_e=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.matrix_e\n"; }
v.matrix_e=old;
}
{
auto old=v.matrix_f;
v.matrix_f=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.matrix_f\n"; }
v.matrix_f=old;
}
}
{
aether_transform_list v{};
aether_transform_operation storage9[1]{}; v.items=storage9;
v.count=1;
const auto original=dump(v);
{
auto old=v.items[0].kind;
v.items[0].kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].kind\n"; }
v.items[0].kind=old;
}
{
auto old=v.items[0].x.value;
v.items[0].x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].x.value\n"; }
v.items[0].x.value=old;
}
{
auto old=v.items[0].x.unit;
v.items[0].x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].x.unit\n"; }
v.items[0].x.unit=old;
}
{
auto old=v.items[0].x.linear_percent;
v.items[0].x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].x.linear_percent\n"; }
v.items[0].x.linear_percent=old;
}
{
auto old=v.items[0].x.linear_em;
v.items[0].x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].x.linear_em\n"; }
v.items[0].x.linear_em=old;
}
{
auto old=v.items[0].x.linear_rem;
v.items[0].x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].x.linear_rem\n"; }
v.items[0].x.linear_rem=old;
}
{
auto old=v.items[0].x.linear_vw;
v.items[0].x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].x.linear_vw\n"; }
v.items[0].x.linear_vw=old;
}
{
auto old=v.items[0].x.linear_vh;
v.items[0].x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].x.linear_vh\n"; }
v.items[0].x.linear_vh=old;
}
{
auto old=v.items[0].y.value;
v.items[0].y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].y.value\n"; }
v.items[0].y.value=old;
}
{
auto old=v.items[0].y.unit;
v.items[0].y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].y.unit\n"; }
v.items[0].y.unit=old;
}
{
auto old=v.items[0].y.linear_percent;
v.items[0].y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].y.linear_percent\n"; }
v.items[0].y.linear_percent=old;
}
{
auto old=v.items[0].y.linear_em;
v.items[0].y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].y.linear_em\n"; }
v.items[0].y.linear_em=old;
}
{
auto old=v.items[0].y.linear_rem;
v.items[0].y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].y.linear_rem\n"; }
v.items[0].y.linear_rem=old;
}
{
auto old=v.items[0].y.linear_vw;
v.items[0].y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].y.linear_vw\n"; }
v.items[0].y.linear_vw=old;
}
{
auto old=v.items[0].y.linear_vh;
v.items[0].y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].y.linear_vh\n"; }
v.items[0].y.linear_vh=old;
}
{
auto old=v.items[0].scalar_x;
v.items[0].scalar_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].scalar_x\n"; }
v.items[0].scalar_x=old;
}
{
auto old=v.items[0].scalar_y;
v.items[0].scalar_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].scalar_y\n"; }
v.items[0].scalar_y=old;
}
{
auto old=v.items[0].angle_radians;
v.items[0].angle_radians=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].angle_radians\n"; }
v.items[0].angle_radians=old;
}
{
auto old=v.items[0].matrix_a;
v.items[0].matrix_a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].matrix_a\n"; }
v.items[0].matrix_a=old;
}
{
auto old=v.items[0].matrix_b;
v.items[0].matrix_b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].matrix_b\n"; }
v.items[0].matrix_b=old;
}
{
auto old=v.items[0].matrix_c;
v.items[0].matrix_c=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].matrix_c\n"; }
v.items[0].matrix_c=old;
}
{
auto old=v.items[0].matrix_d;
v.items[0].matrix_d=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].matrix_d\n"; }
v.items[0].matrix_d=old;
}
{
auto old=v.items[0].matrix_e;
v.items[0].matrix_e=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].matrix_e\n"; }
v.items[0].matrix_e=old;
}
{
auto old=v.items[0].matrix_f;
v.items[0].matrix_f=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.items[0].matrix_f\n"; }
v.items[0].matrix_f=old;
}
{
auto old=v.count;
v.count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.count\n"; }
v.count=old;
}
}
{
aether_transform_origin v{};
const auto original=dump(v);
{
auto old=v.x.value;
v.x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.value\n"; }
v.x.value=old;
}
{
auto old=v.x.unit;
v.x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.unit\n"; }
v.x.unit=old;
}
{
auto old=v.x.linear_percent;
v.x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.linear_percent\n"; }
v.x.linear_percent=old;
}
{
auto old=v.x.linear_em;
v.x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.linear_em\n"; }
v.x.linear_em=old;
}
{
auto old=v.x.linear_rem;
v.x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.linear_rem\n"; }
v.x.linear_rem=old;
}
{
auto old=v.x.linear_vw;
v.x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.linear_vw\n"; }
v.x.linear_vw=old;
}
{
auto old=v.x.linear_vh;
v.x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.x.linear_vh\n"; }
v.x.linear_vh=old;
}
{
auto old=v.y.value;
v.y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.value\n"; }
v.y.value=old;
}
{
auto old=v.y.unit;
v.y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.unit\n"; }
v.y.unit=old;
}
{
auto old=v.y.linear_percent;
v.y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.linear_percent\n"; }
v.y.linear_percent=old;
}
{
auto old=v.y.linear_em;
v.y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.linear_em\n"; }
v.y.linear_em=old;
}
{
auto old=v.y.linear_rem;
v.y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.linear_rem\n"; }
v.y.linear_rem=old;
}
{
auto old=v.y.linear_vw;
v.y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.linear_vw\n"; }
v.y.linear_vw=old;
}
{
auto old=v.y.linear_vh;
v.y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.y.linear_vh\n"; }
v.y.linear_vh=old;
}
}
{
aether_transform_matrix v{};
const auto original=dump(v);
{
auto old=v.m11;
v.m11=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.m11\n"; }
v.m11=old;
}
{
auto old=v.m12;
v.m12=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.m12\n"; }
v.m12=old;
}
{
auto old=v.m21;
v.m21=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.m21\n"; }
v.m21=old;
}
{
auto old=v.m22;
v.m22=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.m22\n"; }
v.m22=old;
}
{
auto old=v.dx;
v.dx=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.dx\n"; }
v.dx=old;
}
{
auto old=v.dy;
v.dy=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.dy\n"; }
v.dy=old;
}
}
{
aether_computed_style_t v{};
aether_background_layer storage10[1]{}; v.background_layers.items=storage10;
aether_gradient_stop storage11[1]{}; v.background_layers.items[0].gradient.stops=storage11;
v.background_layers.items[0].gradient.stop_count=1;
char storage12[]="a"; v.background_layers.items[0].image_source=storage12;
v.background_layers.count=1;
aether_backdrop_filter storage13[1]{}; v.backdrop_filters.items=storage13;
v.backdrop_filters.count=1;
aether_gradient_stop storage14[1]{}; v.text_gradient.stops=storage14;
v.text_gradient.stop_count=1;
aether_transform_operation storage15[1]{}; v.transforms.items=storage15;
v.transforms.count=1;
aether_shadow storage16[1]{}; v.text_shadows.items=storage16;
v.text_shadows.count=1;
aether_shadow storage17[1]{}; v.box_shadows.items=storage17;
v.box_shadows.count=1;
const auto original=dump(v);
{
auto old=v.display;
v.display=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.display\n"; }
v.display=old;
}
{
auto old=v.flex_direction;
v.flex_direction=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_direction\n"; }
v.flex_direction=old;
}
{
auto old=v.justify_content;
v.justify_content=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.justify_content\n"; }
v.justify_content=old;
}
{
auto old=v.align_items;
v.align_items=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.align_items\n"; }
v.align_items=old;
}
{
auto old=v.align_self;
v.align_self=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.align_self\n"; }
v.align_self=old;
}
{
auto old=v.flex_grow;
v.flex_grow=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_grow\n"; }
v.flex_grow=old;
}
{
auto old=v.flex_shrink;
v.flex_shrink=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_shrink\n"; }
v.flex_shrink=old;
}
{
auto old=v.flex_basis.kind;
v.flex_basis.kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_basis.kind\n"; }
v.flex_basis.kind=old;
}
{
auto old=v.flex_basis.length.value;
v.flex_basis.length.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_basis.length.value\n"; }
v.flex_basis.length.value=old;
}
{
auto old=v.flex_basis.length.unit;
v.flex_basis.length.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_basis.length.unit\n"; }
v.flex_basis.length.unit=old;
}
{
auto old=v.flex_basis.length.linear_percent;
v.flex_basis.length.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_basis.length.linear_percent\n"; }
v.flex_basis.length.linear_percent=old;
}
{
auto old=v.flex_basis.length.linear_em;
v.flex_basis.length.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_basis.length.linear_em\n"; }
v.flex_basis.length.linear_em=old;
}
{
auto old=v.flex_basis.length.linear_rem;
v.flex_basis.length.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_basis.length.linear_rem\n"; }
v.flex_basis.length.linear_rem=old;
}
{
auto old=v.flex_basis.length.linear_vw;
v.flex_basis.length.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_basis.length.linear_vw\n"; }
v.flex_basis.length.linear_vw=old;
}
{
auto old=v.flex_basis.length.linear_vh;
v.flex_basis.length.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_basis.length.linear_vh\n"; }
v.flex_basis.length.linear_vh=old;
}
{
auto old=v.flex_wrap;
v.flex_wrap=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.flex_wrap\n"; }
v.flex_wrap=old;
}
{
auto old=v.gap;
v.gap=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.gap\n"; }
v.gap=old;
}
{
auto old=v.position;
v.position=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.position\n"; }
v.position=old;
}
{
auto old=v.inset.left.value;
v.inset.left.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.left.value\n"; }
v.inset.left.value=old;
}
{
auto old=v.inset.left.unit;
v.inset.left.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.left.unit\n"; }
v.inset.left.unit=old;
}
{
auto old=v.inset.left.linear_percent;
v.inset.left.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.left.linear_percent\n"; }
v.inset.left.linear_percent=old;
}
{
auto old=v.inset.left.linear_em;
v.inset.left.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.left.linear_em\n"; }
v.inset.left.linear_em=old;
}
{
auto old=v.inset.left.linear_rem;
v.inset.left.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.left.linear_rem\n"; }
v.inset.left.linear_rem=old;
}
{
auto old=v.inset.left.linear_vw;
v.inset.left.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.left.linear_vw\n"; }
v.inset.left.linear_vw=old;
}
{
auto old=v.inset.left.linear_vh;
v.inset.left.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.left.linear_vh\n"; }
v.inset.left.linear_vh=old;
}
{
auto old=v.inset.top.value;
v.inset.top.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.top.value\n"; }
v.inset.top.value=old;
}
{
auto old=v.inset.top.unit;
v.inset.top.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.top.unit\n"; }
v.inset.top.unit=old;
}
{
auto old=v.inset.top.linear_percent;
v.inset.top.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.top.linear_percent\n"; }
v.inset.top.linear_percent=old;
}
{
auto old=v.inset.top.linear_em;
v.inset.top.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.top.linear_em\n"; }
v.inset.top.linear_em=old;
}
{
auto old=v.inset.top.linear_rem;
v.inset.top.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.top.linear_rem\n"; }
v.inset.top.linear_rem=old;
}
{
auto old=v.inset.top.linear_vw;
v.inset.top.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.top.linear_vw\n"; }
v.inset.top.linear_vw=old;
}
{
auto old=v.inset.top.linear_vh;
v.inset.top.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.top.linear_vh\n"; }
v.inset.top.linear_vh=old;
}
{
auto old=v.inset.right.value;
v.inset.right.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.right.value\n"; }
v.inset.right.value=old;
}
{
auto old=v.inset.right.unit;
v.inset.right.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.right.unit\n"; }
v.inset.right.unit=old;
}
{
auto old=v.inset.right.linear_percent;
v.inset.right.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.right.linear_percent\n"; }
v.inset.right.linear_percent=old;
}
{
auto old=v.inset.right.linear_em;
v.inset.right.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.right.linear_em\n"; }
v.inset.right.linear_em=old;
}
{
auto old=v.inset.right.linear_rem;
v.inset.right.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.right.linear_rem\n"; }
v.inset.right.linear_rem=old;
}
{
auto old=v.inset.right.linear_vw;
v.inset.right.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.right.linear_vw\n"; }
v.inset.right.linear_vw=old;
}
{
auto old=v.inset.right.linear_vh;
v.inset.right.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.right.linear_vh\n"; }
v.inset.right.linear_vh=old;
}
{
auto old=v.inset.bottom.value;
v.inset.bottom.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.bottom.value\n"; }
v.inset.bottom.value=old;
}
{
auto old=v.inset.bottom.unit;
v.inset.bottom.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.bottom.unit\n"; }
v.inset.bottom.unit=old;
}
{
auto old=v.inset.bottom.linear_percent;
v.inset.bottom.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.bottom.linear_percent\n"; }
v.inset.bottom.linear_percent=old;
}
{
auto old=v.inset.bottom.linear_em;
v.inset.bottom.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.bottom.linear_em\n"; }
v.inset.bottom.linear_em=old;
}
{
auto old=v.inset.bottom.linear_rem;
v.inset.bottom.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.bottom.linear_rem\n"; }
v.inset.bottom.linear_rem=old;
}
{
auto old=v.inset.bottom.linear_vw;
v.inset.bottom.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.bottom.linear_vw\n"; }
v.inset.bottom.linear_vw=old;
}
{
auto old=v.inset.bottom.linear_vh;
v.inset.bottom.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.inset.bottom.linear_vh\n"; }
v.inset.bottom.linear_vh=old;
}
{
auto old=v.width.value;
v.width.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.width.value\n"; }
v.width.value=old;
}
{
auto old=v.width.unit;
v.width.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.width.unit\n"; }
v.width.unit=old;
}
{
auto old=v.width.linear_percent;
v.width.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.width.linear_percent\n"; }
v.width.linear_percent=old;
}
{
auto old=v.width.linear_em;
v.width.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.width.linear_em\n"; }
v.width.linear_em=old;
}
{
auto old=v.width.linear_rem;
v.width.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.width.linear_rem\n"; }
v.width.linear_rem=old;
}
{
auto old=v.width.linear_vw;
v.width.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.width.linear_vw\n"; }
v.width.linear_vw=old;
}
{
auto old=v.width.linear_vh;
v.width.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.width.linear_vh\n"; }
v.width.linear_vh=old;
}
{
auto old=v.height.value;
v.height.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.height.value\n"; }
v.height.value=old;
}
{
auto old=v.height.unit;
v.height.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.height.unit\n"; }
v.height.unit=old;
}
{
auto old=v.height.linear_percent;
v.height.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.height.linear_percent\n"; }
v.height.linear_percent=old;
}
{
auto old=v.height.linear_em;
v.height.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.height.linear_em\n"; }
v.height.linear_em=old;
}
{
auto old=v.height.linear_rem;
v.height.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.height.linear_rem\n"; }
v.height.linear_rem=old;
}
{
auto old=v.height.linear_vw;
v.height.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.height.linear_vw\n"; }
v.height.linear_vw=old;
}
{
auto old=v.height.linear_vh;
v.height.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.height.linear_vh\n"; }
v.height.linear_vh=old;
}
{
auto old=v.min_width.value;
v.min_width.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_width.value\n"; }
v.min_width.value=old;
}
{
auto old=v.min_width.unit;
v.min_width.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_width.unit\n"; }
v.min_width.unit=old;
}
{
auto old=v.min_width.linear_percent;
v.min_width.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_width.linear_percent\n"; }
v.min_width.linear_percent=old;
}
{
auto old=v.min_width.linear_em;
v.min_width.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_width.linear_em\n"; }
v.min_width.linear_em=old;
}
{
auto old=v.min_width.linear_rem;
v.min_width.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_width.linear_rem\n"; }
v.min_width.linear_rem=old;
}
{
auto old=v.min_width.linear_vw;
v.min_width.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_width.linear_vw\n"; }
v.min_width.linear_vw=old;
}
{
auto old=v.min_width.linear_vh;
v.min_width.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_width.linear_vh\n"; }
v.min_width.linear_vh=old;
}
{
auto old=v.min_height.value;
v.min_height.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_height.value\n"; }
v.min_height.value=old;
}
{
auto old=v.min_height.unit;
v.min_height.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_height.unit\n"; }
v.min_height.unit=old;
}
{
auto old=v.min_height.linear_percent;
v.min_height.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_height.linear_percent\n"; }
v.min_height.linear_percent=old;
}
{
auto old=v.min_height.linear_em;
v.min_height.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_height.linear_em\n"; }
v.min_height.linear_em=old;
}
{
auto old=v.min_height.linear_rem;
v.min_height.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_height.linear_rem\n"; }
v.min_height.linear_rem=old;
}
{
auto old=v.min_height.linear_vw;
v.min_height.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_height.linear_vw\n"; }
v.min_height.linear_vw=old;
}
{
auto old=v.min_height.linear_vh;
v.min_height.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.min_height.linear_vh\n"; }
v.min_height.linear_vh=old;
}
{
auto old=v.max_width.value;
v.max_width.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_width.value\n"; }
v.max_width.value=old;
}
{
auto old=v.max_width.unit;
v.max_width.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_width.unit\n"; }
v.max_width.unit=old;
}
{
auto old=v.max_width.linear_percent;
v.max_width.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_width.linear_percent\n"; }
v.max_width.linear_percent=old;
}
{
auto old=v.max_width.linear_em;
v.max_width.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_width.linear_em\n"; }
v.max_width.linear_em=old;
}
{
auto old=v.max_width.linear_rem;
v.max_width.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_width.linear_rem\n"; }
v.max_width.linear_rem=old;
}
{
auto old=v.max_width.linear_vw;
v.max_width.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_width.linear_vw\n"; }
v.max_width.linear_vw=old;
}
{
auto old=v.max_width.linear_vh;
v.max_width.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_width.linear_vh\n"; }
v.max_width.linear_vh=old;
}
{
auto old=v.max_height.value;
v.max_height.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_height.value\n"; }
v.max_height.value=old;
}
{
auto old=v.max_height.unit;
v.max_height.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_height.unit\n"; }
v.max_height.unit=old;
}
{
auto old=v.max_height.linear_percent;
v.max_height.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_height.linear_percent\n"; }
v.max_height.linear_percent=old;
}
{
auto old=v.max_height.linear_em;
v.max_height.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_height.linear_em\n"; }
v.max_height.linear_em=old;
}
{
auto old=v.max_height.linear_rem;
v.max_height.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_height.linear_rem\n"; }
v.max_height.linear_rem=old;
}
{
auto old=v.max_height.linear_vw;
v.max_height.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_height.linear_vw\n"; }
v.max_height.linear_vw=old;
}
{
auto old=v.max_height.linear_vh;
v.max_height.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.max_height.linear_vh\n"; }
v.max_height.linear_vh=old;
}
{
auto old=v.margin.left.value;
v.margin.left.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.left.value\n"; }
v.margin.left.value=old;
}
{
auto old=v.margin.left.unit;
v.margin.left.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.left.unit\n"; }
v.margin.left.unit=old;
}
{
auto old=v.margin.left.linear_percent;
v.margin.left.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.left.linear_percent\n"; }
v.margin.left.linear_percent=old;
}
{
auto old=v.margin.left.linear_em;
v.margin.left.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.left.linear_em\n"; }
v.margin.left.linear_em=old;
}
{
auto old=v.margin.left.linear_rem;
v.margin.left.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.left.linear_rem\n"; }
v.margin.left.linear_rem=old;
}
{
auto old=v.margin.left.linear_vw;
v.margin.left.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.left.linear_vw\n"; }
v.margin.left.linear_vw=old;
}
{
auto old=v.margin.left.linear_vh;
v.margin.left.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.left.linear_vh\n"; }
v.margin.left.linear_vh=old;
}
{
auto old=v.margin.top.value;
v.margin.top.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.top.value\n"; }
v.margin.top.value=old;
}
{
auto old=v.margin.top.unit;
v.margin.top.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.top.unit\n"; }
v.margin.top.unit=old;
}
{
auto old=v.margin.top.linear_percent;
v.margin.top.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.top.linear_percent\n"; }
v.margin.top.linear_percent=old;
}
{
auto old=v.margin.top.linear_em;
v.margin.top.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.top.linear_em\n"; }
v.margin.top.linear_em=old;
}
{
auto old=v.margin.top.linear_rem;
v.margin.top.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.top.linear_rem\n"; }
v.margin.top.linear_rem=old;
}
{
auto old=v.margin.top.linear_vw;
v.margin.top.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.top.linear_vw\n"; }
v.margin.top.linear_vw=old;
}
{
auto old=v.margin.top.linear_vh;
v.margin.top.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.top.linear_vh\n"; }
v.margin.top.linear_vh=old;
}
{
auto old=v.margin.right.value;
v.margin.right.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.right.value\n"; }
v.margin.right.value=old;
}
{
auto old=v.margin.right.unit;
v.margin.right.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.right.unit\n"; }
v.margin.right.unit=old;
}
{
auto old=v.margin.right.linear_percent;
v.margin.right.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.right.linear_percent\n"; }
v.margin.right.linear_percent=old;
}
{
auto old=v.margin.right.linear_em;
v.margin.right.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.right.linear_em\n"; }
v.margin.right.linear_em=old;
}
{
auto old=v.margin.right.linear_rem;
v.margin.right.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.right.linear_rem\n"; }
v.margin.right.linear_rem=old;
}
{
auto old=v.margin.right.linear_vw;
v.margin.right.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.right.linear_vw\n"; }
v.margin.right.linear_vw=old;
}
{
auto old=v.margin.right.linear_vh;
v.margin.right.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.right.linear_vh\n"; }
v.margin.right.linear_vh=old;
}
{
auto old=v.margin.bottom.value;
v.margin.bottom.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.bottom.value\n"; }
v.margin.bottom.value=old;
}
{
auto old=v.margin.bottom.unit;
v.margin.bottom.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.bottom.unit\n"; }
v.margin.bottom.unit=old;
}
{
auto old=v.margin.bottom.linear_percent;
v.margin.bottom.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.bottom.linear_percent\n"; }
v.margin.bottom.linear_percent=old;
}
{
auto old=v.margin.bottom.linear_em;
v.margin.bottom.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.bottom.linear_em\n"; }
v.margin.bottom.linear_em=old;
}
{
auto old=v.margin.bottom.linear_rem;
v.margin.bottom.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.bottom.linear_rem\n"; }
v.margin.bottom.linear_rem=old;
}
{
auto old=v.margin.bottom.linear_vw;
v.margin.bottom.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.bottom.linear_vw\n"; }
v.margin.bottom.linear_vw=old;
}
{
auto old=v.margin.bottom.linear_vh;
v.margin.bottom.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.margin.bottom.linear_vh\n"; }
v.margin.bottom.linear_vh=old;
}
{
auto old=v.padding.left.value;
v.padding.left.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.left.value\n"; }
v.padding.left.value=old;
}
{
auto old=v.padding.left.unit;
v.padding.left.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.left.unit\n"; }
v.padding.left.unit=old;
}
{
auto old=v.padding.left.linear_percent;
v.padding.left.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.left.linear_percent\n"; }
v.padding.left.linear_percent=old;
}
{
auto old=v.padding.left.linear_em;
v.padding.left.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.left.linear_em\n"; }
v.padding.left.linear_em=old;
}
{
auto old=v.padding.left.linear_rem;
v.padding.left.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.left.linear_rem\n"; }
v.padding.left.linear_rem=old;
}
{
auto old=v.padding.left.linear_vw;
v.padding.left.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.left.linear_vw\n"; }
v.padding.left.linear_vw=old;
}
{
auto old=v.padding.left.linear_vh;
v.padding.left.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.left.linear_vh\n"; }
v.padding.left.linear_vh=old;
}
{
auto old=v.padding.top.value;
v.padding.top.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.top.value\n"; }
v.padding.top.value=old;
}
{
auto old=v.padding.top.unit;
v.padding.top.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.top.unit\n"; }
v.padding.top.unit=old;
}
{
auto old=v.padding.top.linear_percent;
v.padding.top.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.top.linear_percent\n"; }
v.padding.top.linear_percent=old;
}
{
auto old=v.padding.top.linear_em;
v.padding.top.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.top.linear_em\n"; }
v.padding.top.linear_em=old;
}
{
auto old=v.padding.top.linear_rem;
v.padding.top.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.top.linear_rem\n"; }
v.padding.top.linear_rem=old;
}
{
auto old=v.padding.top.linear_vw;
v.padding.top.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.top.linear_vw\n"; }
v.padding.top.linear_vw=old;
}
{
auto old=v.padding.top.linear_vh;
v.padding.top.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.top.linear_vh\n"; }
v.padding.top.linear_vh=old;
}
{
auto old=v.padding.right.value;
v.padding.right.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.right.value\n"; }
v.padding.right.value=old;
}
{
auto old=v.padding.right.unit;
v.padding.right.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.right.unit\n"; }
v.padding.right.unit=old;
}
{
auto old=v.padding.right.linear_percent;
v.padding.right.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.right.linear_percent\n"; }
v.padding.right.linear_percent=old;
}
{
auto old=v.padding.right.linear_em;
v.padding.right.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.right.linear_em\n"; }
v.padding.right.linear_em=old;
}
{
auto old=v.padding.right.linear_rem;
v.padding.right.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.right.linear_rem\n"; }
v.padding.right.linear_rem=old;
}
{
auto old=v.padding.right.linear_vw;
v.padding.right.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.right.linear_vw\n"; }
v.padding.right.linear_vw=old;
}
{
auto old=v.padding.right.linear_vh;
v.padding.right.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.right.linear_vh\n"; }
v.padding.right.linear_vh=old;
}
{
auto old=v.padding.bottom.value;
v.padding.bottom.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.bottom.value\n"; }
v.padding.bottom.value=old;
}
{
auto old=v.padding.bottom.unit;
v.padding.bottom.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.bottom.unit\n"; }
v.padding.bottom.unit=old;
}
{
auto old=v.padding.bottom.linear_percent;
v.padding.bottom.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.bottom.linear_percent\n"; }
v.padding.bottom.linear_percent=old;
}
{
auto old=v.padding.bottom.linear_em;
v.padding.bottom.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.bottom.linear_em\n"; }
v.padding.bottom.linear_em=old;
}
{
auto old=v.padding.bottom.linear_rem;
v.padding.bottom.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.bottom.linear_rem\n"; }
v.padding.bottom.linear_rem=old;
}
{
auto old=v.padding.bottom.linear_vw;
v.padding.bottom.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.bottom.linear_vw\n"; }
v.padding.bottom.linear_vw=old;
}
{
auto old=v.padding.bottom.linear_vh;
v.padding.bottom.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.padding.bottom.linear_vh\n"; }
v.padding.bottom.linear_vh=old;
}
{
auto old=v.border_width.left.value;
v.border_width.left.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.left.value\n"; }
v.border_width.left.value=old;
}
{
auto old=v.border_width.left.unit;
v.border_width.left.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.left.unit\n"; }
v.border_width.left.unit=old;
}
{
auto old=v.border_width.left.linear_percent;
v.border_width.left.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.left.linear_percent\n"; }
v.border_width.left.linear_percent=old;
}
{
auto old=v.border_width.left.linear_em;
v.border_width.left.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.left.linear_em\n"; }
v.border_width.left.linear_em=old;
}
{
auto old=v.border_width.left.linear_rem;
v.border_width.left.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.left.linear_rem\n"; }
v.border_width.left.linear_rem=old;
}
{
auto old=v.border_width.left.linear_vw;
v.border_width.left.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.left.linear_vw\n"; }
v.border_width.left.linear_vw=old;
}
{
auto old=v.border_width.left.linear_vh;
v.border_width.left.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.left.linear_vh\n"; }
v.border_width.left.linear_vh=old;
}
{
auto old=v.border_width.top.value;
v.border_width.top.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.top.value\n"; }
v.border_width.top.value=old;
}
{
auto old=v.border_width.top.unit;
v.border_width.top.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.top.unit\n"; }
v.border_width.top.unit=old;
}
{
auto old=v.border_width.top.linear_percent;
v.border_width.top.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.top.linear_percent\n"; }
v.border_width.top.linear_percent=old;
}
{
auto old=v.border_width.top.linear_em;
v.border_width.top.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.top.linear_em\n"; }
v.border_width.top.linear_em=old;
}
{
auto old=v.border_width.top.linear_rem;
v.border_width.top.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.top.linear_rem\n"; }
v.border_width.top.linear_rem=old;
}
{
auto old=v.border_width.top.linear_vw;
v.border_width.top.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.top.linear_vw\n"; }
v.border_width.top.linear_vw=old;
}
{
auto old=v.border_width.top.linear_vh;
v.border_width.top.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.top.linear_vh\n"; }
v.border_width.top.linear_vh=old;
}
{
auto old=v.border_width.right.value;
v.border_width.right.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.right.value\n"; }
v.border_width.right.value=old;
}
{
auto old=v.border_width.right.unit;
v.border_width.right.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.right.unit\n"; }
v.border_width.right.unit=old;
}
{
auto old=v.border_width.right.linear_percent;
v.border_width.right.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.right.linear_percent\n"; }
v.border_width.right.linear_percent=old;
}
{
auto old=v.border_width.right.linear_em;
v.border_width.right.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.right.linear_em\n"; }
v.border_width.right.linear_em=old;
}
{
auto old=v.border_width.right.linear_rem;
v.border_width.right.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.right.linear_rem\n"; }
v.border_width.right.linear_rem=old;
}
{
auto old=v.border_width.right.linear_vw;
v.border_width.right.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.right.linear_vw\n"; }
v.border_width.right.linear_vw=old;
}
{
auto old=v.border_width.right.linear_vh;
v.border_width.right.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.right.linear_vh\n"; }
v.border_width.right.linear_vh=old;
}
{
auto old=v.border_width.bottom.value;
v.border_width.bottom.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.bottom.value\n"; }
v.border_width.bottom.value=old;
}
{
auto old=v.border_width.bottom.unit;
v.border_width.bottom.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.bottom.unit\n"; }
v.border_width.bottom.unit=old;
}
{
auto old=v.border_width.bottom.linear_percent;
v.border_width.bottom.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.bottom.linear_percent\n"; }
v.border_width.bottom.linear_percent=old;
}
{
auto old=v.border_width.bottom.linear_em;
v.border_width.bottom.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.bottom.linear_em\n"; }
v.border_width.bottom.linear_em=old;
}
{
auto old=v.border_width.bottom.linear_rem;
v.border_width.bottom.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.bottom.linear_rem\n"; }
v.border_width.bottom.linear_rem=old;
}
{
auto old=v.border_width.bottom.linear_vw;
v.border_width.bottom.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.bottom.linear_vw\n"; }
v.border_width.bottom.linear_vw=old;
}
{
auto old=v.border_width.bottom.linear_vh;
v.border_width.bottom.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_width.bottom.linear_vh\n"; }
v.border_width.bottom.linear_vh=old;
}
{
auto old=v.box_sizing;
v.box_sizing=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_sizing\n"; }
v.box_sizing=old;
}
{
auto old=v.color.r;
v.color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.r\n"; }
v.color.r=old;
}
{
auto old=v.color.g;
v.color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.g\n"; }
v.color.g=old;
}
{
auto old=v.color.b;
v.color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.b\n"; }
v.color.b=old;
}
{
auto old=v.color.a;
v.color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.color.a\n"; }
v.color.a=old;
}
{
auto old=v.background_color.r;
v.background_color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_color.r\n"; }
v.background_color.r=old;
}
{
auto old=v.background_color.g;
v.background_color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_color.g\n"; }
v.background_color.g=old;
}
{
auto old=v.background_color.b;
v.background_color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_color.b\n"; }
v.background_color.b=old;
}
{
auto old=v.background_color.a;
v.background_color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_color.a\n"; }
v.background_color.a=old;
}
{
auto old=v.background_layers.items[0].kind;
v.background_layers.items[0].kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].kind\n"; }
v.background_layers.items[0].kind=old;
}
{
auto old=v.background_layers.items[0].gradient.direction_x;
v.background_layers.items[0].gradient.direction_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.direction_x\n"; }
v.background_layers.items[0].gradient.direction_x=old;
}
{
auto old=v.background_layers.items[0].gradient.direction_y;
v.background_layers.items[0].gradient.direction_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.direction_y\n"; }
v.background_layers.items[0].gradient.direction_y=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].color.r;
v.background_layers.items[0].gradient.stops[0].color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].color.r\n"; }
v.background_layers.items[0].gradient.stops[0].color.r=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].color.g;
v.background_layers.items[0].gradient.stops[0].color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].color.g\n"; }
v.background_layers.items[0].gradient.stops[0].color.g=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].color.b;
v.background_layers.items[0].gradient.stops[0].color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].color.b\n"; }
v.background_layers.items[0].gradient.stops[0].color.b=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].color.a;
v.background_layers.items[0].gradient.stops[0].color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].color.a\n"; }
v.background_layers.items[0].gradient.stops[0].color.a=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].position.value;
v.background_layers.items[0].gradient.stops[0].position.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].position.value\n"; }
v.background_layers.items[0].gradient.stops[0].position.value=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].position.unit;
v.background_layers.items[0].gradient.stops[0].position.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].position.unit\n"; }
v.background_layers.items[0].gradient.stops[0].position.unit=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].position.linear_percent;
v.background_layers.items[0].gradient.stops[0].position.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].position.linear_percent\n"; }
v.background_layers.items[0].gradient.stops[0].position.linear_percent=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].position.linear_em;
v.background_layers.items[0].gradient.stops[0].position.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].position.linear_em\n"; }
v.background_layers.items[0].gradient.stops[0].position.linear_em=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].position.linear_rem;
v.background_layers.items[0].gradient.stops[0].position.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].position.linear_rem\n"; }
v.background_layers.items[0].gradient.stops[0].position.linear_rem=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].position.linear_vw;
v.background_layers.items[0].gradient.stops[0].position.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].position.linear_vw\n"; }
v.background_layers.items[0].gradient.stops[0].position.linear_vw=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].position.linear_vh;
v.background_layers.items[0].gradient.stops[0].position.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].position.linear_vh\n"; }
v.background_layers.items[0].gradient.stops[0].position.linear_vh=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].offset;
v.background_layers.items[0].gradient.stops[0].offset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].offset\n"; }
v.background_layers.items[0].gradient.stops[0].offset=old;
}
{
auto old=v.background_layers.items[0].gradient.stops[0].has_position;
v.background_layers.items[0].gradient.stops[0].has_position=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stops[0].has_position\n"; }
v.background_layers.items[0].gradient.stops[0].has_position=old;
}
{
auto old=v.background_layers.items[0].gradient.stop_count;
v.background_layers.items[0].gradient.stop_count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].gradient.stop_count\n"; }
v.background_layers.items[0].gradient.stop_count=old;
}
{
auto old=v.background_layers.items[0].image_source[0];
v.background_layers.items[0].image_source[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].image_source[0]\n"; }
v.background_layers.items[0].image_source[0]=old;
}
{
auto old=v.background_layers.items[0].position_x.value;
v.background_layers.items[0].position_x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_x.value\n"; }
v.background_layers.items[0].position_x.value=old;
}
{
auto old=v.background_layers.items[0].position_x.unit;
v.background_layers.items[0].position_x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_x.unit\n"; }
v.background_layers.items[0].position_x.unit=old;
}
{
auto old=v.background_layers.items[0].position_x.linear_percent;
v.background_layers.items[0].position_x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_x.linear_percent\n"; }
v.background_layers.items[0].position_x.linear_percent=old;
}
{
auto old=v.background_layers.items[0].position_x.linear_em;
v.background_layers.items[0].position_x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_x.linear_em\n"; }
v.background_layers.items[0].position_x.linear_em=old;
}
{
auto old=v.background_layers.items[0].position_x.linear_rem;
v.background_layers.items[0].position_x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_x.linear_rem\n"; }
v.background_layers.items[0].position_x.linear_rem=old;
}
{
auto old=v.background_layers.items[0].position_x.linear_vw;
v.background_layers.items[0].position_x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_x.linear_vw\n"; }
v.background_layers.items[0].position_x.linear_vw=old;
}
{
auto old=v.background_layers.items[0].position_x.linear_vh;
v.background_layers.items[0].position_x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_x.linear_vh\n"; }
v.background_layers.items[0].position_x.linear_vh=old;
}
{
auto old=v.background_layers.items[0].position_y.value;
v.background_layers.items[0].position_y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_y.value\n"; }
v.background_layers.items[0].position_y.value=old;
}
{
auto old=v.background_layers.items[0].position_y.unit;
v.background_layers.items[0].position_y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_y.unit\n"; }
v.background_layers.items[0].position_y.unit=old;
}
{
auto old=v.background_layers.items[0].position_y.linear_percent;
v.background_layers.items[0].position_y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_y.linear_percent\n"; }
v.background_layers.items[0].position_y.linear_percent=old;
}
{
auto old=v.background_layers.items[0].position_y.linear_em;
v.background_layers.items[0].position_y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_y.linear_em\n"; }
v.background_layers.items[0].position_y.linear_em=old;
}
{
auto old=v.background_layers.items[0].position_y.linear_rem;
v.background_layers.items[0].position_y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_y.linear_rem\n"; }
v.background_layers.items[0].position_y.linear_rem=old;
}
{
auto old=v.background_layers.items[0].position_y.linear_vw;
v.background_layers.items[0].position_y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_y.linear_vw\n"; }
v.background_layers.items[0].position_y.linear_vw=old;
}
{
auto old=v.background_layers.items[0].position_y.linear_vh;
v.background_layers.items[0].position_y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].position_y.linear_vh\n"; }
v.background_layers.items[0].position_y.linear_vh=old;
}
{
auto old=v.background_layers.items[0].size_kind;
v.background_layers.items[0].size_kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_kind\n"; }
v.background_layers.items[0].size_kind=old;
}
{
auto old=v.background_layers.items[0].size_x.value;
v.background_layers.items[0].size_x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_x.value\n"; }
v.background_layers.items[0].size_x.value=old;
}
{
auto old=v.background_layers.items[0].size_x.unit;
v.background_layers.items[0].size_x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_x.unit\n"; }
v.background_layers.items[0].size_x.unit=old;
}
{
auto old=v.background_layers.items[0].size_x.linear_percent;
v.background_layers.items[0].size_x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_x.linear_percent\n"; }
v.background_layers.items[0].size_x.linear_percent=old;
}
{
auto old=v.background_layers.items[0].size_x.linear_em;
v.background_layers.items[0].size_x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_x.linear_em\n"; }
v.background_layers.items[0].size_x.linear_em=old;
}
{
auto old=v.background_layers.items[0].size_x.linear_rem;
v.background_layers.items[0].size_x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_x.linear_rem\n"; }
v.background_layers.items[0].size_x.linear_rem=old;
}
{
auto old=v.background_layers.items[0].size_x.linear_vw;
v.background_layers.items[0].size_x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_x.linear_vw\n"; }
v.background_layers.items[0].size_x.linear_vw=old;
}
{
auto old=v.background_layers.items[0].size_x.linear_vh;
v.background_layers.items[0].size_x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_x.linear_vh\n"; }
v.background_layers.items[0].size_x.linear_vh=old;
}
{
auto old=v.background_layers.items[0].size_y.value;
v.background_layers.items[0].size_y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_y.value\n"; }
v.background_layers.items[0].size_y.value=old;
}
{
auto old=v.background_layers.items[0].size_y.unit;
v.background_layers.items[0].size_y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_y.unit\n"; }
v.background_layers.items[0].size_y.unit=old;
}
{
auto old=v.background_layers.items[0].size_y.linear_percent;
v.background_layers.items[0].size_y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_y.linear_percent\n"; }
v.background_layers.items[0].size_y.linear_percent=old;
}
{
auto old=v.background_layers.items[0].size_y.linear_em;
v.background_layers.items[0].size_y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_y.linear_em\n"; }
v.background_layers.items[0].size_y.linear_em=old;
}
{
auto old=v.background_layers.items[0].size_y.linear_rem;
v.background_layers.items[0].size_y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_y.linear_rem\n"; }
v.background_layers.items[0].size_y.linear_rem=old;
}
{
auto old=v.background_layers.items[0].size_y.linear_vw;
v.background_layers.items[0].size_y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_y.linear_vw\n"; }
v.background_layers.items[0].size_y.linear_vw=old;
}
{
auto old=v.background_layers.items[0].size_y.linear_vh;
v.background_layers.items[0].size_y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].size_y.linear_vh\n"; }
v.background_layers.items[0].size_y.linear_vh=old;
}
{
auto old=v.background_layers.items[0].repeat_x;
v.background_layers.items[0].repeat_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].repeat_x\n"; }
v.background_layers.items[0].repeat_x=old;
}
{
auto old=v.background_layers.items[0].repeat_y;
v.background_layers.items[0].repeat_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.items[0].repeat_y\n"; }
v.background_layers.items[0].repeat_y=old;
}
{
auto old=v.background_layers.count;
v.background_layers.count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.background_layers.count\n"; }
v.background_layers.count=old;
}
{
auto old=v.backdrop_filters.items[0].kind;
v.backdrop_filters.items[0].kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.backdrop_filters.items[0].kind\n"; }
v.backdrop_filters.items[0].kind=old;
}
{
auto old=v.backdrop_filters.items[0].length_amount.value;
v.backdrop_filters.items[0].length_amount.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.backdrop_filters.items[0].length_amount.value\n"; }
v.backdrop_filters.items[0].length_amount.value=old;
}
{
auto old=v.backdrop_filters.items[0].length_amount.unit;
v.backdrop_filters.items[0].length_amount.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.backdrop_filters.items[0].length_amount.unit\n"; }
v.backdrop_filters.items[0].length_amount.unit=old;
}
{
auto old=v.backdrop_filters.items[0].length_amount.linear_percent;
v.backdrop_filters.items[0].length_amount.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.backdrop_filters.items[0].length_amount.linear_percent\n"; }
v.backdrop_filters.items[0].length_amount.linear_percent=old;
}
{
auto old=v.backdrop_filters.items[0].length_amount.linear_em;
v.backdrop_filters.items[0].length_amount.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.backdrop_filters.items[0].length_amount.linear_em\n"; }
v.backdrop_filters.items[0].length_amount.linear_em=old;
}
{
auto old=v.backdrop_filters.items[0].length_amount.linear_rem;
v.backdrop_filters.items[0].length_amount.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.backdrop_filters.items[0].length_amount.linear_rem\n"; }
v.backdrop_filters.items[0].length_amount.linear_rem=old;
}
{
auto old=v.backdrop_filters.items[0].length_amount.linear_vw;
v.backdrop_filters.items[0].length_amount.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.backdrop_filters.items[0].length_amount.linear_vw\n"; }
v.backdrop_filters.items[0].length_amount.linear_vw=old;
}
{
auto old=v.backdrop_filters.items[0].length_amount.linear_vh;
v.backdrop_filters.items[0].length_amount.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.backdrop_filters.items[0].length_amount.linear_vh\n"; }
v.backdrop_filters.items[0].length_amount.linear_vh=old;
}
{
auto old=v.backdrop_filters.items[0].scalar_amount;
v.backdrop_filters.items[0].scalar_amount=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.backdrop_filters.items[0].scalar_amount\n"; }
v.backdrop_filters.items[0].scalar_amount=old;
}
{
auto old=v.backdrop_filters.count;
v.backdrop_filters.count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.backdrop_filters.count\n"; }
v.backdrop_filters.count=old;
}
{
auto old=v.border_color.r;
v.border_color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_color.r\n"; }
v.border_color.r=old;
}
{
auto old=v.border_color.g;
v.border_color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_color.g\n"; }
v.border_color.g=old;
}
{
auto old=v.border_color.b;
v.border_color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_color.b\n"; }
v.border_color.b=old;
}
{
auto old=v.border_color.a;
v.border_color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_color.a\n"; }
v.border_color.a=old;
}
{
auto old=v.border_colors.left.r;
v.border_colors.left.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.left.r\n"; }
v.border_colors.left.r=old;
}
{
auto old=v.border_colors.left.g;
v.border_colors.left.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.left.g\n"; }
v.border_colors.left.g=old;
}
{
auto old=v.border_colors.left.b;
v.border_colors.left.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.left.b\n"; }
v.border_colors.left.b=old;
}
{
auto old=v.border_colors.left.a;
v.border_colors.left.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.left.a\n"; }
v.border_colors.left.a=old;
}
{
auto old=v.border_colors.top.r;
v.border_colors.top.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.top.r\n"; }
v.border_colors.top.r=old;
}
{
auto old=v.border_colors.top.g;
v.border_colors.top.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.top.g\n"; }
v.border_colors.top.g=old;
}
{
auto old=v.border_colors.top.b;
v.border_colors.top.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.top.b\n"; }
v.border_colors.top.b=old;
}
{
auto old=v.border_colors.top.a;
v.border_colors.top.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.top.a\n"; }
v.border_colors.top.a=old;
}
{
auto old=v.border_colors.right.r;
v.border_colors.right.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.right.r\n"; }
v.border_colors.right.r=old;
}
{
auto old=v.border_colors.right.g;
v.border_colors.right.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.right.g\n"; }
v.border_colors.right.g=old;
}
{
auto old=v.border_colors.right.b;
v.border_colors.right.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.right.b\n"; }
v.border_colors.right.b=old;
}
{
auto old=v.border_colors.right.a;
v.border_colors.right.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.right.a\n"; }
v.border_colors.right.a=old;
}
{
auto old=v.border_colors.bottom.r;
v.border_colors.bottom.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.bottom.r\n"; }
v.border_colors.bottom.r=old;
}
{
auto old=v.border_colors.bottom.g;
v.border_colors.bottom.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.bottom.g\n"; }
v.border_colors.bottom.g=old;
}
{
auto old=v.border_colors.bottom.b;
v.border_colors.bottom.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.bottom.b\n"; }
v.border_colors.bottom.b=old;
}
{
auto old=v.border_colors.bottom.a;
v.border_colors.bottom.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_colors.bottom.a\n"; }
v.border_colors.bottom.a=old;
}
{
auto old=v.border_styles.left;
v.border_styles.left=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_styles.left\n"; }
v.border_styles.left=old;
}
{
auto old=v.border_styles.top;
v.border_styles.top=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_styles.top\n"; }
v.border_styles.top=old;
}
{
auto old=v.border_styles.right;
v.border_styles.right=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_styles.right\n"; }
v.border_styles.right=old;
}
{
auto old=v.border_styles.bottom;
v.border_styles.bottom=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_styles.bottom\n"; }
v.border_styles.bottom=old;
}
{
auto old=v.has_text_gradient;
v.has_text_gradient=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.has_text_gradient\n"; }
v.has_text_gradient=old;
}
{
auto old=v.text_gradient.direction_x;
v.text_gradient.direction_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.direction_x\n"; }
v.text_gradient.direction_x=old;
}
{
auto old=v.text_gradient.direction_y;
v.text_gradient.direction_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.direction_y\n"; }
v.text_gradient.direction_y=old;
}
{
auto old=v.text_gradient.stops[0].color.r;
v.text_gradient.stops[0].color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].color.r\n"; }
v.text_gradient.stops[0].color.r=old;
}
{
auto old=v.text_gradient.stops[0].color.g;
v.text_gradient.stops[0].color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].color.g\n"; }
v.text_gradient.stops[0].color.g=old;
}
{
auto old=v.text_gradient.stops[0].color.b;
v.text_gradient.stops[0].color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].color.b\n"; }
v.text_gradient.stops[0].color.b=old;
}
{
auto old=v.text_gradient.stops[0].color.a;
v.text_gradient.stops[0].color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].color.a\n"; }
v.text_gradient.stops[0].color.a=old;
}
{
auto old=v.text_gradient.stops[0].position.value;
v.text_gradient.stops[0].position.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].position.value\n"; }
v.text_gradient.stops[0].position.value=old;
}
{
auto old=v.text_gradient.stops[0].position.unit;
v.text_gradient.stops[0].position.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].position.unit\n"; }
v.text_gradient.stops[0].position.unit=old;
}
{
auto old=v.text_gradient.stops[0].position.linear_percent;
v.text_gradient.stops[0].position.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].position.linear_percent\n"; }
v.text_gradient.stops[0].position.linear_percent=old;
}
{
auto old=v.text_gradient.stops[0].position.linear_em;
v.text_gradient.stops[0].position.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].position.linear_em\n"; }
v.text_gradient.stops[0].position.linear_em=old;
}
{
auto old=v.text_gradient.stops[0].position.linear_rem;
v.text_gradient.stops[0].position.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].position.linear_rem\n"; }
v.text_gradient.stops[0].position.linear_rem=old;
}
{
auto old=v.text_gradient.stops[0].position.linear_vw;
v.text_gradient.stops[0].position.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].position.linear_vw\n"; }
v.text_gradient.stops[0].position.linear_vw=old;
}
{
auto old=v.text_gradient.stops[0].position.linear_vh;
v.text_gradient.stops[0].position.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].position.linear_vh\n"; }
v.text_gradient.stops[0].position.linear_vh=old;
}
{
auto old=v.text_gradient.stops[0].offset;
v.text_gradient.stops[0].offset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].offset\n"; }
v.text_gradient.stops[0].offset=old;
}
{
auto old=v.text_gradient.stops[0].has_position;
v.text_gradient.stops[0].has_position=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stops[0].has_position\n"; }
v.text_gradient.stops[0].has_position=old;
}
{
auto old=v.text_gradient.stop_count;
v.text_gradient.stop_count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_gradient.stop_count\n"; }
v.text_gradient.stop_count=old;
}
{
auto old=v.transforms.items[0].kind;
v.transforms.items[0].kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].kind\n"; }
v.transforms.items[0].kind=old;
}
{
auto old=v.transforms.items[0].x.value;
v.transforms.items[0].x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].x.value\n"; }
v.transforms.items[0].x.value=old;
}
{
auto old=v.transforms.items[0].x.unit;
v.transforms.items[0].x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].x.unit\n"; }
v.transforms.items[0].x.unit=old;
}
{
auto old=v.transforms.items[0].x.linear_percent;
v.transforms.items[0].x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].x.linear_percent\n"; }
v.transforms.items[0].x.linear_percent=old;
}
{
auto old=v.transforms.items[0].x.linear_em;
v.transforms.items[0].x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].x.linear_em\n"; }
v.transforms.items[0].x.linear_em=old;
}
{
auto old=v.transforms.items[0].x.linear_rem;
v.transforms.items[0].x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].x.linear_rem\n"; }
v.transforms.items[0].x.linear_rem=old;
}
{
auto old=v.transforms.items[0].x.linear_vw;
v.transforms.items[0].x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].x.linear_vw\n"; }
v.transforms.items[0].x.linear_vw=old;
}
{
auto old=v.transforms.items[0].x.linear_vh;
v.transforms.items[0].x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].x.linear_vh\n"; }
v.transforms.items[0].x.linear_vh=old;
}
{
auto old=v.transforms.items[0].y.value;
v.transforms.items[0].y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].y.value\n"; }
v.transforms.items[0].y.value=old;
}
{
auto old=v.transforms.items[0].y.unit;
v.transforms.items[0].y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].y.unit\n"; }
v.transforms.items[0].y.unit=old;
}
{
auto old=v.transforms.items[0].y.linear_percent;
v.transforms.items[0].y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].y.linear_percent\n"; }
v.transforms.items[0].y.linear_percent=old;
}
{
auto old=v.transforms.items[0].y.linear_em;
v.transforms.items[0].y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].y.linear_em\n"; }
v.transforms.items[0].y.linear_em=old;
}
{
auto old=v.transforms.items[0].y.linear_rem;
v.transforms.items[0].y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].y.linear_rem\n"; }
v.transforms.items[0].y.linear_rem=old;
}
{
auto old=v.transforms.items[0].y.linear_vw;
v.transforms.items[0].y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].y.linear_vw\n"; }
v.transforms.items[0].y.linear_vw=old;
}
{
auto old=v.transforms.items[0].y.linear_vh;
v.transforms.items[0].y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].y.linear_vh\n"; }
v.transforms.items[0].y.linear_vh=old;
}
{
auto old=v.transforms.items[0].scalar_x;
v.transforms.items[0].scalar_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].scalar_x\n"; }
v.transforms.items[0].scalar_x=old;
}
{
auto old=v.transforms.items[0].scalar_y;
v.transforms.items[0].scalar_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].scalar_y\n"; }
v.transforms.items[0].scalar_y=old;
}
{
auto old=v.transforms.items[0].angle_radians;
v.transforms.items[0].angle_radians=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].angle_radians\n"; }
v.transforms.items[0].angle_radians=old;
}
{
auto old=v.transforms.items[0].matrix_a;
v.transforms.items[0].matrix_a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].matrix_a\n"; }
v.transforms.items[0].matrix_a=old;
}
{
auto old=v.transforms.items[0].matrix_b;
v.transforms.items[0].matrix_b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].matrix_b\n"; }
v.transforms.items[0].matrix_b=old;
}
{
auto old=v.transforms.items[0].matrix_c;
v.transforms.items[0].matrix_c=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].matrix_c\n"; }
v.transforms.items[0].matrix_c=old;
}
{
auto old=v.transforms.items[0].matrix_d;
v.transforms.items[0].matrix_d=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].matrix_d\n"; }
v.transforms.items[0].matrix_d=old;
}
{
auto old=v.transforms.items[0].matrix_e;
v.transforms.items[0].matrix_e=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].matrix_e\n"; }
v.transforms.items[0].matrix_e=old;
}
{
auto old=v.transforms.items[0].matrix_f;
v.transforms.items[0].matrix_f=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.items[0].matrix_f\n"; }
v.transforms.items[0].matrix_f=old;
}
{
auto old=v.transforms.count;
v.transforms.count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transforms.count\n"; }
v.transforms.count=old;
}
{
auto old=v.transform_origin.x.value;
v.transform_origin.x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.x.value\n"; }
v.transform_origin.x.value=old;
}
{
auto old=v.transform_origin.x.unit;
v.transform_origin.x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.x.unit\n"; }
v.transform_origin.x.unit=old;
}
{
auto old=v.transform_origin.x.linear_percent;
v.transform_origin.x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.x.linear_percent\n"; }
v.transform_origin.x.linear_percent=old;
}
{
auto old=v.transform_origin.x.linear_em;
v.transform_origin.x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.x.linear_em\n"; }
v.transform_origin.x.linear_em=old;
}
{
auto old=v.transform_origin.x.linear_rem;
v.transform_origin.x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.x.linear_rem\n"; }
v.transform_origin.x.linear_rem=old;
}
{
auto old=v.transform_origin.x.linear_vw;
v.transform_origin.x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.x.linear_vw\n"; }
v.transform_origin.x.linear_vw=old;
}
{
auto old=v.transform_origin.x.linear_vh;
v.transform_origin.x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.x.linear_vh\n"; }
v.transform_origin.x.linear_vh=old;
}
{
auto old=v.transform_origin.y.value;
v.transform_origin.y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.y.value\n"; }
v.transform_origin.y.value=old;
}
{
auto old=v.transform_origin.y.unit;
v.transform_origin.y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.y.unit\n"; }
v.transform_origin.y.unit=old;
}
{
auto old=v.transform_origin.y.linear_percent;
v.transform_origin.y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.y.linear_percent\n"; }
v.transform_origin.y.linear_percent=old;
}
{
auto old=v.transform_origin.y.linear_em;
v.transform_origin.y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.y.linear_em\n"; }
v.transform_origin.y.linear_em=old;
}
{
auto old=v.transform_origin.y.linear_rem;
v.transform_origin.y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.y.linear_rem\n"; }
v.transform_origin.y.linear_rem=old;
}
{
auto old=v.transform_origin.y.linear_vw;
v.transform_origin.y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.y.linear_vw\n"; }
v.transform_origin.y.linear_vw=old;
}
{
auto old=v.transform_origin.y.linear_vh;
v.transform_origin.y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transform_origin.y.linear_vh\n"; }
v.transform_origin.y.linear_vh=old;
}
{
auto old=v.text_shadows.items[0].offset_x;
v.text_shadows.items[0].offset_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_shadows.items[0].offset_x\n"; }
v.text_shadows.items[0].offset_x=old;
}
{
auto old=v.text_shadows.items[0].offset_y;
v.text_shadows.items[0].offset_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_shadows.items[0].offset_y\n"; }
v.text_shadows.items[0].offset_y=old;
}
{
auto old=v.text_shadows.items[0].blur_radius;
v.text_shadows.items[0].blur_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_shadows.items[0].blur_radius\n"; }
v.text_shadows.items[0].blur_radius=old;
}
{
auto old=v.text_shadows.items[0].spread_radius;
v.text_shadows.items[0].spread_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_shadows.items[0].spread_radius\n"; }
v.text_shadows.items[0].spread_radius=old;
}
{
auto old=v.text_shadows.items[0].color.r;
v.text_shadows.items[0].color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_shadows.items[0].color.r\n"; }
v.text_shadows.items[0].color.r=old;
}
{
auto old=v.text_shadows.items[0].color.g;
v.text_shadows.items[0].color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_shadows.items[0].color.g\n"; }
v.text_shadows.items[0].color.g=old;
}
{
auto old=v.text_shadows.items[0].color.b;
v.text_shadows.items[0].color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_shadows.items[0].color.b\n"; }
v.text_shadows.items[0].color.b=old;
}
{
auto old=v.text_shadows.items[0].color.a;
v.text_shadows.items[0].color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_shadows.items[0].color.a\n"; }
v.text_shadows.items[0].color.a=old;
}
{
auto old=v.text_shadows.items[0].inset;
v.text_shadows.items[0].inset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_shadows.items[0].inset\n"; }
v.text_shadows.items[0].inset=old;
}
{
auto old=v.text_shadows.count;
v.text_shadows.count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_shadows.count\n"; }
v.text_shadows.count=old;
}
{
auto old=v.box_shadows.items[0].offset_x;
v.box_shadows.items[0].offset_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_shadows.items[0].offset_x\n"; }
v.box_shadows.items[0].offset_x=old;
}
{
auto old=v.box_shadows.items[0].offset_y;
v.box_shadows.items[0].offset_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_shadows.items[0].offset_y\n"; }
v.box_shadows.items[0].offset_y=old;
}
{
auto old=v.box_shadows.items[0].blur_radius;
v.box_shadows.items[0].blur_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_shadows.items[0].blur_radius\n"; }
v.box_shadows.items[0].blur_radius=old;
}
{
auto old=v.box_shadows.items[0].spread_radius;
v.box_shadows.items[0].spread_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_shadows.items[0].spread_radius\n"; }
v.box_shadows.items[0].spread_radius=old;
}
{
auto old=v.box_shadows.items[0].color.r;
v.box_shadows.items[0].color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_shadows.items[0].color.r\n"; }
v.box_shadows.items[0].color.r=old;
}
{
auto old=v.box_shadows.items[0].color.g;
v.box_shadows.items[0].color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_shadows.items[0].color.g\n"; }
v.box_shadows.items[0].color.g=old;
}
{
auto old=v.box_shadows.items[0].color.b;
v.box_shadows.items[0].color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_shadows.items[0].color.b\n"; }
v.box_shadows.items[0].color.b=old;
}
{
auto old=v.box_shadows.items[0].color.a;
v.box_shadows.items[0].color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_shadows.items[0].color.a\n"; }
v.box_shadows.items[0].color.a=old;
}
{
auto old=v.box_shadows.items[0].inset;
v.box_shadows.items[0].inset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_shadows.items[0].inset\n"; }
v.box_shadows.items[0].inset=old;
}
{
auto old=v.box_shadows.count;
v.box_shadows.count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.box_shadows.count\n"; }
v.box_shadows.count=old;
}
{
auto old=v.border_radii.top_left.horizontal.value;
v.border_radii.top_left.horizontal.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.horizontal.value\n"; }
v.border_radii.top_left.horizontal.value=old;
}
{
auto old=v.border_radii.top_left.horizontal.unit;
v.border_radii.top_left.horizontal.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.horizontal.unit\n"; }
v.border_radii.top_left.horizontal.unit=old;
}
{
auto old=v.border_radii.top_left.horizontal.linear_percent;
v.border_radii.top_left.horizontal.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.horizontal.linear_percent\n"; }
v.border_radii.top_left.horizontal.linear_percent=old;
}
{
auto old=v.border_radii.top_left.horizontal.linear_em;
v.border_radii.top_left.horizontal.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.horizontal.linear_em\n"; }
v.border_radii.top_left.horizontal.linear_em=old;
}
{
auto old=v.border_radii.top_left.horizontal.linear_rem;
v.border_radii.top_left.horizontal.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.horizontal.linear_rem\n"; }
v.border_radii.top_left.horizontal.linear_rem=old;
}
{
auto old=v.border_radii.top_left.horizontal.linear_vw;
v.border_radii.top_left.horizontal.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.horizontal.linear_vw\n"; }
v.border_radii.top_left.horizontal.linear_vw=old;
}
{
auto old=v.border_radii.top_left.horizontal.linear_vh;
v.border_radii.top_left.horizontal.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.horizontal.linear_vh\n"; }
v.border_radii.top_left.horizontal.linear_vh=old;
}
{
auto old=v.border_radii.top_left.vertical.value;
v.border_radii.top_left.vertical.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.vertical.value\n"; }
v.border_radii.top_left.vertical.value=old;
}
{
auto old=v.border_radii.top_left.vertical.unit;
v.border_radii.top_left.vertical.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.vertical.unit\n"; }
v.border_radii.top_left.vertical.unit=old;
}
{
auto old=v.border_radii.top_left.vertical.linear_percent;
v.border_radii.top_left.vertical.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.vertical.linear_percent\n"; }
v.border_radii.top_left.vertical.linear_percent=old;
}
{
auto old=v.border_radii.top_left.vertical.linear_em;
v.border_radii.top_left.vertical.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.vertical.linear_em\n"; }
v.border_radii.top_left.vertical.linear_em=old;
}
{
auto old=v.border_radii.top_left.vertical.linear_rem;
v.border_radii.top_left.vertical.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.vertical.linear_rem\n"; }
v.border_radii.top_left.vertical.linear_rem=old;
}
{
auto old=v.border_radii.top_left.vertical.linear_vw;
v.border_radii.top_left.vertical.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.vertical.linear_vw\n"; }
v.border_radii.top_left.vertical.linear_vw=old;
}
{
auto old=v.border_radii.top_left.vertical.linear_vh;
v.border_radii.top_left.vertical.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_left.vertical.linear_vh\n"; }
v.border_radii.top_left.vertical.linear_vh=old;
}
{
auto old=v.border_radii.top_right.horizontal.value;
v.border_radii.top_right.horizontal.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.horizontal.value\n"; }
v.border_radii.top_right.horizontal.value=old;
}
{
auto old=v.border_radii.top_right.horizontal.unit;
v.border_radii.top_right.horizontal.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.horizontal.unit\n"; }
v.border_radii.top_right.horizontal.unit=old;
}
{
auto old=v.border_radii.top_right.horizontal.linear_percent;
v.border_radii.top_right.horizontal.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.horizontal.linear_percent\n"; }
v.border_radii.top_right.horizontal.linear_percent=old;
}
{
auto old=v.border_radii.top_right.horizontal.linear_em;
v.border_radii.top_right.horizontal.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.horizontal.linear_em\n"; }
v.border_radii.top_right.horizontal.linear_em=old;
}
{
auto old=v.border_radii.top_right.horizontal.linear_rem;
v.border_radii.top_right.horizontal.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.horizontal.linear_rem\n"; }
v.border_radii.top_right.horizontal.linear_rem=old;
}
{
auto old=v.border_radii.top_right.horizontal.linear_vw;
v.border_radii.top_right.horizontal.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.horizontal.linear_vw\n"; }
v.border_radii.top_right.horizontal.linear_vw=old;
}
{
auto old=v.border_radii.top_right.horizontal.linear_vh;
v.border_radii.top_right.horizontal.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.horizontal.linear_vh\n"; }
v.border_radii.top_right.horizontal.linear_vh=old;
}
{
auto old=v.border_radii.top_right.vertical.value;
v.border_radii.top_right.vertical.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.vertical.value\n"; }
v.border_radii.top_right.vertical.value=old;
}
{
auto old=v.border_radii.top_right.vertical.unit;
v.border_radii.top_right.vertical.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.vertical.unit\n"; }
v.border_radii.top_right.vertical.unit=old;
}
{
auto old=v.border_radii.top_right.vertical.linear_percent;
v.border_radii.top_right.vertical.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.vertical.linear_percent\n"; }
v.border_radii.top_right.vertical.linear_percent=old;
}
{
auto old=v.border_radii.top_right.vertical.linear_em;
v.border_radii.top_right.vertical.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.vertical.linear_em\n"; }
v.border_radii.top_right.vertical.linear_em=old;
}
{
auto old=v.border_radii.top_right.vertical.linear_rem;
v.border_radii.top_right.vertical.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.vertical.linear_rem\n"; }
v.border_radii.top_right.vertical.linear_rem=old;
}
{
auto old=v.border_radii.top_right.vertical.linear_vw;
v.border_radii.top_right.vertical.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.vertical.linear_vw\n"; }
v.border_radii.top_right.vertical.linear_vw=old;
}
{
auto old=v.border_radii.top_right.vertical.linear_vh;
v.border_radii.top_right.vertical.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.top_right.vertical.linear_vh\n"; }
v.border_radii.top_right.vertical.linear_vh=old;
}
{
auto old=v.border_radii.bottom_right.horizontal.value;
v.border_radii.bottom_right.horizontal.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.horizontal.value\n"; }
v.border_radii.bottom_right.horizontal.value=old;
}
{
auto old=v.border_radii.bottom_right.horizontal.unit;
v.border_radii.bottom_right.horizontal.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.horizontal.unit\n"; }
v.border_radii.bottom_right.horizontal.unit=old;
}
{
auto old=v.border_radii.bottom_right.horizontal.linear_percent;
v.border_radii.bottom_right.horizontal.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.horizontal.linear_percent\n"; }
v.border_radii.bottom_right.horizontal.linear_percent=old;
}
{
auto old=v.border_radii.bottom_right.horizontal.linear_em;
v.border_radii.bottom_right.horizontal.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.horizontal.linear_em\n"; }
v.border_radii.bottom_right.horizontal.linear_em=old;
}
{
auto old=v.border_radii.bottom_right.horizontal.linear_rem;
v.border_radii.bottom_right.horizontal.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.horizontal.linear_rem\n"; }
v.border_radii.bottom_right.horizontal.linear_rem=old;
}
{
auto old=v.border_radii.bottom_right.horizontal.linear_vw;
v.border_radii.bottom_right.horizontal.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.horizontal.linear_vw\n"; }
v.border_radii.bottom_right.horizontal.linear_vw=old;
}
{
auto old=v.border_radii.bottom_right.horizontal.linear_vh;
v.border_radii.bottom_right.horizontal.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.horizontal.linear_vh\n"; }
v.border_radii.bottom_right.horizontal.linear_vh=old;
}
{
auto old=v.border_radii.bottom_right.vertical.value;
v.border_radii.bottom_right.vertical.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.vertical.value\n"; }
v.border_radii.bottom_right.vertical.value=old;
}
{
auto old=v.border_radii.bottom_right.vertical.unit;
v.border_radii.bottom_right.vertical.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.vertical.unit\n"; }
v.border_radii.bottom_right.vertical.unit=old;
}
{
auto old=v.border_radii.bottom_right.vertical.linear_percent;
v.border_radii.bottom_right.vertical.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.vertical.linear_percent\n"; }
v.border_radii.bottom_right.vertical.linear_percent=old;
}
{
auto old=v.border_radii.bottom_right.vertical.linear_em;
v.border_radii.bottom_right.vertical.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.vertical.linear_em\n"; }
v.border_radii.bottom_right.vertical.linear_em=old;
}
{
auto old=v.border_radii.bottom_right.vertical.linear_rem;
v.border_radii.bottom_right.vertical.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.vertical.linear_rem\n"; }
v.border_radii.bottom_right.vertical.linear_rem=old;
}
{
auto old=v.border_radii.bottom_right.vertical.linear_vw;
v.border_radii.bottom_right.vertical.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.vertical.linear_vw\n"; }
v.border_radii.bottom_right.vertical.linear_vw=old;
}
{
auto old=v.border_radii.bottom_right.vertical.linear_vh;
v.border_radii.bottom_right.vertical.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_right.vertical.linear_vh\n"; }
v.border_radii.bottom_right.vertical.linear_vh=old;
}
{
auto old=v.border_radii.bottom_left.horizontal.value;
v.border_radii.bottom_left.horizontal.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.horizontal.value\n"; }
v.border_radii.bottom_left.horizontal.value=old;
}
{
auto old=v.border_radii.bottom_left.horizontal.unit;
v.border_radii.bottom_left.horizontal.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.horizontal.unit\n"; }
v.border_radii.bottom_left.horizontal.unit=old;
}
{
auto old=v.border_radii.bottom_left.horizontal.linear_percent;
v.border_radii.bottom_left.horizontal.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.horizontal.linear_percent\n"; }
v.border_radii.bottom_left.horizontal.linear_percent=old;
}
{
auto old=v.border_radii.bottom_left.horizontal.linear_em;
v.border_radii.bottom_left.horizontal.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.horizontal.linear_em\n"; }
v.border_radii.bottom_left.horizontal.linear_em=old;
}
{
auto old=v.border_radii.bottom_left.horizontal.linear_rem;
v.border_radii.bottom_left.horizontal.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.horizontal.linear_rem\n"; }
v.border_radii.bottom_left.horizontal.linear_rem=old;
}
{
auto old=v.border_radii.bottom_left.horizontal.linear_vw;
v.border_radii.bottom_left.horizontal.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.horizontal.linear_vw\n"; }
v.border_radii.bottom_left.horizontal.linear_vw=old;
}
{
auto old=v.border_radii.bottom_left.horizontal.linear_vh;
v.border_radii.bottom_left.horizontal.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.horizontal.linear_vh\n"; }
v.border_radii.bottom_left.horizontal.linear_vh=old;
}
{
auto old=v.border_radii.bottom_left.vertical.value;
v.border_radii.bottom_left.vertical.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.vertical.value\n"; }
v.border_radii.bottom_left.vertical.value=old;
}
{
auto old=v.border_radii.bottom_left.vertical.unit;
v.border_radii.bottom_left.vertical.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.vertical.unit\n"; }
v.border_radii.bottom_left.vertical.unit=old;
}
{
auto old=v.border_radii.bottom_left.vertical.linear_percent;
v.border_radii.bottom_left.vertical.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.vertical.linear_percent\n"; }
v.border_radii.bottom_left.vertical.linear_percent=old;
}
{
auto old=v.border_radii.bottom_left.vertical.linear_em;
v.border_radii.bottom_left.vertical.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.vertical.linear_em\n"; }
v.border_radii.bottom_left.vertical.linear_em=old;
}
{
auto old=v.border_radii.bottom_left.vertical.linear_rem;
v.border_radii.bottom_left.vertical.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.vertical.linear_rem\n"; }
v.border_radii.bottom_left.vertical.linear_rem=old;
}
{
auto old=v.border_radii.bottom_left.vertical.linear_vw;
v.border_radii.bottom_left.vertical.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.vertical.linear_vw\n"; }
v.border_radii.bottom_left.vertical.linear_vw=old;
}
{
auto old=v.border_radii.bottom_left.vertical.linear_vh;
v.border_radii.bottom_left.vertical.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.border_radii.bottom_left.vertical.linear_vh\n"; }
v.border_radii.bottom_left.vertical.linear_vh=old;
}
{
auto old=v.opacity;
v.opacity=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.opacity\n"; }
v.opacity=old;
}
{
auto old=v.font_family[0];
v.font_family[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.font_family[0]\n"; }
v.font_family[0]=old;
}
{
auto old=v.font_size;
v.font_size=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.font_size\n"; }
v.font_size=old;
}
{
auto old=v.font_weight;
v.font_weight=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.font_weight\n"; }
v.font_weight=old;
}
{
auto old=v.line_height;
v.line_height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.line_height\n"; }
v.line_height=old;
}
{
auto old=v.text_align;
v.text_align=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_align\n"; }
v.text_align=old;
}
{
auto old=v.text_transform;
v.text_transform=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.text_transform\n"; }
v.text_transform=old;
}
{
auto old=v.white_space;
v.white_space=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.white_space\n"; }
v.white_space=old;
}
{
auto old=v.overflow_x;
v.overflow_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.overflow_x\n"; }
v.overflow_x=old;
}
{
auto old=v.overflow_y;
v.overflow_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.overflow_y\n"; }
v.overflow_y=old;
}
{
auto old=v.cursor;
v.cursor=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.cursor\n"; }
v.cursor=old;
}
{
auto old=v.pointer_events;
v.pointer_events=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.pointer_events\n"; }
v.pointer_events=old;
}
{
auto old=v.visible;
v.visible=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.visible\n"; }
v.visible=old;
}
{
auto old=v.z_index;
v.z_index=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.z_index\n"; }
v.z_index=old;
}
{
auto old=v.z_index_kind;
v.z_index_kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.z_index_kind\n"; }
v.z_index_kind=old;
}
{
auto old=v.transition_properties;
v.transition_properties=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transition_properties\n"; }
v.transition_properties=old;
}
{
auto old=v.transition_duration;
v.transition_duration=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transition_duration\n"; }
v.transition_duration=old;
}
{
auto old=v.transition_delay;
v.transition_delay=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transition_delay\n"; }
v.transition_delay=old;
}
{
auto old=v.transition_timing;
v.transition_timing=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v.transition_timing\n"; }
v.transition_timing=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_FILL_ROUNDED_RECT;
const auto original=dump(v);
{
auto old=v .data.fill_rounded_rect.rect.x;
v .data.fill_rounded_rect.rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.rect.x\n"; }
v .data.fill_rounded_rect.rect.x=old;
}
{
auto old=v .data.fill_rounded_rect.rect.y;
v .data.fill_rounded_rect.rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.rect.y\n"; }
v .data.fill_rounded_rect.rect.y=old;
}
{
auto old=v .data.fill_rounded_rect.rect.width;
v .data.fill_rounded_rect.rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.rect.width\n"; }
v .data.fill_rounded_rect.rect.width=old;
}
{
auto old=v .data.fill_rounded_rect.rect.height;
v .data.fill_rounded_rect.rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.rect.height\n"; }
v .data.fill_rounded_rect.rect.height=old;
}
{
auto old=v .data.fill_rounded_rect.radii.top_left_x;
v .data.fill_rounded_rect.radii.top_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.radii.top_left_x\n"; }
v .data.fill_rounded_rect.radii.top_left_x=old;
}
{
auto old=v .data.fill_rounded_rect.radii.top_left_y;
v .data.fill_rounded_rect.radii.top_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.radii.top_left_y\n"; }
v .data.fill_rounded_rect.radii.top_left_y=old;
}
{
auto old=v .data.fill_rounded_rect.radii.top_right_x;
v .data.fill_rounded_rect.radii.top_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.radii.top_right_x\n"; }
v .data.fill_rounded_rect.radii.top_right_x=old;
}
{
auto old=v .data.fill_rounded_rect.radii.top_right_y;
v .data.fill_rounded_rect.radii.top_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.radii.top_right_y\n"; }
v .data.fill_rounded_rect.radii.top_right_y=old;
}
{
auto old=v .data.fill_rounded_rect.radii.bottom_right_x;
v .data.fill_rounded_rect.radii.bottom_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.radii.bottom_right_x\n"; }
v .data.fill_rounded_rect.radii.bottom_right_x=old;
}
{
auto old=v .data.fill_rounded_rect.radii.bottom_right_y;
v .data.fill_rounded_rect.radii.bottom_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.radii.bottom_right_y\n"; }
v .data.fill_rounded_rect.radii.bottom_right_y=old;
}
{
auto old=v .data.fill_rounded_rect.radii.bottom_left_x;
v .data.fill_rounded_rect.radii.bottom_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.radii.bottom_left_x\n"; }
v .data.fill_rounded_rect.radii.bottom_left_x=old;
}
{
auto old=v .data.fill_rounded_rect.radii.bottom_left_y;
v .data.fill_rounded_rect.radii.bottom_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.radii.bottom_left_y\n"; }
v .data.fill_rounded_rect.radii.bottom_left_y=old;
}
{
auto old=v .data.fill_rounded_rect.color.r;
v .data.fill_rounded_rect.color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.color.r\n"; }
v .data.fill_rounded_rect.color.r=old;
}
{
auto old=v .data.fill_rounded_rect.color.g;
v .data.fill_rounded_rect.color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.color.g\n"; }
v .data.fill_rounded_rect.color.g=old;
}
{
auto old=v .data.fill_rounded_rect.color.b;
v .data.fill_rounded_rect.color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.color.b\n"; }
v .data.fill_rounded_rect.color.b=old;
}
{
auto old=v .data.fill_rounded_rect.color.a;
v .data.fill_rounded_rect.color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect.color.a\n"; }
v .data.fill_rounded_rect.color.a=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_DRAW_BOX_SHADOW;
const auto original=dump(v);
{
auto old=v .data.draw_box_shadow.rect.x;
v .data.draw_box_shadow.rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.rect.x\n"; }
v .data.draw_box_shadow.rect.x=old;
}
{
auto old=v .data.draw_box_shadow.rect.y;
v .data.draw_box_shadow.rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.rect.y\n"; }
v .data.draw_box_shadow.rect.y=old;
}
{
auto old=v .data.draw_box_shadow.rect.width;
v .data.draw_box_shadow.rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.rect.width\n"; }
v .data.draw_box_shadow.rect.width=old;
}
{
auto old=v .data.draw_box_shadow.rect.height;
v .data.draw_box_shadow.rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.rect.height\n"; }
v .data.draw_box_shadow.rect.height=old;
}
{
auto old=v .data.draw_box_shadow.radii.top_left_x;
v .data.draw_box_shadow.radii.top_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.radii.top_left_x\n"; }
v .data.draw_box_shadow.radii.top_left_x=old;
}
{
auto old=v .data.draw_box_shadow.radii.top_left_y;
v .data.draw_box_shadow.radii.top_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.radii.top_left_y\n"; }
v .data.draw_box_shadow.radii.top_left_y=old;
}
{
auto old=v .data.draw_box_shadow.radii.top_right_x;
v .data.draw_box_shadow.radii.top_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.radii.top_right_x\n"; }
v .data.draw_box_shadow.radii.top_right_x=old;
}
{
auto old=v .data.draw_box_shadow.radii.top_right_y;
v .data.draw_box_shadow.radii.top_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.radii.top_right_y\n"; }
v .data.draw_box_shadow.radii.top_right_y=old;
}
{
auto old=v .data.draw_box_shadow.radii.bottom_right_x;
v .data.draw_box_shadow.radii.bottom_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.radii.bottom_right_x\n"; }
v .data.draw_box_shadow.radii.bottom_right_x=old;
}
{
auto old=v .data.draw_box_shadow.radii.bottom_right_y;
v .data.draw_box_shadow.radii.bottom_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.radii.bottom_right_y\n"; }
v .data.draw_box_shadow.radii.bottom_right_y=old;
}
{
auto old=v .data.draw_box_shadow.radii.bottom_left_x;
v .data.draw_box_shadow.radii.bottom_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.radii.bottom_left_x\n"; }
v .data.draw_box_shadow.radii.bottom_left_x=old;
}
{
auto old=v .data.draw_box_shadow.radii.bottom_left_y;
v .data.draw_box_shadow.radii.bottom_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.radii.bottom_left_y\n"; }
v .data.draw_box_shadow.radii.bottom_left_y=old;
}
{
auto old=v .data.draw_box_shadow.shadow.offset_x;
v .data.draw_box_shadow.shadow.offset_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.shadow.offset_x\n"; }
v .data.draw_box_shadow.shadow.offset_x=old;
}
{
auto old=v .data.draw_box_shadow.shadow.offset_y;
v .data.draw_box_shadow.shadow.offset_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.shadow.offset_y\n"; }
v .data.draw_box_shadow.shadow.offset_y=old;
}
{
auto old=v .data.draw_box_shadow.shadow.blur_radius;
v .data.draw_box_shadow.shadow.blur_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.shadow.blur_radius\n"; }
v .data.draw_box_shadow.shadow.blur_radius=old;
}
{
auto old=v .data.draw_box_shadow.shadow.spread_radius;
v .data.draw_box_shadow.shadow.spread_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.shadow.spread_radius\n"; }
v .data.draw_box_shadow.shadow.spread_radius=old;
}
{
auto old=v .data.draw_box_shadow.shadow.color.r;
v .data.draw_box_shadow.shadow.color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.shadow.color.r\n"; }
v .data.draw_box_shadow.shadow.color.r=old;
}
{
auto old=v .data.draw_box_shadow.shadow.color.g;
v .data.draw_box_shadow.shadow.color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.shadow.color.g\n"; }
v .data.draw_box_shadow.shadow.color.g=old;
}
{
auto old=v .data.draw_box_shadow.shadow.color.b;
v .data.draw_box_shadow.shadow.color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.shadow.color.b\n"; }
v .data.draw_box_shadow.shadow.color.b=old;
}
{
auto old=v .data.draw_box_shadow.shadow.color.a;
v .data.draw_box_shadow.shadow.color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.shadow.color.a\n"; }
v .data.draw_box_shadow.shadow.color.a=old;
}
{
auto old=v .data.draw_box_shadow.shadow.inset;
v .data.draw_box_shadow.shadow.inset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_box_shadow.shadow.inset\n"; }
v .data.draw_box_shadow.shadow.inset=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT;
aether_gradient_stop storage18[1]{}; v .data.fill_rounded_rect_gradient.gradient.stops=storage18;
v .data.fill_rounded_rect_gradient.gradient.stop_count=1;
const auto original=dump(v);
{
auto old=v .data.fill_rounded_rect_gradient.rect.x;
v .data.fill_rounded_rect_gradient.rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.rect.x\n"; }
v .data.fill_rounded_rect_gradient.rect.x=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.rect.y;
v .data.fill_rounded_rect_gradient.rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.rect.y\n"; }
v .data.fill_rounded_rect_gradient.rect.y=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.rect.width;
v .data.fill_rounded_rect_gradient.rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.rect.width\n"; }
v .data.fill_rounded_rect_gradient.rect.width=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.rect.height;
v .data.fill_rounded_rect_gradient.rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.rect.height\n"; }
v .data.fill_rounded_rect_gradient.rect.height=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.radii.top_left_x;
v .data.fill_rounded_rect_gradient.radii.top_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.radii.top_left_x\n"; }
v .data.fill_rounded_rect_gradient.radii.top_left_x=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.radii.top_left_y;
v .data.fill_rounded_rect_gradient.radii.top_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.radii.top_left_y\n"; }
v .data.fill_rounded_rect_gradient.radii.top_left_y=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.radii.top_right_x;
v .data.fill_rounded_rect_gradient.radii.top_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.radii.top_right_x\n"; }
v .data.fill_rounded_rect_gradient.radii.top_right_x=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.radii.top_right_y;
v .data.fill_rounded_rect_gradient.radii.top_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.radii.top_right_y\n"; }
v .data.fill_rounded_rect_gradient.radii.top_right_y=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.radii.bottom_right_x;
v .data.fill_rounded_rect_gradient.radii.bottom_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.radii.bottom_right_x\n"; }
v .data.fill_rounded_rect_gradient.radii.bottom_right_x=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.radii.bottom_right_y;
v .data.fill_rounded_rect_gradient.radii.bottom_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.radii.bottom_right_y\n"; }
v .data.fill_rounded_rect_gradient.radii.bottom_right_y=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.radii.bottom_left_x;
v .data.fill_rounded_rect_gradient.radii.bottom_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.radii.bottom_left_x\n"; }
v .data.fill_rounded_rect_gradient.radii.bottom_left_x=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.radii.bottom_left_y;
v .data.fill_rounded_rect_gradient.radii.bottom_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.radii.bottom_left_y\n"; }
v .data.fill_rounded_rect_gradient.radii.bottom_left_y=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient_rect.x;
v .data.fill_rounded_rect_gradient.gradient_rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient_rect.x\n"; }
v .data.fill_rounded_rect_gradient.gradient_rect.x=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient_rect.y;
v .data.fill_rounded_rect_gradient.gradient_rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient_rect.y\n"; }
v .data.fill_rounded_rect_gradient.gradient_rect.y=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient_rect.width;
v .data.fill_rounded_rect_gradient.gradient_rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient_rect.width\n"; }
v .data.fill_rounded_rect_gradient.gradient_rect.width=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient_rect.height;
v .data.fill_rounded_rect_gradient.gradient_rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient_rect.height\n"; }
v .data.fill_rounded_rect_gradient.gradient_rect.height=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.direction_x;
v .data.fill_rounded_rect_gradient.gradient.direction_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.direction_x\n"; }
v .data.fill_rounded_rect_gradient.gradient.direction_x=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.direction_y;
v .data.fill_rounded_rect_gradient.gradient.direction_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.direction_y\n"; }
v .data.fill_rounded_rect_gradient.gradient.direction_y=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].color.r;
v .data.fill_rounded_rect_gradient.gradient.stops[0].color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].color.r\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].color.r=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].color.g;
v .data.fill_rounded_rect_gradient.gradient.stops[0].color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].color.g\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].color.g=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].color.b;
v .data.fill_rounded_rect_gradient.gradient.stops[0].color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].color.b\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].color.b=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].color.a;
v .data.fill_rounded_rect_gradient.gradient.stops[0].color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].color.a\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].color.a=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].position.value;
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].position.value\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.value=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].position.unit;
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].position.unit\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.unit=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_percent;
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_percent\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_percent=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_em;
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_em\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_em=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_rem;
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_rem\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_rem=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_vw;
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_vw\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_vw=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_vh;
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_vh\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].position.linear_vh=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].offset;
v .data.fill_rounded_rect_gradient.gradient.stops[0].offset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].offset\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].offset=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stops[0].has_position;
v .data.fill_rounded_rect_gradient.gradient.stops[0].has_position=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stops[0].has_position\n"; }
v .data.fill_rounded_rect_gradient.gradient.stops[0].has_position=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.gradient.stop_count;
v .data.fill_rounded_rect_gradient.gradient.stop_count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.gradient.stop_count\n"; }
v .data.fill_rounded_rect_gradient.gradient.stop_count=old;
}
{
auto old=v .data.fill_rounded_rect_gradient.opacity;
v .data.fill_rounded_rect_gradient.opacity=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_rounded_rect_gradient.opacity\n"; }
v .data.fill_rounded_rect_gradient.opacity=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_STROKE_ROUNDED_RECT;
const auto original=dump(v);
{
auto old=v .data.stroke_rounded_rect.rect.x;
v .data.stroke_rounded_rect.rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.rect.x\n"; }
v .data.stroke_rounded_rect.rect.x=old;
}
{
auto old=v .data.stroke_rounded_rect.rect.y;
v .data.stroke_rounded_rect.rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.rect.y\n"; }
v .data.stroke_rounded_rect.rect.y=old;
}
{
auto old=v .data.stroke_rounded_rect.rect.width;
v .data.stroke_rounded_rect.rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.rect.width\n"; }
v .data.stroke_rounded_rect.rect.width=old;
}
{
auto old=v .data.stroke_rounded_rect.rect.height;
v .data.stroke_rounded_rect.rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.rect.height\n"; }
v .data.stroke_rounded_rect.rect.height=old;
}
{
auto old=v .data.stroke_rounded_rect.radii.top_left_x;
v .data.stroke_rounded_rect.radii.top_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.radii.top_left_x\n"; }
v .data.stroke_rounded_rect.radii.top_left_x=old;
}
{
auto old=v .data.stroke_rounded_rect.radii.top_left_y;
v .data.stroke_rounded_rect.radii.top_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.radii.top_left_y\n"; }
v .data.stroke_rounded_rect.radii.top_left_y=old;
}
{
auto old=v .data.stroke_rounded_rect.radii.top_right_x;
v .data.stroke_rounded_rect.radii.top_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.radii.top_right_x\n"; }
v .data.stroke_rounded_rect.radii.top_right_x=old;
}
{
auto old=v .data.stroke_rounded_rect.radii.top_right_y;
v .data.stroke_rounded_rect.radii.top_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.radii.top_right_y\n"; }
v .data.stroke_rounded_rect.radii.top_right_y=old;
}
{
auto old=v .data.stroke_rounded_rect.radii.bottom_right_x;
v .data.stroke_rounded_rect.radii.bottom_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.radii.bottom_right_x\n"; }
v .data.stroke_rounded_rect.radii.bottom_right_x=old;
}
{
auto old=v .data.stroke_rounded_rect.radii.bottom_right_y;
v .data.stroke_rounded_rect.radii.bottom_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.radii.bottom_right_y\n"; }
v .data.stroke_rounded_rect.radii.bottom_right_y=old;
}
{
auto old=v .data.stroke_rounded_rect.radii.bottom_left_x;
v .data.stroke_rounded_rect.radii.bottom_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.radii.bottom_left_x\n"; }
v .data.stroke_rounded_rect.radii.bottom_left_x=old;
}
{
auto old=v .data.stroke_rounded_rect.radii.bottom_left_y;
v .data.stroke_rounded_rect.radii.bottom_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.radii.bottom_left_y\n"; }
v .data.stroke_rounded_rect.radii.bottom_left_y=old;
}
{
auto old=v .data.stroke_rounded_rect.stroke_width;
v .data.stroke_rounded_rect.stroke_width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.stroke_width\n"; }
v .data.stroke_rounded_rect.stroke_width=old;
}
{
auto old=v .data.stroke_rounded_rect.color.r;
v .data.stroke_rounded_rect.color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.color.r\n"; }
v .data.stroke_rounded_rect.color.r=old;
}
{
auto old=v .data.stroke_rounded_rect.color.g;
v .data.stroke_rounded_rect.color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.color.g\n"; }
v .data.stroke_rounded_rect.color.g=old;
}
{
auto old=v .data.stroke_rounded_rect.color.b;
v .data.stroke_rounded_rect.color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.color.b\n"; }
v .data.stroke_rounded_rect.color.b=old;
}
{
auto old=v .data.stroke_rounded_rect.color.a;
v .data.stroke_rounded_rect.color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.stroke_rounded_rect.color.a\n"; }
v .data.stroke_rounded_rect.color.a=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_DRAW_TEXT;
char storage19[]="a"; v .data.draw_text.text=storage19;
const auto original=dump(v);
{
auto old=v .data.draw_text.text[0];
v .data.draw_text.text[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.text[0]\n"; }
v .data.draw_text.text[0]=old;
}
{
auto old=v .data.draw_text.rect.x;
v .data.draw_text.rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.rect.x\n"; }
v .data.draw_text.rect.x=old;
}
{
auto old=v .data.draw_text.rect.y;
v .data.draw_text.rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.rect.y\n"; }
v .data.draw_text.rect.y=old;
}
{
auto old=v .data.draw_text.rect.width;
v .data.draw_text.rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.rect.width\n"; }
v .data.draw_text.rect.width=old;
}
{
auto old=v .data.draw_text.rect.height;
v .data.draw_text.rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.rect.height\n"; }
v .data.draw_text.rect.height=old;
}
{
auto old=v .data.draw_text.font_family[0];
v .data.draw_text.font_family[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.font_family[0]\n"; }
v .data.draw_text.font_family[0]=old;
}
{
auto old=v .data.draw_text.font_size;
v .data.draw_text.font_size=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.font_size\n"; }
v .data.draw_text.font_size=old;
}
{
auto old=v .data.draw_text.font_weight;
v .data.draw_text.font_weight=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.font_weight\n"; }
v .data.draw_text.font_weight=old;
}
{
auto old=v .data.draw_text.text_align;
v .data.draw_text.text_align=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.text_align\n"; }
v .data.draw_text.text_align=old;
}
{
auto old=v .data.draw_text.color.r;
v .data.draw_text.color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.color.r\n"; }
v .data.draw_text.color.r=old;
}
{
auto old=v .data.draw_text.color.g;
v .data.draw_text.color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.color.g\n"; }
v .data.draw_text.color.g=old;
}
{
auto old=v .data.draw_text.color.b;
v .data.draw_text.color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.color.b\n"; }
v .data.draw_text.color.b=old;
}
{
auto old=v .data.draw_text.color.a;
v .data.draw_text.color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.color.a\n"; }
v .data.draw_text.color.a=old;
}
{
auto old=v .data.draw_text.wrap;
v .data.draw_text.wrap=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text.wrap\n"; }
v .data.draw_text.wrap=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_DRAW_TEXT_GRADIENT;
char storage20[]="a"; v .data.draw_text_gradient.text=storage20;
aether_gradient_stop storage21[1]{}; v .data.draw_text_gradient.gradient.stops=storage21;
v .data.draw_text_gradient.gradient.stop_count=1;
const auto original=dump(v);
{
auto old=v .data.draw_text_gradient.text[0];
v .data.draw_text_gradient.text[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.text[0]\n"; }
v .data.draw_text_gradient.text[0]=old;
}
{
auto old=v .data.draw_text_gradient.rect.x;
v .data.draw_text_gradient.rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.rect.x\n"; }
v .data.draw_text_gradient.rect.x=old;
}
{
auto old=v .data.draw_text_gradient.rect.y;
v .data.draw_text_gradient.rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.rect.y\n"; }
v .data.draw_text_gradient.rect.y=old;
}
{
auto old=v .data.draw_text_gradient.rect.width;
v .data.draw_text_gradient.rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.rect.width\n"; }
v .data.draw_text_gradient.rect.width=old;
}
{
auto old=v .data.draw_text_gradient.rect.height;
v .data.draw_text_gradient.rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.rect.height\n"; }
v .data.draw_text_gradient.rect.height=old;
}
{
auto old=v .data.draw_text_gradient.font_family[0];
v .data.draw_text_gradient.font_family[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.font_family[0]\n"; }
v .data.draw_text_gradient.font_family[0]=old;
}
{
auto old=v .data.draw_text_gradient.font_size;
v .data.draw_text_gradient.font_size=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.font_size\n"; }
v .data.draw_text_gradient.font_size=old;
}
{
auto old=v .data.draw_text_gradient.font_weight;
v .data.draw_text_gradient.font_weight=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.font_weight\n"; }
v .data.draw_text_gradient.font_weight=old;
}
{
auto old=v .data.draw_text_gradient.text_align;
v .data.draw_text_gradient.text_align=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.text_align\n"; }
v .data.draw_text_gradient.text_align=old;
}
{
auto old=v .data.draw_text_gradient.gradient.direction_x;
v .data.draw_text_gradient.gradient.direction_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.direction_x\n"; }
v .data.draw_text_gradient.gradient.direction_x=old;
}
{
auto old=v .data.draw_text_gradient.gradient.direction_y;
v .data.draw_text_gradient.gradient.direction_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.direction_y\n"; }
v .data.draw_text_gradient.gradient.direction_y=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].color.r;
v .data.draw_text_gradient.gradient.stops[0].color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].color.r\n"; }
v .data.draw_text_gradient.gradient.stops[0].color.r=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].color.g;
v .data.draw_text_gradient.gradient.stops[0].color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].color.g\n"; }
v .data.draw_text_gradient.gradient.stops[0].color.g=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].color.b;
v .data.draw_text_gradient.gradient.stops[0].color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].color.b\n"; }
v .data.draw_text_gradient.gradient.stops[0].color.b=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].color.a;
v .data.draw_text_gradient.gradient.stops[0].color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].color.a\n"; }
v .data.draw_text_gradient.gradient.stops[0].color.a=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].position.value;
v .data.draw_text_gradient.gradient.stops[0].position.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].position.value\n"; }
v .data.draw_text_gradient.gradient.stops[0].position.value=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].position.unit;
v .data.draw_text_gradient.gradient.stops[0].position.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].position.unit\n"; }
v .data.draw_text_gradient.gradient.stops[0].position.unit=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].position.linear_percent;
v .data.draw_text_gradient.gradient.stops[0].position.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].position.linear_percent\n"; }
v .data.draw_text_gradient.gradient.stops[0].position.linear_percent=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].position.linear_em;
v .data.draw_text_gradient.gradient.stops[0].position.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].position.linear_em\n"; }
v .data.draw_text_gradient.gradient.stops[0].position.linear_em=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].position.linear_rem;
v .data.draw_text_gradient.gradient.stops[0].position.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].position.linear_rem\n"; }
v .data.draw_text_gradient.gradient.stops[0].position.linear_rem=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].position.linear_vw;
v .data.draw_text_gradient.gradient.stops[0].position.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].position.linear_vw\n"; }
v .data.draw_text_gradient.gradient.stops[0].position.linear_vw=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].position.linear_vh;
v .data.draw_text_gradient.gradient.stops[0].position.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].position.linear_vh\n"; }
v .data.draw_text_gradient.gradient.stops[0].position.linear_vh=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].offset;
v .data.draw_text_gradient.gradient.stops[0].offset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].offset\n"; }
v .data.draw_text_gradient.gradient.stops[0].offset=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stops[0].has_position;
v .data.draw_text_gradient.gradient.stops[0].has_position=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stops[0].has_position\n"; }
v .data.draw_text_gradient.gradient.stops[0].has_position=old;
}
{
auto old=v .data.draw_text_gradient.gradient.stop_count;
v .data.draw_text_gradient.gradient.stop_count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.gradient.stop_count\n"; }
v .data.draw_text_gradient.gradient.stop_count=old;
}
{
auto old=v .data.draw_text_gradient.opacity;
v .data.draw_text_gradient.opacity=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_gradient.opacity\n"; }
v .data.draw_text_gradient.opacity=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_DRAW_TEXT_SHADOW;
char storage22[]="a"; v .data.draw_text_shadow.text=storage22;
const auto original=dump(v);
{
auto old=v .data.draw_text_shadow.text[0];
v .data.draw_text_shadow.text[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.text[0]\n"; }
v .data.draw_text_shadow.text[0]=old;
}
{
auto old=v .data.draw_text_shadow.rect.x;
v .data.draw_text_shadow.rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.rect.x\n"; }
v .data.draw_text_shadow.rect.x=old;
}
{
auto old=v .data.draw_text_shadow.rect.y;
v .data.draw_text_shadow.rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.rect.y\n"; }
v .data.draw_text_shadow.rect.y=old;
}
{
auto old=v .data.draw_text_shadow.rect.width;
v .data.draw_text_shadow.rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.rect.width\n"; }
v .data.draw_text_shadow.rect.width=old;
}
{
auto old=v .data.draw_text_shadow.rect.height;
v .data.draw_text_shadow.rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.rect.height\n"; }
v .data.draw_text_shadow.rect.height=old;
}
{
auto old=v .data.draw_text_shadow.font_family[0];
v .data.draw_text_shadow.font_family[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.font_family[0]\n"; }
v .data.draw_text_shadow.font_family[0]=old;
}
{
auto old=v .data.draw_text_shadow.font_size;
v .data.draw_text_shadow.font_size=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.font_size\n"; }
v .data.draw_text_shadow.font_size=old;
}
{
auto old=v .data.draw_text_shadow.font_weight;
v .data.draw_text_shadow.font_weight=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.font_weight\n"; }
v .data.draw_text_shadow.font_weight=old;
}
{
auto old=v .data.draw_text_shadow.text_align;
v .data.draw_text_shadow.text_align=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.text_align\n"; }
v .data.draw_text_shadow.text_align=old;
}
{
auto old=v .data.draw_text_shadow.shadow.offset_x;
v .data.draw_text_shadow.shadow.offset_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.shadow.offset_x\n"; }
v .data.draw_text_shadow.shadow.offset_x=old;
}
{
auto old=v .data.draw_text_shadow.shadow.offset_y;
v .data.draw_text_shadow.shadow.offset_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.shadow.offset_y\n"; }
v .data.draw_text_shadow.shadow.offset_y=old;
}
{
auto old=v .data.draw_text_shadow.shadow.blur_radius;
v .data.draw_text_shadow.shadow.blur_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.shadow.blur_radius\n"; }
v .data.draw_text_shadow.shadow.blur_radius=old;
}
{
auto old=v .data.draw_text_shadow.shadow.spread_radius;
v .data.draw_text_shadow.shadow.spread_radius=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.shadow.spread_radius\n"; }
v .data.draw_text_shadow.shadow.spread_radius=old;
}
{
auto old=v .data.draw_text_shadow.shadow.color.r;
v .data.draw_text_shadow.shadow.color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.shadow.color.r\n"; }
v .data.draw_text_shadow.shadow.color.r=old;
}
{
auto old=v .data.draw_text_shadow.shadow.color.g;
v .data.draw_text_shadow.shadow.color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.shadow.color.g\n"; }
v .data.draw_text_shadow.shadow.color.g=old;
}
{
auto old=v .data.draw_text_shadow.shadow.color.b;
v .data.draw_text_shadow.shadow.color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.shadow.color.b\n"; }
v .data.draw_text_shadow.shadow.color.b=old;
}
{
auto old=v .data.draw_text_shadow.shadow.color.a;
v .data.draw_text_shadow.shadow.color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.shadow.color.a\n"; }
v .data.draw_text_shadow.shadow.color.a=old;
}
{
auto old=v .data.draw_text_shadow.shadow.inset;
v .data.draw_text_shadow.shadow.inset=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.shadow.inset\n"; }
v .data.draw_text_shadow.shadow.inset=old;
}
{
auto old=v .data.draw_text_shadow.wrap;
v .data.draw_text_shadow.wrap=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_text_shadow.wrap\n"; }
v .data.draw_text_shadow.wrap=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_PUSH_CLIP;
const auto original=dump(v);
{
auto old=v .data.push_clip.rect.x;
v .data.push_clip.rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.rect.x\n"; }
v .data.push_clip.rect.x=old;
}
{
auto old=v .data.push_clip.rect.y;
v .data.push_clip.rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.rect.y\n"; }
v .data.push_clip.rect.y=old;
}
{
auto old=v .data.push_clip.rect.width;
v .data.push_clip.rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.rect.width\n"; }
v .data.push_clip.rect.width=old;
}
{
auto old=v .data.push_clip.rect.height;
v .data.push_clip.rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.rect.height\n"; }
v .data.push_clip.rect.height=old;
}
{
auto old=v .data.push_clip.radii.top_left_x;
v .data.push_clip.radii.top_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.radii.top_left_x\n"; }
v .data.push_clip.radii.top_left_x=old;
}
{
auto old=v .data.push_clip.radii.top_left_y;
v .data.push_clip.radii.top_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.radii.top_left_y\n"; }
v .data.push_clip.radii.top_left_y=old;
}
{
auto old=v .data.push_clip.radii.top_right_x;
v .data.push_clip.radii.top_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.radii.top_right_x\n"; }
v .data.push_clip.radii.top_right_x=old;
}
{
auto old=v .data.push_clip.radii.top_right_y;
v .data.push_clip.radii.top_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.radii.top_right_y\n"; }
v .data.push_clip.radii.top_right_y=old;
}
{
auto old=v .data.push_clip.radii.bottom_right_x;
v .data.push_clip.radii.bottom_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.radii.bottom_right_x\n"; }
v .data.push_clip.radii.bottom_right_x=old;
}
{
auto old=v .data.push_clip.radii.bottom_right_y;
v .data.push_clip.radii.bottom_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.radii.bottom_right_y\n"; }
v .data.push_clip.radii.bottom_right_y=old;
}
{
auto old=v .data.push_clip.radii.bottom_left_x;
v .data.push_clip.radii.bottom_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.radii.bottom_left_x\n"; }
v .data.push_clip.radii.bottom_left_x=old;
}
{
auto old=v .data.push_clip.radii.bottom_left_y;
v .data.push_clip.radii.bottom_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_clip.radii.bottom_left_y\n"; }
v .data.push_clip.radii.bottom_left_y=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_FILL_BORDER_SIDE;
const auto original=dump(v);
{
auto old=v .data.fill_border_side.outer_rect.x;
v .data.fill_border_side.outer_rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_rect.x\n"; }
v .data.fill_border_side.outer_rect.x=old;
}
{
auto old=v .data.fill_border_side.outer_rect.y;
v .data.fill_border_side.outer_rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_rect.y\n"; }
v .data.fill_border_side.outer_rect.y=old;
}
{
auto old=v .data.fill_border_side.outer_rect.width;
v .data.fill_border_side.outer_rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_rect.width\n"; }
v .data.fill_border_side.outer_rect.width=old;
}
{
auto old=v .data.fill_border_side.outer_rect.height;
v .data.fill_border_side.outer_rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_rect.height\n"; }
v .data.fill_border_side.outer_rect.height=old;
}
{
auto old=v .data.fill_border_side.outer_radii.top_left_x;
v .data.fill_border_side.outer_radii.top_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_radii.top_left_x\n"; }
v .data.fill_border_side.outer_radii.top_left_x=old;
}
{
auto old=v .data.fill_border_side.outer_radii.top_left_y;
v .data.fill_border_side.outer_radii.top_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_radii.top_left_y\n"; }
v .data.fill_border_side.outer_radii.top_left_y=old;
}
{
auto old=v .data.fill_border_side.outer_radii.top_right_x;
v .data.fill_border_side.outer_radii.top_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_radii.top_right_x\n"; }
v .data.fill_border_side.outer_radii.top_right_x=old;
}
{
auto old=v .data.fill_border_side.outer_radii.top_right_y;
v .data.fill_border_side.outer_radii.top_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_radii.top_right_y\n"; }
v .data.fill_border_side.outer_radii.top_right_y=old;
}
{
auto old=v .data.fill_border_side.outer_radii.bottom_right_x;
v .data.fill_border_side.outer_radii.bottom_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_radii.bottom_right_x\n"; }
v .data.fill_border_side.outer_radii.bottom_right_x=old;
}
{
auto old=v .data.fill_border_side.outer_radii.bottom_right_y;
v .data.fill_border_side.outer_radii.bottom_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_radii.bottom_right_y\n"; }
v .data.fill_border_side.outer_radii.bottom_right_y=old;
}
{
auto old=v .data.fill_border_side.outer_radii.bottom_left_x;
v .data.fill_border_side.outer_radii.bottom_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_radii.bottom_left_x\n"; }
v .data.fill_border_side.outer_radii.bottom_left_x=old;
}
{
auto old=v .data.fill_border_side.outer_radii.bottom_left_y;
v .data.fill_border_side.outer_radii.bottom_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.outer_radii.bottom_left_y\n"; }
v .data.fill_border_side.outer_radii.bottom_left_y=old;
}
{
auto old=v .data.fill_border_side.inner_rect.x;
v .data.fill_border_side.inner_rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_rect.x\n"; }
v .data.fill_border_side.inner_rect.x=old;
}
{
auto old=v .data.fill_border_side.inner_rect.y;
v .data.fill_border_side.inner_rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_rect.y\n"; }
v .data.fill_border_side.inner_rect.y=old;
}
{
auto old=v .data.fill_border_side.inner_rect.width;
v .data.fill_border_side.inner_rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_rect.width\n"; }
v .data.fill_border_side.inner_rect.width=old;
}
{
auto old=v .data.fill_border_side.inner_rect.height;
v .data.fill_border_side.inner_rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_rect.height\n"; }
v .data.fill_border_side.inner_rect.height=old;
}
{
auto old=v .data.fill_border_side.inner_radii.top_left_x;
v .data.fill_border_side.inner_radii.top_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_radii.top_left_x\n"; }
v .data.fill_border_side.inner_radii.top_left_x=old;
}
{
auto old=v .data.fill_border_side.inner_radii.top_left_y;
v .data.fill_border_side.inner_radii.top_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_radii.top_left_y\n"; }
v .data.fill_border_side.inner_radii.top_left_y=old;
}
{
auto old=v .data.fill_border_side.inner_radii.top_right_x;
v .data.fill_border_side.inner_radii.top_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_radii.top_right_x\n"; }
v .data.fill_border_side.inner_radii.top_right_x=old;
}
{
auto old=v .data.fill_border_side.inner_radii.top_right_y;
v .data.fill_border_side.inner_radii.top_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_radii.top_right_y\n"; }
v .data.fill_border_side.inner_radii.top_right_y=old;
}
{
auto old=v .data.fill_border_side.inner_radii.bottom_right_x;
v .data.fill_border_side.inner_radii.bottom_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_radii.bottom_right_x\n"; }
v .data.fill_border_side.inner_radii.bottom_right_x=old;
}
{
auto old=v .data.fill_border_side.inner_radii.bottom_right_y;
v .data.fill_border_side.inner_radii.bottom_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_radii.bottom_right_y\n"; }
v .data.fill_border_side.inner_radii.bottom_right_y=old;
}
{
auto old=v .data.fill_border_side.inner_radii.bottom_left_x;
v .data.fill_border_side.inner_radii.bottom_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_radii.bottom_left_x\n"; }
v .data.fill_border_side.inner_radii.bottom_left_x=old;
}
{
auto old=v .data.fill_border_side.inner_radii.bottom_left_y;
v .data.fill_border_side.inner_radii.bottom_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.inner_radii.bottom_left_y\n"; }
v .data.fill_border_side.inner_radii.bottom_left_y=old;
}
{
auto old=v .data.fill_border_side.side;
v .data.fill_border_side.side=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.side\n"; }
v .data.fill_border_side.side=old;
}
{
auto old=v .data.fill_border_side.style;
v .data.fill_border_side.style=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.style\n"; }
v .data.fill_border_side.style=old;
}
{
auto old=v .data.fill_border_side.color.r;
v .data.fill_border_side.color.r=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.color.r\n"; }
v .data.fill_border_side.color.r=old;
}
{
auto old=v .data.fill_border_side.color.g;
v .data.fill_border_side.color.g=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.color.g\n"; }
v .data.fill_border_side.color.g=old;
}
{
auto old=v .data.fill_border_side.color.b;
v .data.fill_border_side.color.b=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.color.b\n"; }
v .data.fill_border_side.color.b=old;
}
{
auto old=v .data.fill_border_side.color.a;
v .data.fill_border_side.color.a=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.fill_border_side.color.a\n"; }
v .data.fill_border_side.color.a=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_DRAW_BACKGROUND_IMAGE;
char storage23[]="a"; v .data.draw_background_image.source=storage23;
const auto original=dump(v);
{
auto old=v .data.draw_background_image.source[0];
v .data.draw_background_image.source[0]=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.source[0]\n"; }
v .data.draw_background_image.source[0]=old;
}
{
auto old=v .data.draw_background_image.paint_rect.x;
v .data.draw_background_image.paint_rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.paint_rect.x\n"; }
v .data.draw_background_image.paint_rect.x=old;
}
{
auto old=v .data.draw_background_image.paint_rect.y;
v .data.draw_background_image.paint_rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.paint_rect.y\n"; }
v .data.draw_background_image.paint_rect.y=old;
}
{
auto old=v .data.draw_background_image.paint_rect.width;
v .data.draw_background_image.paint_rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.paint_rect.width\n"; }
v .data.draw_background_image.paint_rect.width=old;
}
{
auto old=v .data.draw_background_image.paint_rect.height;
v .data.draw_background_image.paint_rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.paint_rect.height\n"; }
v .data.draw_background_image.paint_rect.height=old;
}
{
auto old=v .data.draw_background_image.position_x.value;
v .data.draw_background_image.position_x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_x.value\n"; }
v .data.draw_background_image.position_x.value=old;
}
{
auto old=v .data.draw_background_image.position_x.unit;
v .data.draw_background_image.position_x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_x.unit\n"; }
v .data.draw_background_image.position_x.unit=old;
}
{
auto old=v .data.draw_background_image.position_x.linear_percent;
v .data.draw_background_image.position_x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_x.linear_percent\n"; }
v .data.draw_background_image.position_x.linear_percent=old;
}
{
auto old=v .data.draw_background_image.position_x.linear_em;
v .data.draw_background_image.position_x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_x.linear_em\n"; }
v .data.draw_background_image.position_x.linear_em=old;
}
{
auto old=v .data.draw_background_image.position_x.linear_rem;
v .data.draw_background_image.position_x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_x.linear_rem\n"; }
v .data.draw_background_image.position_x.linear_rem=old;
}
{
auto old=v .data.draw_background_image.position_x.linear_vw;
v .data.draw_background_image.position_x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_x.linear_vw\n"; }
v .data.draw_background_image.position_x.linear_vw=old;
}
{
auto old=v .data.draw_background_image.position_x.linear_vh;
v .data.draw_background_image.position_x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_x.linear_vh\n"; }
v .data.draw_background_image.position_x.linear_vh=old;
}
{
auto old=v .data.draw_background_image.position_y.value;
v .data.draw_background_image.position_y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_y.value\n"; }
v .data.draw_background_image.position_y.value=old;
}
{
auto old=v .data.draw_background_image.position_y.unit;
v .data.draw_background_image.position_y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_y.unit\n"; }
v .data.draw_background_image.position_y.unit=old;
}
{
auto old=v .data.draw_background_image.position_y.linear_percent;
v .data.draw_background_image.position_y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_y.linear_percent\n"; }
v .data.draw_background_image.position_y.linear_percent=old;
}
{
auto old=v .data.draw_background_image.position_y.linear_em;
v .data.draw_background_image.position_y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_y.linear_em\n"; }
v .data.draw_background_image.position_y.linear_em=old;
}
{
auto old=v .data.draw_background_image.position_y.linear_rem;
v .data.draw_background_image.position_y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_y.linear_rem\n"; }
v .data.draw_background_image.position_y.linear_rem=old;
}
{
auto old=v .data.draw_background_image.position_y.linear_vw;
v .data.draw_background_image.position_y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_y.linear_vw\n"; }
v .data.draw_background_image.position_y.linear_vw=old;
}
{
auto old=v .data.draw_background_image.position_y.linear_vh;
v .data.draw_background_image.position_y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.position_y.linear_vh\n"; }
v .data.draw_background_image.position_y.linear_vh=old;
}
{
auto old=v .data.draw_background_image.size_kind;
v .data.draw_background_image.size_kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_kind\n"; }
v .data.draw_background_image.size_kind=old;
}
{
auto old=v .data.draw_background_image.size_x.value;
v .data.draw_background_image.size_x.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_x.value\n"; }
v .data.draw_background_image.size_x.value=old;
}
{
auto old=v .data.draw_background_image.size_x.unit;
v .data.draw_background_image.size_x.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_x.unit\n"; }
v .data.draw_background_image.size_x.unit=old;
}
{
auto old=v .data.draw_background_image.size_x.linear_percent;
v .data.draw_background_image.size_x.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_x.linear_percent\n"; }
v .data.draw_background_image.size_x.linear_percent=old;
}
{
auto old=v .data.draw_background_image.size_x.linear_em;
v .data.draw_background_image.size_x.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_x.linear_em\n"; }
v .data.draw_background_image.size_x.linear_em=old;
}
{
auto old=v .data.draw_background_image.size_x.linear_rem;
v .data.draw_background_image.size_x.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_x.linear_rem\n"; }
v .data.draw_background_image.size_x.linear_rem=old;
}
{
auto old=v .data.draw_background_image.size_x.linear_vw;
v .data.draw_background_image.size_x.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_x.linear_vw\n"; }
v .data.draw_background_image.size_x.linear_vw=old;
}
{
auto old=v .data.draw_background_image.size_x.linear_vh;
v .data.draw_background_image.size_x.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_x.linear_vh\n"; }
v .data.draw_background_image.size_x.linear_vh=old;
}
{
auto old=v .data.draw_background_image.size_y.value;
v .data.draw_background_image.size_y.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_y.value\n"; }
v .data.draw_background_image.size_y.value=old;
}
{
auto old=v .data.draw_background_image.size_y.unit;
v .data.draw_background_image.size_y.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_y.unit\n"; }
v .data.draw_background_image.size_y.unit=old;
}
{
auto old=v .data.draw_background_image.size_y.linear_percent;
v .data.draw_background_image.size_y.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_y.linear_percent\n"; }
v .data.draw_background_image.size_y.linear_percent=old;
}
{
auto old=v .data.draw_background_image.size_y.linear_em;
v .data.draw_background_image.size_y.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_y.linear_em\n"; }
v .data.draw_background_image.size_y.linear_em=old;
}
{
auto old=v .data.draw_background_image.size_y.linear_rem;
v .data.draw_background_image.size_y.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_y.linear_rem\n"; }
v .data.draw_background_image.size_y.linear_rem=old;
}
{
auto old=v .data.draw_background_image.size_y.linear_vw;
v .data.draw_background_image.size_y.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_y.linear_vw\n"; }
v .data.draw_background_image.size_y.linear_vw=old;
}
{
auto old=v .data.draw_background_image.size_y.linear_vh;
v .data.draw_background_image.size_y.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.size_y.linear_vh\n"; }
v .data.draw_background_image.size_y.linear_vh=old;
}
{
auto old=v .data.draw_background_image.repeat_x;
v .data.draw_background_image.repeat_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.repeat_x\n"; }
v .data.draw_background_image.repeat_x=old;
}
{
auto old=v .data.draw_background_image.repeat_y;
v .data.draw_background_image.repeat_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.repeat_y\n"; }
v .data.draw_background_image.repeat_y=old;
}
{
auto old=v .data.draw_background_image.opacity;
v .data.draw_background_image.opacity=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.opacity\n"; }
v .data.draw_background_image.opacity=old;
}
{
auto old=v .data.draw_background_image.source_rect.x;
v .data.draw_background_image.source_rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.source_rect.x\n"; }
v .data.draw_background_image.source_rect.x=old;
}
{
auto old=v .data.draw_background_image.source_rect.y;
v .data.draw_background_image.source_rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.source_rect.y\n"; }
v .data.draw_background_image.source_rect.y=old;
}
{
auto old=v .data.draw_background_image.source_rect.width;
v .data.draw_background_image.source_rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.source_rect.width\n"; }
v .data.draw_background_image.source_rect.width=old;
}
{
auto old=v .data.draw_background_image.source_rect.height;
v .data.draw_background_image.source_rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.source_rect.height\n"; }
v .data.draw_background_image.source_rect.height=old;
}
{
auto old=v .data.draw_background_image.destination_rect.x;
v .data.draw_background_image.destination_rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.destination_rect.x\n"; }
v .data.draw_background_image.destination_rect.x=old;
}
{
auto old=v .data.draw_background_image.destination_rect.y;
v .data.draw_background_image.destination_rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.destination_rect.y\n"; }
v .data.draw_background_image.destination_rect.y=old;
}
{
auto old=v .data.draw_background_image.destination_rect.width;
v .data.draw_background_image.destination_rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.destination_rect.width\n"; }
v .data.draw_background_image.destination_rect.width=old;
}
{
auto old=v .data.draw_background_image.destination_rect.height;
v .data.draw_background_image.destination_rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.destination_rect.height\n"; }
v .data.draw_background_image.destination_rect.height=old;
}
{
auto old=v .data.draw_background_image.has_resolved_geometry;
v .data.draw_background_image.has_resolved_geometry=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_background_image.has_resolved_geometry\n"; }
v .data.draw_background_image.has_resolved_geometry=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_APPLY_BACKDROP_FILTER;
aether_backdrop_filter storage24[1]{}; v .data.apply_backdrop_filter.filters.items=storage24;
v .data.apply_backdrop_filter.filters.count=1;
const auto original=dump(v);
{
auto old=v .data.apply_backdrop_filter.rect.x;
v .data.apply_backdrop_filter.rect.x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.rect.x\n"; }
v .data.apply_backdrop_filter.rect.x=old;
}
{
auto old=v .data.apply_backdrop_filter.rect.y;
v .data.apply_backdrop_filter.rect.y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.rect.y\n"; }
v .data.apply_backdrop_filter.rect.y=old;
}
{
auto old=v .data.apply_backdrop_filter.rect.width;
v .data.apply_backdrop_filter.rect.width=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.rect.width\n"; }
v .data.apply_backdrop_filter.rect.width=old;
}
{
auto old=v .data.apply_backdrop_filter.rect.height;
v .data.apply_backdrop_filter.rect.height=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.rect.height\n"; }
v .data.apply_backdrop_filter.rect.height=old;
}
{
auto old=v .data.apply_backdrop_filter.radii.top_left_x;
v .data.apply_backdrop_filter.radii.top_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.radii.top_left_x\n"; }
v .data.apply_backdrop_filter.radii.top_left_x=old;
}
{
auto old=v .data.apply_backdrop_filter.radii.top_left_y;
v .data.apply_backdrop_filter.radii.top_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.radii.top_left_y\n"; }
v .data.apply_backdrop_filter.radii.top_left_y=old;
}
{
auto old=v .data.apply_backdrop_filter.radii.top_right_x;
v .data.apply_backdrop_filter.radii.top_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.radii.top_right_x\n"; }
v .data.apply_backdrop_filter.radii.top_right_x=old;
}
{
auto old=v .data.apply_backdrop_filter.radii.top_right_y;
v .data.apply_backdrop_filter.radii.top_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.radii.top_right_y\n"; }
v .data.apply_backdrop_filter.radii.top_right_y=old;
}
{
auto old=v .data.apply_backdrop_filter.radii.bottom_right_x;
v .data.apply_backdrop_filter.radii.bottom_right_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.radii.bottom_right_x\n"; }
v .data.apply_backdrop_filter.radii.bottom_right_x=old;
}
{
auto old=v .data.apply_backdrop_filter.radii.bottom_right_y;
v .data.apply_backdrop_filter.radii.bottom_right_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.radii.bottom_right_y\n"; }
v .data.apply_backdrop_filter.radii.bottom_right_y=old;
}
{
auto old=v .data.apply_backdrop_filter.radii.bottom_left_x;
v .data.apply_backdrop_filter.radii.bottom_left_x=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.radii.bottom_left_x\n"; }
v .data.apply_backdrop_filter.radii.bottom_left_x=old;
}
{
auto old=v .data.apply_backdrop_filter.radii.bottom_left_y;
v .data.apply_backdrop_filter.radii.bottom_left_y=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.radii.bottom_left_y\n"; }
v .data.apply_backdrop_filter.radii.bottom_left_y=old;
}
{
auto old=v .data.apply_backdrop_filter.filters.items[0].kind;
v .data.apply_backdrop_filter.filters.items[0].kind=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.filters.items[0].kind\n"; }
v .data.apply_backdrop_filter.filters.items[0].kind=old;
}
{
auto old=v .data.apply_backdrop_filter.filters.items[0].length_amount.value;
v .data.apply_backdrop_filter.filters.items[0].length_amount.value=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.filters.items[0].length_amount.value\n"; }
v .data.apply_backdrop_filter.filters.items[0].length_amount.value=old;
}
{
auto old=v .data.apply_backdrop_filter.filters.items[0].length_amount.unit;
v .data.apply_backdrop_filter.filters.items[0].length_amount.unit=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.filters.items[0].length_amount.unit\n"; }
v .data.apply_backdrop_filter.filters.items[0].length_amount.unit=old;
}
{
auto old=v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_percent;
v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_percent=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_percent\n"; }
v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_percent=old;
}
{
auto old=v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_em;
v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_em=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_em\n"; }
v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_em=old;
}
{
auto old=v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_rem;
v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_rem=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_rem\n"; }
v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_rem=old;
}
{
auto old=v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_vw;
v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_vw=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_vw\n"; }
v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_vw=old;
}
{
auto old=v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_vh;
v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_vh=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_vh\n"; }
v .data.apply_backdrop_filter.filters.items[0].length_amount.linear_vh=old;
}
{
auto old=v .data.apply_backdrop_filter.filters.items[0].scalar_amount;
v .data.apply_backdrop_filter.filters.items[0].scalar_amount=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.filters.items[0].scalar_amount\n"; }
v .data.apply_backdrop_filter.filters.items[0].scalar_amount=old;
}
{
auto old=v .data.apply_backdrop_filter.filters.count;
v .data.apply_backdrop_filter.filters.count=0;
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.apply_backdrop_filter.filters.count\n"; }
v .data.apply_backdrop_filter.filters.count=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_PUSH_TRANSFORM;
const auto original=dump(v);
{
auto old=v .data.push_transform.matrix.m11;
v .data.push_transform.matrix.m11=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_transform.matrix.m11\n"; }
v .data.push_transform.matrix.m11=old;
}
{
auto old=v .data.push_transform.matrix.m12;
v .data.push_transform.matrix.m12=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_transform.matrix.m12\n"; }
v .data.push_transform.matrix.m12=old;
}
{
auto old=v .data.push_transform.matrix.m21;
v .data.push_transform.matrix.m21=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_transform.matrix.m21\n"; }
v .data.push_transform.matrix.m21=old;
}
{
auto old=v .data.push_transform.matrix.m22;
v .data.push_transform.matrix.m22=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_transform.matrix.m22\n"; }
v .data.push_transform.matrix.m22=old;
}
{
auto old=v .data.push_transform.matrix.dx;
v .data.push_transform.matrix.dx=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_transform.matrix.dx\n"; }
v .data.push_transform.matrix.dx=old;
}
{
auto old=v .data.push_transform.matrix.dy;
v .data.push_transform.matrix.dy=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_transform.matrix.dy\n"; }
v .data.push_transform.matrix.dy=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_PUSH_SCROLL_OFFSET;
const auto original=dump(v);
{
auto old=v .data.push_scroll_offset.scroll_container_id;
v .data.push_scroll_offset.scroll_container_id=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.push_scroll_offset.scroll_container_id\n"; }
v .data.push_scroll_offset.scroll_container_id=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_DRAW_SCROLLBAR_CHROME;
const auto original=dump(v);
{
auto old=v .data.draw_scrollbar_chrome.scroll_container_id;
v .data.draw_scrollbar_chrome.scroll_container_id=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.draw_scrollbar_chrome.scroll_container_id\n"; }
v .data.draw_scrollbar_chrome.scroll_container_id=old;
}
}
{
aether_paint_command_t v{};
v.tag=AETHER_PAINT_BEGIN_PAINT_CHUNK;
const auto original=dump(v);
{
auto old=v .data.paint_chunk.style_node_id;
v .data.paint_chunk.style_node_id=static_cast<decltype(old)>(old+1);
++checks; if(dump(v)==original) { ++failures; std::cerr << "missed v .data.paint_chunk.style_node_id\n"; }
v .data.paint_chunk.style_node_id=old;
}
}
std::cout << "leaf mutation checks=" << checks << " failures=" << failures << "\n"; return failures?1:0;
}
