#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/native_viewport_capture.h"
#include "../src/layout/layout_internal.h"
#include "../src/layout/scrollbar_geometry.h"
#include <cassert>
#include <limits>
#include <iostream>
using namespace aether::rendering;
using E=text::truetype::HtmlTextError;
static bool same(aether_rectf a,aether_rectf b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
static auto geometry(unsigned w,unsigned h,double rx,double ry,unsigned x=0,unsigned y=0){auto r=resolve_native_viewport_chrome(w,h,rx,ry,{x,y});assert(std::holds_alternative<aether_scrollbar_geometry_t>(r));return std::get<aether_scrollbar_geometry_t>(r);}
static void error(unsigned w,unsigned h,double rx,double ry,unsigned x,unsigned y,E e){auto r=resolve_native_viewport_chrome(w,h,rx,ry,{x,y});assert(std::holds_alternative<E>(r)&&std::get<E>(r)==e);}
int main(){
 unsigned cases=0;
 // Differential gate: exact legacy arithmetic, not a second visual oracle.
 for(auto w:{8u,9u,24u,128u,2048u})for(auto h:{8u,17u,128u,2048u})
 for(auto rx:{0u,1u,24u,65536u})for(auto ry:{0u,1u,24u,65536u})for(auto end:{false,true}){
  aether_layout_box_s box;box.style.overflow_x=box.style.overflow_y=AETHER_OVERFLOW_AUTO;
  box.padding_box={0,0,float(w),float(h)};box.scroll.scrollable_overflow_x=rx;box.scroll.scrollable_overflow_y=ry;
  const auto x=end?rx:0,y=end?ry:0;auto a=geometry(w,h,rx,ry,x,y);
  auto b=aether_resolve_scrollbar_geometry(box,{float(x),float(y)});
  assert(a.vertical_visible==b.vertical_visible&&a.horizontal_visible==b.horizontal_visible);
  assert(same(a.vertical_track,b.vertical_track)&&same(a.horizontal_track,b.horizontal_track));
  assert(same(a.vertical_thumb,b.vertical_thumb)&&same(a.horizontal_thumb,b.horizontal_thumb)&&same(a.corner,b.corner));++cases;
 }
 // Independent numeric anchors, including two-axis reservation and midpoint.
 auto a=geometry(100,80,94,74,47,37);
 assert(same(a.horizontal_track,{0,74,94,4})&&same(a.vertical_track,{94,0,4,74}));
 assert(same(a.horizontal_thumb,{23.5f,74,47,4})&&same(a.vertical_thumb,{94,18.5f,4,37}));
 assert(same(a.corner,{94,74,4,4}));
 auto small=geometry(8,8,65536,65536,65536,65536);
 assert(same(small.horizontal_thumb,{0,2,2,4})&&same(small.vertical_thumb,{2,0,4,2}));
 auto none=geometry(7,7,0,0);assert(!none.horizontal_visible&&!none.vertical_visible&&same(none.corner,{}));
 unsigned negatives=0;
 for(auto v:{-1.,0.5,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}){
  error(8,8,v,0,0,0,E::unsupported);error(8,8,0,v,0,0,E::unsupported);negatives+=2;
 }
 error(7,8,1,0,0,0,E::unsupported);error(8,7,0,1,0,0,E::unsupported);
 error(8,8,65537,0,0,0,E::limit_exceeded);error(8,8,0,65537,0,0,E::limit_exceeded);
 error(8,8,1,0,2,0,E::invalid_input);error(8,8,0,1,0,2,E::invalid_input);
 error(8,8,0,0,1,0,E::invalid_input);error(8,8,0,0,0,1,E::invalid_input);
 error(8,8,65536,0,65537,0,E::limit_exceeded);error(8,8,0,65536,0,65537,E::limit_exceeded);
 error(0,8,0,0,0,0,E::invalid_input);error(8,0,0,0,0,0,E::invalid_input);
 error(65537,8,0,0,0,0,E::limit_exceeded);error(8,65537,0,0,0,0,E::limit_exceeded);negatives+=14;
 std::cout<<"Q2g chrome gate PASS: "<<cases<<" legacy differentials, 3 independent geometry anchors, "<<negatives<<" invalid/boundary cases.\n";
}
