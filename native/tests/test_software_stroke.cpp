#ifdef NDEBUG
#error "Assertions required"
#endif
#include "../src/rendering/software/software_renderer.h"
#include "../src/rendering/border_painter.h"
#include <array>
#include <cassert>
#include <limits>
#include <new>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
p::PreparedFrame prepare(std::vector<p::Command> cmds) {
 auto r=p::prepare({std::move(cmds)});assert(std::holds_alternative<p::PreparedFrame>(r));
 return std::move(std::get<p::PreparedFrame>(r));
}
Frame get(FrameStore& store) {std::optional<Frame> out;assert(store.copy_confirmed(out)==FrameStatus::ok && out);return *out;}
void pixel(const Frame& f,int x,int y,std::array<int,4> expected) {
 auto n=std::size_t(y)*f.stride+std::size_t(x)*4;
 for(int i=0;i<4;++i) assert(f.pixels[n+std::size_t(i)]==expected[std::size_t(i)]);
}
p::Stroke stroke(float width=1) {return {{1.5f,1.5f,3,3},{},width,{1,0,0,.5f}};}
struct Cancel:p::Cancellation {int count{},at;explicit Cancel(int n):at(n){} bool cancelled() noexcept override{return ++count==at;}};
p::Stroke emit(float w,float h,float thickness) {
 aether::style::StyleScalars style{};
 style.opacity=1;style.border_styles={AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID};
 style.border_colors={{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1}};
 std::vector<aether_paint_command_t> out;
 aether_paint_border(style,{1,1,w,h},{},{thickness,thickness,thickness,thickness},out);
 assert(out.size()==1 && out[0].tag==AETHER_PAINT_STROKE_ROUNDED_RECT);
 const auto& v=out[0].data.stroke_rounded_rect;
 return {v.rect,v.radii,v.stroke_width,v.color};
}
int main() {
 FrameStore store;assert(store.resize({8,8,1})==FrameStatus::ok);
 auto run=[&](p::Stroke st){auto r=s::render(store,prepare({st}));assert(r.status==s::Status::ok);return get(store);};
 auto f=run(stroke());pixel(f,1,1,{0,0,128,128});pixel(f,2,2,{0,0,0,0});pixel(f,4,4,{0,0,128,128});
 auto r=s::render(store,prepare({p::Fill{{0,0,8,8},{},{0,0,1,1}},stroke()}));assert(r.status==s::Status::ok);
 pixel(get(store),1,1,{127,0,128,255});
 f=run(stroke(.25f));pixel(f,1,1,{0,0,32,32});pixel(f,2,1,{0,0,32,32});pixel(f,2,2,{0,0,0,0});
 f=run(stroke(2));pixel(f,0,0,{0,0,32,32});pixel(f,2,2,{0,0,96,96});
 f=run(stroke(8));pixel(f,2,2,{0,0,128,128});
 f=run(stroke(0));for(auto c:f.pixels)assert(c==0);
 auto clipped=prepare({p::PushClip{{1.25f,1.5f,4,4},{}},p::PushClip{{1.5f,1.25f,4,4},{}},stroke(),p::PopClip{},p::PopClip{}});
 assert(s::render(store,clipped).status==s::Status::ok);pixel(get(store),1,1,{0,0,32,32});
 f=run(emit(4,4,1));pixel(f,0,0,{0,0,0,0});pixel(f,1,1,{0,0,255,255});pixel(f,4,4,{0,0,255,255});pixel(f,5,5,{0,0,0,0});pixel(f,2,2,{0,0,0,0});
 const auto saved=get(store);
 for(auto box:std::array<std::array<float,3>,4>{{{4,6,4},{4,6,5},{6,4,4},{6,4,5}}}) {
  auto bad=emit(box[0],box[1],box[2]);
  assert(s::render(store,prepare({stroke(),bad})).status==s::Status::unsupported);
  assert(get(store).sequence==saved.sequence && get(store).pixels==saved.pixels);
 }
 auto rounded=stroke(0);rounded.radii.top_left_x=1;assert(s::render(store,prepare({rounded})).status==s::Status::unsupported);
 auto invalid=stroke(-1);assert(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 invalid=stroke();invalid.rect.x=std::numeric_limits<float>::infinity();assert(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 auto giant=stroke(std::numeric_limits<float>::max());giant.rect={-1,-1,std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
 f=run(giant);pixel(f,0,0,{0,0,128,128});
 auto normal=prepare({stroke()});Cancel count(100000);assert(s::render(store,normal,&count).status==s::Status::ok);
 const auto before=get(store);
 for(int i=1;i<=count.count;++i){Cancel c(i);assert(s::render(store,normal,&c).status==s::Status::cancelled);assert(get(store).sequence==before.sequence && get(store).pixels==before.pixels);}
 bool fail=false;FrameStore oom([&]{if(fail)throw std::bad_alloc{};});assert(oom.resize({8,8,1})==FrameStatus::ok);assert(s::render(oom,normal).status==s::Status::ok);fail=true;assert(s::render(oom,normal).status==s::Status::allocation_failed);fail=false;assert(get(oom).sequence==1);
 assert(store.resize({8192,2048,1})==FrameStatus::ok);
 auto big=stroke(10000);big.rect={1,1,8190,2046};
 assert(s::render(store,prepare({big})).status==s::Status::limit_exceeded); // clear + 2*surface >50M
}
