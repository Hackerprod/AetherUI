#ifdef NDEBUG
#error assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <cassert>
#include <iostream>
#include <limits>
namespace p=aether::rendering::preparation;
namespace r=aether::rendering;
int main(){
 r::FrameStore store;assert(store.resize({20,20,1})==r::FrameStatus::ok);
 auto raster=[&](p::Snapshot s){auto f=p::prepare(s);assert(std::holds_alternative<p::PreparedFrame>(f));assert(r::software::render(store,std::get<p::PreparedFrame>(f)).status==r::software::Status::ok);std::optional<r::Frame> image;assert(store.copy_confirmed(image)==r::FrameStatus::ok);return image->pixels;};
 const p::Fill box{{7,8,6,6},{},{1,0,0,1}};
 p::Snapshot translated{{p::PushClip{{2,2,12,12},{}},p::PushScroll{1,2.25,3.5},p::BeginOpacity{1,0,.5},box,p::EndOpacity{1},p::PopScroll{},p::PopClip{}}};
 p::Snapshot reference{{p::PushClip{{2,2,12,12},{}},p::BeginOpacity{1,0,.5},p::Fill{{4.75,4.5,6,6},{},{1,0,0,1}},p::EndOpacity{1},p::PopClip{}}};
 assert(raster(translated)==raster(reference));
 p::Snapshot nested{{p::PushScroll{1,2,3},p::PushClip{{3,4,14,14},{}},p::PushScroll{2,1,2},box,p::PopScroll{},p::Fill{{15,15,2,2},{},{0,1,0,1}},p::PopClip{},p::PopScroll{},p::Fill{{0,0,1,1},{},{0,0,1,1}}}};
 p::Snapshot flat{{p::PushClip{{1,1,14,14},{}},p::Fill{{4,3,6,6},{},{1,0,0,1}},p::Fill{{13,12,2,2},{},{0,1,0,1}},p::PopClip{},p::Fill{{0,0,1,1},{},{0,0,1,1}}}};
 assert(raster(nested)==raster(flat));
 for(auto bad:{p::Snapshot{{p::PopScroll{}}},p::Snapshot{{p::PushScroll{1,1,1}}},p::Snapshot{{p::PushScroll{1,-1,0},p::PopScroll{}}},p::Snapshot{{p::PushScroll{1,65536,0},p::PushScroll{2,1,0},p::PopScroll{},p::PopScroll{}}},p::Snapshot{{p::PushScroll{1,1,0},p::PushClip{{0,0,1,1},{}},p::PopScroll{},p::PopClip{}}},p::Snapshot{{p::PushClip{{0,0,1,1},{}},p::PushScroll{1,1,0},p::PopClip{},p::PopScroll{}}}})assert(std::holds_alternative<p::Error>(p::prepare(bad)));
 
 // The same translation must cover native border sampling, shadow and stroke.
 p::BorderGroup border; border.outer={7,7,10,10};border.inner={9,9,6,6};
 for(auto& side:border.sides)side={true,AETHER_BORDER_STYLE_DASHED,{0,0,1,1}};
 p::BorderGroup shifted=border;shifted.outer.x-=2;shifted.outer.y-=3;shifted.inner.x-=2;shifted.inner.y-=3;
 assert(raster({{p::PushScroll{1,2,3},border,p::PopScroll{}}})==raster({{shifted}}));
 p::Stroke stroke{{6,6,8,8},{},2,{1,0,1,1}}, shifted_stroke=stroke;shifted_stroke.rect.x-=2;shifted_stroke.rect.y-=3;
 assert(raster({{p::PushScroll{1,2,3},stroke,p::PopScroll{}}})==raster({{shifted_stroke}}));
 p::Snapshot infinite{{p::PushScroll{1,std::numeric_limits<double>::infinity(),0},p::PopScroll{}}};assert(std::holds_alternative<p::Error>(p::prepare(infinite)));
 
 auto valid=p::prepare(translated);assert(std::holds_alternative<p::PreparedFrame>(valid));
 struct Cancel:p::Cancellation{bool cancelled()noexcept override{return true;}} cancel;
 assert(std::holds_alternative<p::Error>(p::prepare(translated,&cancel)));
 std::optional<r::Frame> before,after;assert(store.copy_confirmed(before)==r::FrameStatus::ok);
 assert(r::software::render(store,std::get<p::PreparedFrame>(valid),&cancel).status==r::software::Status::cancelled);
 assert(store.copy_confirmed(after)==r::FrameStatus::ok&&before->pixels==after->pixels);
 auto huge=p::prepare({{p::PushScroll{1,1,1},p::Fill{{65536,0,10,10},{},{1,0,0,1}},p::PopScroll{}}});assert(std::holds_alternative<p::PreparedFrame>(huge));
 assert(r::software::render(store,std::get<p::PreparedFrame>(huge)).status!=r::software::Status::ok);
 assert(store.copy_confirmed(after)==r::FrameStatus::ok&&before->pixels==after->pixels);
 std::cout<<"PASS frozen scroll fractional/nested/restoration/clip/opacity/scopes/bounds\n";
}
