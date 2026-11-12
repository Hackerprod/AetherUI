#include "software_renderer.h"
#include <algorithm>
#include <cmath>
#include <new>
#include <vector>
namespace aether::rendering::software {
namespace {
constexpr std::uint64_t max_work = 50000000;
struct Rect { double l, t, r, b; };
struct Draw { Rect rect; Rect inner; prep::Color color; std::size_t index; Rect original{}; double radius{}; };
Rect intersect(Rect a, Rect b) noexcept {
 return {std::max(a.l,b.l),std::max(a.t,b.t),std::min(a.r,b.r),std::min(a.b,b.b)};
}
Rect rect(prep::Rect r) noexcept {return {r.x,r.y,double(r.x)+r.width,double(r.y)+r.height};}
bool empty(Rect r) noexcept {return r.r<=r.l || r.b<=r.t;}
bool zero(prep::Radii r) noexcept {
 return r.top_left_x==0 && r.top_left_y==0 && r.top_right_x==0 && r.top_right_y==0 &&
 r.bottom_left_x==0 && r.bottom_left_y==0 && r.bottom_right_x==0 && r.bottom_right_y==0;
}
bool uniform(prep::Radii r) noexcept {
 return r.top_left_x==r.top_left_y && r.top_left_x==r.top_right_x &&
 r.top_left_x==r.top_right_y && r.top_left_x==r.bottom_left_x &&
 r.top_left_x==r.bottom_left_y && r.top_left_x==r.bottom_right_x && r.top_left_x==r.bottom_right_y;
}
// Shape centers always use original geometry, never its clipped bounding box.
double rounded_area(const Draw& d,int x,int y,std::uint64_t& sampled) noexcept {
 const auto b=intersect(d.rect,{double(x),double(y),double(x)+1,double(y)+1});
 if(empty(b)) return 0;
 const auto o=d.original;const double r=d.radius;
 // Entire clipped pixel inside either central band is exactly covered.
 if((b.l>=o.l+r && b.r<=o.r-r) || (b.t>=o.t+r && b.b<=o.b-r))
  return (b.r-b.l)*(b.b-b.t);
 ++sampled;
 unsigned inside=0;
 for(int sy=0;sy<16;++sy) for(int sx=0;sx<16;++sx) {
  const double px=b.l+(sx+.5)*(b.r-b.l)/16;
  const double py=b.t+(sy+.5)*(b.b-b.t)/16;
  const double cx=std::clamp(px,o.l+r,o.r-r),cy=std::clamp(py,o.t+r,o.b-r);
  const double dx=px-cx,dy=py-cy;
  if(dx*dx+dy*dy<=r*r) ++inside;
 }
 return (b.r-b.l)*(b.b-b.t)*inside/256;
}
// Inputs already clipped to the viewport, so integer products are <=8192^2.
std::uint64_t pixel_count(Rect r) noexcept {
 if(empty(r)) return 0;
 return std::uint64_t(std::ceil(r.r)-std::floor(r.l))*
        std::uint64_t(std::ceil(r.b)-std::floor(r.t));
}
std::uint64_t corner_bound(Rect original,Rect clipped,double radius) noexcept {
 const auto o=original; const double r=radius;
 const Rect corners[]{{o.l,o.t,o.l+r,o.t+r},{o.r-r,o.t,o.r,o.t+r},
                      {o.l,o.b-r,o.l+r,o.b},{o.r-r,o.b-r,o.r,o.b}};
 std::uint64_t sum=0;
 for(auto c:corners) sum+=pixel_count(intersect(clipped,c));
 return std::min(sum,pixel_count(clipped));
}
bool charge(std::uint64_t& total,std::uint64_t count,std::uint64_t factor) noexcept {
 if(count>(max_work-total)/factor) return false;
 total+=count*factor;return true;
}
bool identity(prep::Matrix m) noexcept {return m.m11==1 && m.m12==0 && m.m21==0 && m.m22==1 && m.dx==0 && m.dy==0;}
bool cancelled(prep::Cancellation* c) noexcept {return c && c->cancelled();}
double area(Rect r, int x, int y) noexcept {
 const double w=std::max(0.0,std::min(r.r,double(x)+1)-std::max(r.l,double(x)));
 const double h=std::max(0.0,std::min(r.b,double(y)+1)-std::max(r.t,double(y)));
 return w*h;
}
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
 std::uint64_t corner_pixels=0,sampled_pixels=0;
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
    const double radius=fill->radii.top_left_x;
    if(!uniform(fill->radii) || (radius>0 &&
       (fill->rect.width<=0 || fill->rect.height<=0 || 2*radius>std::min(fill->rect.width,fill->rect.height))))
     return {Status::unsupported,FrameStatus::ok,index,work};
    auto r=intersect(current,rect(fill->rect));
    if(!empty(r)) {
     // All endpoints are bounded by viewport before integer conversion.
     const auto count=pixel_count(r);
     if(radius>0) {
      const auto corners=corner_bound(rect(fill->rect),r,radius);
      if(!charge(work,count,2) || !charge(work,corners,256))
       return {Status::limit_exceeded,FrameStatus::ok,index,work};
      // Bounded by total charged work, so this addition cannot overflow.
      corner_pixels+=corners;
     } else if(!charge(work,count,1)) return {Status::limit_exceeded,FrameStatus::ok,index,work};
     draws.push_back({r,{},fill->color,index,rect(fill->rect),radius});
    }
   } else if(const auto* stroke=std::get_if<prep::Stroke>(&command)) {
    if(!zero(stroke->radii)) return {Status::unsupported,FrameStatus::ok,index,work};
    if(stroke->stroke_width>0) {
     // A degenerate centerline may represent a visible thick CSS border.
     // Its original border-box cannot be reconstructed: reject, never omit.
     if(stroke->rect.width==0 || stroke->rect.height==0)
      return {Status::unsupported,FrameStatus::ok,index,work};
     const auto center=rect(stroke->rect);
     const double half=double(stroke->stroke_width)/2;
     auto outer=intersect(current,{center.l-half,center.t-half,center.r+half,center.b+half});
     auto inner=intersect(current,{center.l+half,center.t+half,center.r-half,center.b-half});
     if(!empty(outer)) {
      const auto width=std::uint64_t(std::ceil(outer.r)-std::floor(outer.l));
      const auto height=std::uint64_t(std::ceil(outer.b)-std::floor(outer.t));
      // Two area evaluations per pixel; all endpoints bounded by viewport.
      if(height && width>(max_work-work)/2/height)
       return {Status::limit_exceeded,FrameStatus::ok,index,work};
      work+=2*width*height;
      draws.push_back({outer,inner,stroke->color,index});
     }
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
     for(int x=x0;x<x1;++x) {
      const unsigned coverage=byte(std::clamp(draw.radius>0?rounded_area(draw,x,y,sampled_pixels):area(r,x,y)-area(draw.inner,x,y),0.0,1.0));
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
  return {Status::ok,FrameStatus::ok,0,work,corner_pixels,sampled_pixels};
 } catch(const std::bad_alloc&) {return {Status::allocation_failed,FrameStatus::ok,index,work};}
}
}
