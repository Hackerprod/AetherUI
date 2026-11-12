#include "software_renderer.h"
#include <algorithm>
#include <cmath>
#include <new>
#include <vector>
namespace aether::rendering::software {
namespace {
constexpr std::uint64_t max_work = 50000000;
struct Rect { double l, t, r, b; };
struct Draw { Rect rect; prep::Color color; std::size_t index; };
Rect intersect(Rect a, Rect b) noexcept {
 return {std::max(a.l,b.l),std::max(a.t,b.t),std::min(a.r,b.r),std::min(a.b,b.b)};
}
Rect rect(prep::Rect r) noexcept {return {r.x,r.y,double(r.x)+r.width,double(r.y)+r.height};}
bool empty(Rect r) noexcept {return r.r<=r.l || r.b<=r.t;}
bool zero(prep::Radii r) noexcept {
 return r.top_left_x==0 && r.top_left_y==0 && r.top_right_x==0 && r.top_right_y==0 &&
 r.bottom_left_x==0 && r.bottom_left_y==0 && r.bottom_right_x==0 && r.bottom_right_y==0;
}
bool identity(prep::Matrix m) noexcept {return m.m11==1 && m.m12==0 && m.m21==0 && m.m22==1 && m.dx==0 && m.dy==0;}
bool cancelled(prep::Cancellation* c) noexcept {return c && c->cancelled();}
unsigned byte(double v) noexcept {return unsigned(std::floor(v*255+0.5));}
unsigned mul(unsigned a,unsigned b) noexcept {return (a*b+127)/255;}
Result failed(FrameStatus s) noexcept {
 return {s==FrameStatus::allocation_failed?Status::allocation_failed:Status::frame_failed,s};
}
struct Guard {
 FrameStore& store; FrameStore::Token token; bool active{true};
 ~Guard() {if(active) (void)store.discard(token);}
};
}
Result render(FrameStore& store,const prep::PreparedFrame& frame,prep::Cancellation* cancel) noexcept {
 Viewport v;
 if(auto s=store.current_viewport(v);s!=FrameStatus::ok) return failed(s);
 if(v.dpi!=1) return {Status::unsupported};
 if(!v.width || !v.height) return failed(FrameStatus::suspended);
 // FrameStore owns validation of viewport and its 64 MiB per-surface limit.
 std::uint64_t work=std::uint64_t(v.width)*std::uint64_t(v.height);
 std::size_t index=0;
 try {
  if(cancelled(cancel)) return {Status::cancelled};
  std::vector<Draw> draws;
  std::vector<Rect> clips;
  Rect current{0,0,double(v.width),double(v.height)};
  for(const auto& event:frame.events()) {
   if(cancelled(cancel)) return {Status::cancelled,FrameStatus::ok,index,work};
   const auto& command=event.command;
   if(!identity(event.matrix)) return {Status::unsupported,FrameStatus::ok,index,work};
   if(const auto* fill=std::get_if<prep::Fill>(&command)) {
    if(!zero(fill->radii)) return {Status::unsupported,FrameStatus::ok,index,work};
    auto r=intersect(current,rect(fill->rect));
    if(!empty(r)) {
     // All endpoints are bounded by viewport before integer conversion.
     const auto width=std::uint64_t(std::ceil(r.r)-std::floor(r.l));
     const auto height=std::uint64_t(std::ceil(r.b)-std::floor(r.t));
     if(height && width>(max_work-work)/height) return {Status::limit_exceeded,FrameStatus::ok,index,work};
     work+=width*height;
     draws.push_back({r,fill->color,index});
    }
   } else if(const auto* clip=std::get_if<prep::PushClip>(&command)) {
    if(!zero(clip->radii)) return {Status::unsupported,FrameStatus::ok,index,work};
    clips.push_back(current); current=intersect(current,rect(clip->rect));
   } else if(std::holds_alternative<prep::PopClip>(command)) {
    // PreparedFrame guarantees balanced scopes and validated finite payloads.
    current=clips.back();clips.pop_back();
   } else if(const auto* push=std::get_if<prep::PushTransform>(&command)) {
    if(!identity(push->matrix)) return {Status::unsupported,FrameStatus::ok,index,work};
   } else if(!std::holds_alternative<prep::PopTransform>(command) &&
             !std::holds_alternative<prep::BeginChunk>(command) &&
             !std::holds_alternative<prep::EndChunk>(command)) {
    return {Status::unsupported,FrameStatus::ok,index,work};
   }
   ++index;
  }
  if(cancelled(cancel)) return {Status::cancelled,FrameStatus::ok,index,work};
  FrameStore::Token token;
  if(auto s=store.prepare(token);s!=FrameStatus::ok) return failed(s);
  Guard guard{store,token};
  {
   std::span<std::uint8_t> pixels;
   if(auto s=store.candidate(token,pixels);s!=FrameStatus::ok) return failed(s);
   const auto stride=std::size_t(v.width)*4;
   for(int y=0;y<v.height;++y) {
    if(cancelled(cancel)) return {Status::cancelled,FrameStatus::ok,index,work};
    std::fill_n(pixels.data()+std::size_t(y)*stride,stride,std::uint8_t(0));
   }
   for(const auto& draw:draws) {
    index=draw.index;
    if(cancelled(cancel)) return {Status::cancelled,FrameStatus::ok,index,work};
    const auto r=draw.rect;
    const int x0=int(std::floor(r.l)),x1=int(std::ceil(r.r));
    const int y0=int(std::floor(r.t)),y1=int(std::ceil(r.b));
    const unsigned alpha=byte(draw.color.a);
    const unsigned channels[]{byte(draw.color.b),byte(draw.color.g),byte(draw.color.r)};
    for(int y=y0;y<y1;++y) {
     if(cancelled(cancel)) return {Status::cancelled,FrameStatus::ok,index,work};
     const double cy=std::min(r.b,double(y)+1)-std::max(r.t,double(y));
     for(int x=x0;x<x1;++x) {
      const double cx=std::min(r.r,double(x)+1)-std::max(r.l,double(x));
      const unsigned coverage=byte(std::clamp(cx*cy,0.0,1.0));
      const unsigned sa=mul(alpha,coverage),inv=255-sa;
      auto* p=pixels.data()+std::size_t(y)*stride+std::size_t(x)*4;
      for(int c=0;c<3;++c) p[c]=std::uint8_t(mul(channels[c],sa)+mul(p[c],inv));
      p[3]=std::uint8_t(sa+mul(p[3],inv));
     }
    }
   }
  } // No borrowed view survives commit/discard, including exception unwinding.
  if(cancelled(cancel)) return {Status::cancelled,FrameStatus::ok,index,work};
  if(auto s=store.commit(token);s!=FrameStatus::ok) return failed(s);
  guard.active=false;
  return {Status::ok,FrameStatus::ok,0,work};
 } catch(const std::bad_alloc&) {return {Status::allocation_failed,FrameStatus::ok,index,work};}
}
}
