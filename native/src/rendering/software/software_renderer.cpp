#include "software_renderer.h"
#include "shadow_math.h"
#include "border_math.h"
#include <algorithm>
#include <cmath>
#include <new>
#include <vector>
#include <map>
#include <set>
#include <tuple>
#include <chrono>
namespace aether::rendering::software {
namespace {
constexpr std::uint64_t max_work = 50000000;
struct Rect { double l, t, r, b; };
struct CurvedClip { Rect original{}; double radius{}; };
struct ClipState { Rect bounds; CurvedClip curve; };
struct Draw { Rect rect; Rect inner; prep::Color color; std::size_t index; Rect original{}; double radius{}; Rect inner_original{}; double inner_radius{}; CurvedClip clip{}; bool shadow{}; Rect source{}; Rect excluded{}; double sigma{}; const prep::BorderGroup* border{}; int layer_event{}; float layer_opacity{1}; double scroll_x{},scroll_y{}; std::shared_ptr<const text::GlyphMaskA8> mask; const prep::LinearGradientFill* gradient{}; Rect gradient_rect{}; };
Rect intersect(Rect a, Rect b) noexcept {
 return {std::max(a.l,b.l),std::max(a.t,b.t),std::min(a.r,b.r),std::min(a.b,b.b)};
}
Rect rect(prep::Rect r) noexcept {return {r.x,r.y,double(r.x)+r.width,double(r.y)+r.height};}
bool empty(Rect r) noexcept {return r.r<=r.l || r.b<=r.t;}
bool uniform(prep::Radii r) noexcept {
 return r.top_left_x==r.top_left_y && r.top_left_x==r.top_right_x &&
 r.top_left_x==r.top_right_y && r.top_left_x==r.bottom_left_x &&
 r.top_left_x==r.bottom_left_y && r.top_left_x==r.bottom_right_x && r.top_left_x==r.bottom_right_y;
}
// Shape centers always use original geometry, never its clipped bounding box.
double rounded_area(Rect clipped,Rect original,double radius,int x,int y,std::uint64_t& sampled) noexcept {
 const auto b=intersect(clipped,{double(x),double(y),double(x)+1,double(y)+1});
 if(empty(b)) return 0;
 const auto o=original;const double r=radius;
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
bool contains(Rect o,double r,double x,double y) noexcept {
 if(empty(o) || x<o.l || x>o.r || y<o.t || y>o.b) return false;
 if(r==0) return true;
 const double dx=x-std::clamp(x,o.l+r,o.r-r),dy=y-std::clamp(y,o.t+r,o.b-r);
 return dx*dx+dy*dy<=r*r;
}
// Evaluate the immutable ramp in its own coordinate system, not the clipped
// coverage rectangle. upper_bound picks the LAST coincident stop at a hard edge.
void gradient_sample(const prep::LinearGradientFill& g,Rect r,double x,double y,double* out) noexcept {
 const double length=(r.r-r.l)*std::abs(g.direction_x)+(r.b-r.t)*std::abs(g.direction_y);
 const double t=.5+((x-(r.l+r.r)*.5)*g.direction_x+(y-(r.t+r.b)*.5)*g.direction_y)/length;
 const auto upper=std::upper_bound(g.stops.begin(),g.stops.end(),t,[](double t,const auto& stop){return t<stop.offset;});
 const auto& a=upper==g.stops.begin()?g.stops.front():*(upper-1);
 const auto& b=upper==g.stops.end()?g.stops.back():*upper;
 const double u=upper==g.stops.begin()||upper==g.stops.end()?0:(t-a.offset)/(b.offset-a.offset);
 const double aa=a.color.a,ba=b.color.a;
 out[0]=(a.color.b*aa+(b.color.b*ba-a.color.b*aa)*u)*g.opacity;
 out[1]=(a.color.g*aa+(b.color.g*ba-a.color.g*aa)*u)*g.opacity;
 out[2]=(a.color.r*aa+(b.color.r*ba-a.color.r*aa)*u)*g.opacity;
 out[3]=(aa+(ba-aa)*u)*g.opacity;
}
bool central(Rect b,CurvedClip c) noexcept {
 return (b.l>=c.original.l+c.radius && b.r<=c.original.r-c.radius) ||
        (b.t>=c.original.t+c.radius && b.b<=c.original.b-c.radius);
}
double joint_area(const Draw& d,Rect b,std::uint64_t& sampled) noexcept {
 ++sampled;unsigned count=0;
 for(int sy=0;sy<16;++sy)for(int sx=0;sx<16;++sx){
  const double x=b.l+(sx+.5)*(b.r-b.l)/16,y=b.t+(sy+.5)*(b.b-b.t)/16;
  if(contains(d.original,d.radius,x,y) && !contains(d.inner_original,d.inner_radius,x,y) &&
     contains(d.clip.original,d.clip.radius,x,y)) ++count;
 }
 return (b.r-b.l)*(b.b-b.t)*count/256;
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
static Result render_impl(FrameStore& store,const prep::PreparedFrame& frame,
 const std::span<const std::shared_ptr<const text::GlyphMaskA8>>* masks, prep::Cancellation* cancel) noexcept {
 const auto started=std::chrono::steady_clock::now();
 Viewport v;
 if(auto s=store.current_viewport(v);s!=FrameStatus::ok) return failed(s);
 if(v.dpi!=1) return {Status::unsupported};
 if(!v.width || !v.height) return failed(FrameStatus::suspended);
 // FrameStore owns validation of viewport and its 64 MiB per-surface limit.
 std::uint64_t work=std::uint64_t(v.width)*std::uint64_t(v.height);
 if(work>max_work) return {Status::limit_exceeded};
 std::size_t index=0;
 std::uint64_t mask_bytes=0,glyph_uses=0,mask_pixels=0,metadata_bytes=0;
 std::uint64_t corner_pixels=0,sampled_pixels=0,inner_corner_pixels=0,inner_sampled_pixels=0,clip_corner_pixels=0,joint_sampled_pixels=0;
 try {
  if(cancelled(cancel)) return {Status::cancelled};
  using Key=std::tuple<std::uintptr_t,std::uint32_t,std::uint32_t,unsigned,unsigned>;
  const auto key=[](const text::FontInstance* f,std::uint32_t id,std::uint32_t flags,unsigned x,unsigned y){return Key{reinterpret_cast<std::uintptr_t>(f),id,flags,x,y};};
  std::map<Key,std::shared_ptr<const text::GlyphMaskA8>> lookup;
  std::set<const text::FontInstance*> fonts;
  if(masks) {
   if(masks->size()>text::max_frame_glyphs) return {Status::limit_exceeded};
   for(const auto& m:*masks) {
    if(cancelled(cancel))return {Status::cancelled};
    if(!m) return {Status::invalid_mask};
    if(m->bytes.size()>text::max_mask_bytes-mask_bytes) return {Status::limit_exceeded};
    mask_bytes+=m->bytes.size();
    // Conservative metadata reservation covers map nodes and shared ownership.
    metadata_bytes+=sizeof(text::GlyphMaskA8)+256;
    if(fonts.insert(m->font.get()).second)metadata_bytes+=m->font->bytes.size();
    if(metadata_bytes>text::max_mask_bytes-mask_bytes) return {Status::limit_exceeded};
    if(!lookup.emplace(key(m->font.get(),m->glyph_id,m->flags,m->phase_x,m->phase_y),m).second)
     return {Status::invalid_mask};
   }
  }
  std::vector<Draw> draws;
  std::vector<ClipState> clips;
  CurvedClip curve;
  std::size_t layer_depth=0,max_layer_depth=0;
  const auto surface_bytes=std::uint64_t(v.width)*std::uint64_t(v.height)*4;
  if(masks && mask_bytes+metadata_bytes+surface_bytes>text::max_mask_bytes)return {Status::limit_exceeded};
  Rect current{0,0,double(v.width),double(v.height)};
  for(const auto& event:frame.events()) {
   if(cancelled(cancel)) return {Status::cancelled,FrameStatus::ok,index,work};
   const auto& command=event.command;
   const auto translated=[&](prep::Rect r){auto v=rect(r);return Rect{v.l+event.scroll_x,v.t+event.scroll_y,v.r+event.scroll_x,v.b+event.scroll_y};};
   const auto bounded=[](Rect r){return std::isfinite(r.l)&&std::isfinite(r.t)&&std::isfinite(r.r)&&std::isfinite(r.b)&&std::abs(r.l)<=65536&&std::abs(r.t)<=65536&&std::abs(r.r)<=65536&&std::abs(r.b)<=65536;};
   const bool translated_ok=std::visit([&](const auto& c){using T=std::decay_t<decltype(c)>;
    if constexpr(std::is_same_v<T,prep::Fill>||std::is_same_v<T,prep::Stroke>||std::is_same_v<T,prep::BoxShadow>||std::is_same_v<T,prep::PushClip>)return bounded(translated(c.rect));
    else if constexpr(std::is_same_v<T,prep::LinearGradientFill>)return bounded(translated(c.rect))&&bounded(translated(c.gradient_rect));
    else if constexpr(std::is_same_v<T,prep::BorderGroup>)return bounded(translated(c.outer))&&bounded(translated(c.inner));
    else return true;
   },command);
   if((event.scroll_depth>0||event.scroll_x!=0||event.scroll_y!=0) && !translated_ok)return {Status::unsupported,FrameStatus::ok,index,work};
   if(!identity(event.matrix)) return {Status::unsupported,FrameStatus::ok,index,work};
   if(const auto* begin=std::get_if<prep::BeginOpacity>(&command)) {
    ++layer_depth;max_layer_depth=std::max(layer_depth,max_layer_depth);
    if(layer_depth>8 || surface_bytes*(layer_depth+(masks?1:0))+mask_bytes+metadata_bytes>64u*1024u*1024u || !charge(work,surface_bytes/4,2))
     return {Status::limit_exceeded,FrameStatus::ok,index,work};
    Draw d{};d.index=index;d.layer_event=1;d.layer_opacity=begin->opacity;draws.push_back(d);
   } else if(std::holds_alternative<prep::EndOpacity>(command)) {
    --layer_depth;Draw d{};d.index=index;d.layer_event=-1;draws.push_back(d);
   } else if(const auto* fill=std::get_if<prep::LinearGradientFill>(&command)) {
    const double radius=fill->radii.top_left_x;
    if(!uniform(fill->radii) || (radius>0 &&
       (fill->rect.width<=0 || fill->rect.height<=0 || 2*radius>std::min(fill->rect.width,fill->rect.height))))
     return {Status::unsupported,FrameStatus::ok,index,work};
    const auto original=translated(fill->rect),r=intersect(current,original);
    // 256 samples, bounded binary stop lookup (<=7 comparisons) and blend.
    // Charged before candidate allocation; solid draws retain their fast path.
    if(!charge(work,pixel_count(r),256*8))return {Status::limit_exceeded,FrameStatus::ok,index,work};
    if(!empty(r)) {
     Draw d{};d.rect=r;d.index=index;d.original=original;d.radius=radius;d.clip=curve;
     d.gradient=fill;d.gradient_rect=translated(fill->gradient_rect);draws.push_back(d);
    }
   } else if(const auto* fill=std::get_if<prep::Fill>(&command)) {
    const double radius=fill->radii.top_left_x;
    if(!uniform(fill->radii) || (radius>0 &&
       (fill->rect.width<=0 || fill->rect.height<=0 || 2*radius>std::min(fill->rect.width,fill->rect.height))))
     return {Status::unsupported,FrameStatus::ok,index,work};
    auto r=intersect(current,translated(fill->rect));
    if(!empty(r)) {
     // All endpoints are bounded by viewport before integer conversion.
     const auto count=pixel_count(r);
     if(radius>0) {
      const auto corners=corner_bound(translated(fill->rect),r,radius);
      if(!charge(work,count,2) || !charge(work,corners,256))
       return {Status::limit_exceeded,FrameStatus::ok,index,work};
      // Bounded by total charged work, so this addition cannot overflow.
      corner_pixels+=corners;
     } else if(!charge(work,count,1)) return {Status::limit_exceeded,FrameStatus::ok,index,work};
     draws.push_back({r,{},fill->color,index,translated(fill->rect),radius,{},0,curve});
    }
   } else if(const auto* stroke=std::get_if<prep::Stroke>(&command)) {
    const double radius=stroke->radii.top_left_x;
    if(!uniform(stroke->radii) || (radius>0 &&
       (stroke->rect.width<=0 || stroke->rect.height<=0 ||
        2*radius>std::min(stroke->rect.width,stroke->rect.height))))
     return {Status::unsupported,FrameStatus::ok,index,work};
    if(stroke->stroke_width>0) {
     // A degenerate centerline may represent a visible thick CSS border.
     // Its original border-box cannot be reconstructed: reject, never omit.
     if(stroke->rect.width==0 || stroke->rect.height==0)
      return {Status::unsupported,FrameStatus::ok,index,work};
     const auto center=translated(stroke->rect);
     const double half=double(stroke->stroke_width)/2;
     const Rect original{center.l-half,center.t-half,center.r+half,center.b+half};
     const Rect inner_original{center.l+half,center.t+half,center.r-half,center.b-half};
     auto outer=intersect(current,original);
     auto inner=intersect(current,inner_original);
     if(!empty(outer)) {
      const auto count=pixel_count(outer);
      if(radius>0) {
       const double outer_radius=radius+half,inner_radius=std::max(radius-half,0.0);
       const auto outer_corners=corner_bound(original,outer,outer_radius);
       const auto inner_corners=inner_radius>0 && !empty(inner)?
        corner_bound(inner_original,inner,inner_radius):0;
       if(!charge(work,count,4) || !charge(work,outer_corners,256) ||
          !charge(work,inner_corners,256))
        return {Status::limit_exceeded,FrameStatus::ok,index,work};
       corner_pixels+=outer_corners;
       inner_corner_pixels+=inner_corners;
       draws.push_back({outer,inner,stroke->color,index,original,outer_radius,inner_original,inner_radius,curve});
      } else {
       if(!charge(work,count,2))
        return {Status::limit_exceeded,FrameStatus::ok,index,work};
       draws.push_back({outer,inner,stroke->color,index,original,0,inner_original,0,curve});
      }
     }
    }
   } else if(const auto* group=std::get_if<prep::BorderGroup>(&command)) {
    const auto o=translated(group->outer),in=translated(group->inner);
    const auto range=[](Rect r){return std::abs(r.l)<=65536 && std::abs(r.r)<=65536 && std::abs(r.t)<=65536 && std::abs(r.b)<=65536;};
    if(curve.radius>0 || !uniform(group->outer_radii) || group->outer_radii.top_left_x!=0 ||
       !uniform(group->inner_radii) || group->inner_radii.top_left_x!=0 ||
       empty(o) || empty(in) || !range(o) || !range(in) ||
       in.l<o.l || in.r>o.r || in.t<o.t || in.b>o.b)
     return {Status::unsupported,FrameStatus::ok,index,work};
    for(int side=0;side<4;++side) if(group->sides[side].present) {
     const auto style=group->sides[side].style;
     const double t=border_math::thickness(*group,side),length=side%2==0?group->outer.width:group->outer.height;
     if(style!=AETHER_BORDER_STYLE_SOLID && style!=AETHER_BORDER_STYLE_DASHED)
      return {Status::unsupported,FrameStatus::ok,index,work};
     if(style==AETHER_BORDER_STYLE_DASHED && t>0 &&
        (t<1.0/64 || t>8192 || length/(6*t)>4096))return {Status::unsupported,FrameStatus::ok,index,work};
    }
    const auto r=intersect(current,o);
    if(!empty(r)) {
     const auto clipped_inner=intersect(r,in);
     const auto whole_width=std::max(0.0,std::floor(clipped_inner.r)-std::ceil(clipped_inner.l));
     const auto whole_height=std::max(0.0,std::floor(clipped_inner.b)-std::ceil(clipped_inner.t));
     const auto n=pixel_count(r),inside=std::uint64_t(whole_width)*std::uint64_t(whole_height);
     if(!charge(work,n,1) || !charge(work,n-inside,1024))return {Status::limit_exceeded,FrameStatus::ok,index,work};
     Draw d{r,intersect(current,in),{},index};d.original=o;d.inner_original=in;d.border=group;d.scroll_x=event.scroll_x;d.scroll_y=event.scroll_y;draws.push_back(d);
    }
   } else if(const auto* shadow=std::get_if<prep::BoxShadow>(&command)) {
    const auto& s=shadow->shadow;
    const auto box=translated(shadow->rect);
    const auto in_range=[](Rect r){return std::abs(r.l)<=65536 && std::abs(r.r)<=65536 && std::abs(r.t)<=65536 && std::abs(r.b)<=65536;};
    if(!uniform(shadow->radii) || shadow->radii.top_left_x!=0 || curve.radius>0 || s.inset ||
       shadow->rect.width>8192 || shadow->rect.height>8192 || !in_range(box) ||
       std::abs(s.offset_x)>8192 || std::abs(s.offset_y)>8192 || std::abs(s.spread_radius)>8192 ||
       s.blur_radius>64 || (s.blur_radius>0 && s.blur_radius<1))
     return {Status::unsupported,FrameStatus::ok,index,work};
    const double spread=s.spread_radius,sigma=double(s.blur_radius)/2;
    const Rect source{box.l-spread+s.offset_x,box.t-spread+s.offset_y,box.r+spread+s.offset_x,box.b+spread+s.offset_y};
    const Rect support{source.l-4*sigma,source.t-4*sigma,source.r+4*sigma,source.b+4*sigma};
    if(!in_range(source) || !in_range(support)) return {Status::unsupported,FrameStatus::ok,index,work};
    if(!empty(source)) {
     const auto r=intersect(current,support);
     if(!empty(r)) {
      const auto columns=std::uint64_t(std::ceil(r.r)-std::floor(r.l));
      const auto rows=std::uint64_t(std::ceil(r.b)-std::floor(r.t));
      if(!charge(work,columns+rows,256) || !charge(work,pixel_count(r),2))
       return {Status::limit_exceeded,FrameStatus::ok,index,work};
      Draw d{r,{},s.color,index};d.shadow=true;d.source=source;d.excluded=box;d.sigma=sigma;draws.push_back(d);
     }
    }
   } else if(const auto* t=std::get_if<prep::Text>(&command)) {
    if(!masks || !t->native || !t->native->run) return {Status::unsupported,FrameStatus::ok,index,work};
    if(curve.radius!=0 || std::floor(current.l)!=current.l || std::floor(current.t)!=current.t ||
       std::floor(current.r)!=current.r || std::floor(current.b)!=current.b)
     return {Status::unsupported,FrameStatus::ok,index,work};
    const auto& run=*t->native->run;
    // Reject off-grid inputs instead of silently quantizing. This also bounds
    // the conversion before any integer cast. 26.6 pen uses checked int64.
    const auto grid=[](double n,std::int64_t& out){
     if(!std::isfinite(n)||std::abs(n)>65536||std::floor(n*64)!=n*64)return false;
     out=static_cast<std::int64_t>(n*64);return true;
    };
    std::int64_t bx,by;
    if(!grid(double(t->rect.x)+event.scroll_x,bx) || !grid(double(t->native->baseline)+event.scroll_y,by))
     return {Status::unsupported,FrameStatus::ok,index,work};
    std::int64_t pen_x=0,pen_y=0;
    for(const auto& g:run.glyphs) {
     if(cancelled(cancel))return {Status::cancelled,FrameStatus::ok,index,work};
     if(++glyph_uses>text::max_frame_glyphs)return {Status::limit_exceeded,FrameStatus::ok,index,work};
     metadata_bytes+=sizeof(Draw)*2; // vector growth and per-use ownership
     if(metadata_bytes+mask_bytes+surface_bytes*(max_layer_depth+1)>text::max_mask_bytes)
      return {Status::limit_exceeded,FrameStatus::ok,index,work};
     const auto x=bx+pen_x+g.offset_x,y=by-pen_y-g.offset_y;
     const auto floor64=[](std::int64_t n){return n/64-(n%64<0);};
     const auto ix=floor64(x),iy=floor64(y);
     const auto found=lookup.find(key(run.font.get(),g.id,run.font->raster_flags,unsigned(x-ix*64),unsigned(y-iy*64)));
     if(found==lookup.end())return {Status::missing_mask,FrameStatus::ok,index,work};
     const auto& m=found->second;
     const auto left=ix+m->bearing_left,top=iy-m->bearing_top;
     // All sums are < 2^40: per-glyph components and glyph count are bounded
     // by immutable ShapedRun, and pen checked on every iteration below.
     const Rect original{double(left),double(top),double(left+m->width),double(top+m->height)};
     const auto r=intersect(current,original);
     const auto count=pixel_count(r);
     if(count>text::max_mask_operations-mask_pixels || !charge(work,count,1))
      return {Status::limit_exceeded,FrameStatus::ok,index,work};
     mask_pixels+=count;
     if(!empty(r)){Draw d{r,{},t->color,index};d.original=original;d.mask=m;draws.push_back(std::move(d));}
     pen_x+=g.advance_x;pen_y+=g.advance_y;
     if(std::abs(pen_x)>(std::int64_t(1)<<40)||std::abs(pen_y)>(std::int64_t(1)<<40))
      return {Status::limit_exceeded,FrameStatus::ok,index,work};
    }
   } else if(const auto* clip=std::get_if<prep::PushClip>(&command)) {
    const double radius=clip->radii.top_left_x;
    if(masks) {
     const auto r=translated(clip->rect);
     if(radius!=0 || !uniform(clip->radii) || std::floor(r.l)!=r.l || std::floor(r.t)!=r.t ||
        std::floor(r.r)!=r.r || std::floor(r.b)!=r.b)
      return {Status::unsupported,FrameStatus::ok,index,work};
    }
    if(!uniform(clip->radii) || (radius>0 && (curve.radius>0 ||
       clip->rect.width<=0 || clip->rect.height<=0 ||
       2*radius>std::min(clip->rect.width,clip->rect.height))))
     return {Status::unsupported,FrameStatus::ok,index,work};
    clips.push_back({current,curve}); current=intersect(current,translated(clip->rect));
    if(radius>0) curve={translated(clip->rect),radius};
   } else if(std::holds_alternative<prep::PopClip>(command)) {
    // PreparedFrame guarantees balanced scopes and validated finite payloads.
    current=clips.back().bounds;curve=clips.back().curve;clips.pop_back();
   } else if(const auto* push=std::get_if<prep::PushTransform>(&command)) {
    if(!identity(push->matrix)) return {Status::unsupported,FrameStatus::ok,index,work};
   } else if(!std::holds_alternative<prep::PushScroll>(command) &&
             !std::holds_alternative<prep::PopScroll>(command) &&
             !std::holds_alternative<prep::PopTransform>(command) &&
             !std::holds_alternative<prep::BeginChunk>(command) &&
             !std::holds_alternative<prep::EndChunk>(command)) {
    return {Status::unsupported,FrameStatus::ok,index,work};
   }
   ++index;
  }
  // Charge clip work for each retained draw before any candidate is allocated.
  for(const auto& d:draws) if(d.clip.radius>0) {
   const auto bound=corner_bound(d.clip.original,d.rect,d.clip.radius);
   if(!charge(work,pixel_count(d.rect),2) || !charge(work,bound,768))
    return {Status::limit_exceeded,FrameStatus::ok,d.index,work};
   clip_corner_pixels+=bound;
  }
  if(cancelled(cancel)) return {Status::cancelled,FrameStatus::ok,index,work};
  std::vector<std::vector<std::uint8_t>> layers(max_layer_depth);
  for(auto& layer:layers) {
   if(cancelled(cancel))return {Status::cancelled,FrameStatus::ok,index,work};
   layer.resize(std::size_t(surface_bytes));
  }
  const auto prepared_at=std::chrono::steady_clock::now();
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
   const auto root_pixels=pixels;
   std::vector<unsigned> alphas;alphas.reserve(max_layer_depth);
   for(const auto& draw:draws) {
    index=draw.index;
    if(cancelled(cancel)) return {Status::cancelled,FrameStatus::ok,index,work};
    if(draw.layer_event==1) {
     pixels=layers[alphas.size()];alphas.push_back(byte(draw.layer_opacity));
     for(int y=0;y<v.height;++y){
      if(cancelled(cancel))return {Status::cancelled,FrameStatus::ok,index,work};
      std::fill_n(pixels.data()+std::size_t(y)*stride,stride,std::uint8_t(0));
     }
     continue;
    }
    if(draw.layer_event==-1) {
     const auto source=pixels;const auto opacity=alphas.back();alphas.pop_back();
     pixels=alphas.empty()?root_pixels:std::span<std::uint8_t>(layers[alphas.size()-1]);
     for(int y=0;y<v.height;++y){
      if(cancelled(cancel))return {Status::cancelled,FrameStatus::ok,index,work};
      for(int x=0;x<v.width;++x){
       const auto offset=std::size_t(y)*stride+std::size_t(x)*4;
       const unsigned alpha=mul(source[offset+3],opacity),inv=255-alpha;
       for(int c=0;c<4;++c)pixels[offset+c]=std::uint8_t(mul(source[offset+c],opacity)+mul(pixels[offset+c],inv));
      }
     }
     continue;
    }
    const auto r=draw.rect;
    const int x0=int(std::floor(r.l)),x1=int(std::ceil(r.r));
    const int y0=int(std::floor(r.t)),y1=int(std::ceil(r.b));
    // One reusable-per-draw scratch allocation: <=2*(8192+8192)*8 =256KiB.
    std::vector<double> scratch;
    const auto nx=std::size_t(x1-x0),ny=std::size_t(y1-y0);
    if(draw.shadow) {
     scratch.resize(2*(nx+ny));
     const auto axis=[&](int first,int last,double low,double high,double a,double b,double el,double eh,double* values,double* excluded){
      for(int i=first;i<last;++i) {
       if(cancelled(cancel)) return Status::cancelled;
       const double lo=std::max(double(i),low),hi=std::min(double(i)+1,high);
       const double all=shadow_math::integral(lo,hi,a,b,draw.sigma);
       const double part=shadow_math::integral(std::max(lo,el),std::min(hi,eh),a,b,draw.sigma);
       if(!shadow_math::bounded(all,hi-lo) || !shadow_math::bounded(part,all)) return Status::numeric_error;
       values[i-first]=std::clamp(all,0.0,hi-lo);excluded[i-first]=std::clamp(part,0.0,values[i-first]);
      }
      return Status::ok;
     };
     auto status=axis(x0,x1,r.l,r.r,draw.source.l,draw.source.r,draw.excluded.l,draw.excluded.r,scratch.data(),scratch.data()+nx);
     if(status==Status::ok)status=axis(y0,y1,r.t,r.b,draw.source.t,draw.source.b,draw.excluded.t,draw.excluded.b,scratch.data()+2*nx,scratch.data()+2*nx+ny);
     if(status!=Status::ok)return {status,FrameStatus::ok,index,work};
    }
    const unsigned alpha=byte(draw.color.a);
    const unsigned channels[]{byte(draw.color.b),byte(draw.color.g),byte(draw.color.r)};
    for(int y=y0;y<y1;++y) {
     if(cancelled(cancel)) return {Status::cancelled,FrameStatus::ok,index,work};
     for(int x=x0;x<x1;++x) {
      const auto b=intersect(r,{double(x),double(y),double(x)+1,double(y)+1});
      if(draw.gradient) {
       double sums[4]{};
       for(int sy=0;sy<16;++sy)for(int sx=0;sx<16;++sx) {
        const double px=b.l+(sx+.5)*(b.r-b.l)/16,py=b.t+(sy+.5)*(b.b-b.t)/16;
        if(!contains(draw.original,draw.radius,px,py) ||
           (draw.clip.radius>0&&!contains(draw.clip.original,draw.clip.radius,px,py)))continue;
        double sample[4];gradient_sample(*draw.gradient,draw.gradient_rect,px,py,sample);
        for(int c=0;c<4;++c)sums[c]+=sample[c];
       }
       const double scale=(b.r-b.l)*(b.b-b.t)/256;
       const unsigned sa=byte(std::clamp(sums[3]*scale,0.0,1.0)),inv=255-sa;
       auto* p=pixels.data()+std::size_t(y)*stride+std::size_t(x)*4;
       for(int c=0;c<3;++c)p[c]=std::uint8_t(std::min(sa,byte(std::clamp(sums[c]*scale,0.0,1.0)))+mul(p[c],inv));
       p[3]=std::uint8_t(sa+mul(p[3],inv));continue;
      }
      if(draw.mask) {
       const auto& m=*draw.mask;
       const auto mx=std::size_t(double(x)-draw.original.l),my=std::size_t(double(y)-draw.original.t);
       const unsigned sa=mul(alpha,m.bytes[my*m.stride+mx]),inv=255-sa;
       auto* p=pixels.data()+std::size_t(y)*stride+std::size_t(x)*4;
       for(int c=0;c<3;++c)p[c]=std::uint8_t(mul(channels[c],sa)+mul(p[c],inv));
       p[3]=std::uint8_t(sa+mul(p[3],inv));continue;
      }
      if(draw.border) {
       if(b.l>=draw.inner_original.l && b.r<=draw.inner_original.r && b.t>=draw.inner_original.t && b.b<=draw.inner_original.b)continue;
       double sums[4]{};
       for(int sy=0;sy<16;++sy)for(int sx=0;sx<16;++sx){
        const double px=b.l+(sx+.5)*(b.r-b.l)/16,py=b.t+(sy+.5)*(b.b-b.t)/16;
        if(contains(draw.inner_original,0,px,py))continue;
        const auto side=border_math::owner(*draw.border,px-draw.scroll_x,py-draw.scroll_y);
        if(side<0 || !draw.border->sides[side].present)continue;
        const auto& s=draw.border->sides[side];
        if(s.style==AETHER_BORDER_STYLE_DASHED && !border_math::dash(*draw.border,side,px-draw.scroll_x,py-draw.scroll_y))continue;
        const double a=s.color.a;sums[0]+=s.color.b*a;sums[1]+=s.color.g*a;sums[2]+=s.color.r*a;sums[3]+=a;
       }
       const double scale=(b.r-b.l)*(b.b-b.t)/256;
       const unsigned sa=byte(std::clamp(sums[3]*scale,0.0,1.0)),inv=255-sa;
       auto* p=pixels.data()+std::size_t(y)*stride+std::size_t(x)*4;
       for(int c=0;c<3;++c)p[c]=std::uint8_t(std::min(sa,byte(std::clamp(sums[c]*scale,0.0,1.0)))+mul(p[c],inv));
       p[3]=std::uint8_t(sa+mul(p[3],inv));continue;
      }
      double covered;
      if(draw.shadow) {
       covered=scratch[x-x0]*scratch[2*nx+y-y0]-scratch[nx+x-x0]*scratch[2*nx+ny+y-y0];
       if(!shadow_math::bounded(covered,1))return {Status::numeric_error,FrameStatus::ok,index,work};
      } else if(draw.clip.radius>0 && !central(b,draw.clip)) {
       covered=joint_area(draw,b,joint_sampled_pixels);
      } else {
      const double outer_area=draw.radius>0?
       rounded_area(r,draw.original,draw.radius,x,y,sampled_pixels):area(r,x,y);
      const double inner_area=draw.inner_radius>0?
       rounded_area(draw.inner,draw.inner_original,draw.inner_radius,x,y,inner_sampled_pixels):area(draw.inner,x,y);
      // Subtract unquantized coverage; only one source-over for the ring.
      covered=outer_area-inner_area;
      }
      const unsigned coverage=byte(std::clamp(covered,0.0,1.0));
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
  return {Status::ok,FrameStatus::ok,0,work,corner_pixels,sampled_pixels,inner_corner_pixels,inner_sampled_pixels,clip_corner_pixels,joint_sampled_pixels,lookup.size(),mask_bytes,glyph_uses,mask_pixels,
   std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(prepared_at-started).count()),
   std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-prepared_at).count())};
 } catch(const std::bad_alloc&) {return {Status::allocation_failed,FrameStatus::ok,index,work};}
}
Result render(FrameStore& store,const prep::PreparedFrame& frame,prep::Cancellation* cancel) noexcept {
 return render_impl(store,frame,nullptr,cancel);
}
Result render_a8(FrameStore& store,const prep::PreparedFrame& frame,
 std::span<const std::shared_ptr<const text::GlyphMaskA8>> masks,prep::Cancellation* cancel) noexcept {
 return render_impl(store,frame,&masks,cancel);
}
}
