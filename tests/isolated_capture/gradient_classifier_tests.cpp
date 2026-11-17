#include "../../native/src/rendering/software/software_renderer.cpp"
#include <cassert>
#include <iostream>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
int main(){
 size_t fast=0,slow=0;
 for(auto dir: {std::pair<float,float>{1,0},{-1,0},{0,1},{0,-1},{.6f,.8f},{-.6f,-.8f}})for(int scenario=0;scenario<6;++scenario){
 p::LinearGradientFill g;g.rect={1.25f,2.5f,16.5f,12.25f};g.gradient_rect=scenario==2?p::Rect{5,4,3,2}:g.rect;g.direction_x=dir.first;g.direction_y=dir.second;g.stops={{0,{1,0,0,1}},{1,{0,0,1,1}}};
 if(scenario==1)g.stops={{0,{1,0,0,1}},{.5f,{1,0,0,1}},{.5f,{0,1,0,1}},{std::nextafter(.5f,1.f),{0,0,1,1}},{1,{0,0,1,1}}};
 if(scenario==3){g.stops.clear();for(int i=0;i<64;++i)g.stops.push_back({i/63.f,{1,0,0,1}});}
 if(scenario==4)g.gradient_rect={8,5,1e-20f,1e-20f};
 if(scenario==5)g.gradient_rect={-65536,-65536,65536,65536};
 s::Draw d{};d.original=s::rect(g.rect);d.rect=s::intersect(d.original,{2.125,3.375,15.625,13.875});d.gradient_rect=s::rect(g.gradient_rect);d.gradient=&g;d.radius=scenario==0?3:0;d.clip={{2.125,3.375,15.625,13.875},scenario==0?2.:0.};
 for(int y=0;y<20;++y)for(int x=0;x<24;++x){auto b=s::intersect(d.rect,{double(x),double(y),x+1.,y+1.});if(s::empty(b))continue;
 bool quick=s::gradient_fast(d,b);(quick?fast:slow)++;if(!quick)continue;
 // Long-double oracle deliberately uses original float fields and scans ALL stops.
 long double lo=1e99L,hi=-1e99L;for(double xx:{b.l,b.r})for(double yy:{b.t,b.b}){
 long double width=g.gradient_rect.width,height=g.gradient_rect.height,dx=g.direction_x,dy=g.direction_y;
 long double z=.5L+((xx-(static_cast<long double>(g.gradient_rect.x)+width/2))*dx+(yy-(static_cast<long double>(g.gradient_rect.y)+height/2))*dy)/(width*std::abs(dx)+height*std::abs(dy));lo=std::min(lo,z);hi=std::max(hi,z);
 auto covered=[&](s::Rect r,double radius){long double cx=std::clamp(xx,r.l+radius,r.r-radius),cy=std::clamp(yy,r.t+radius,r.b-radius);return (xx-cx)*(xx-cx)+(yy-cy)*(yy-cy)<=radius*radius;};assert(covered(d.original,d.radius));assert(!d.clip.radius||covered(d.clip.original,d.clip.radius));}
 for(auto stop:g.stops)assert(stop.offset<lo||stop.offset>hi);
 }
 }
 // Explicit boundary equality and adjacent floating values must not admit a
 // hard step. These expectations are declared, not derived from production.
 p::LinearGradientFill edge;edge.rect=edge.gradient_rect={0,0,16,12};edge.direction_x=1;edge.direction_y=0;edge.stops={{0,{1,0,0,1}},{.5f,{1,0,0,1}},{.5f,{0,0,1,1}},{1,{0,0,1,1}}};
 s::Draw boundary{};boundary.original=boundary.rect=boundary.gradient_rect=s::rect(edge.rect);boundary.gradient=&edge;
 assert(!s::gradient_fast(boundary,{7,2,8,3}));assert(!s::gradient_fast(boundary,{8,2,9,3}));assert(!s::gradient_fast(boundary,{7.5,2,8.5,3}));
 assert(!s::gradient_fast(boundary,{7,2,std::nextafter(8.,0.),3}));assert(!s::gradient_fast(boundary,{std::nextafter(8.,9.),2,9,3}));
 assert(s::gradient_fast(boundary,{6,2,7,3}));assert(s::gradient_fast(boundary,{9,2,10,3}));
 edge.stops.insert(edge.stops.begin()+3,{std::nextafter(.5f,1.f),{0,1,0,1}});assert(!s::gradient_fast(boundary,{7.9999999,2,8.000001,3}));
 assert(fast&&slow);std::cout<<"classifier independent fast="<<fast<<" slow="<<slow<<" PASS\n";
}
