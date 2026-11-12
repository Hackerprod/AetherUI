#include "cairo_geometry.h"
#include <algorithm>
#include <cmath>
#include <new>
namespace aether::rendering::cairo_adapter {
ClassifiedMatrix classify(prep::Matrix m,float dpi) noexcept {
 if (!std::isfinite(dpi) || dpi<=0) return {};
 Matrix v{double(m.m11)*dpi,double(m.m12)*dpi,double(m.m21)*dpi,
          double(m.m22)*dpi,double(m.dx)*dpi,double(m.dy)*dpi};
 for(double n:{v.xx,v.yx,v.xy,v.yy,v.x0,v.y0}) if(!std::isfinite(n)) return {};
 const double det=v.xx*v.yy-v.xy*v.yx;
 if(!std::isfinite(det)) return {};
 if(det==0) return {MatrixKind::singular,v};
 const double xx=v.yy/det, yx=-v.yx/det, xy=-v.xy/det, yy=v.xx/det;
 for(double n:{xx,yx,xy,yy,-(xx*v.x0+xy*v.y0),-(yx*v.x0+yy*v.y0)})
  if(!std::isfinite(n)) return {};
 return {MatrixKind::valid,v};
}
std::optional<Path> rounded_path(prep::Rect r,prep::Radii a) noexcept {
 for(double n:{double(r.x),double(r.y),double(r.width),double(r.height)})
  if(!std::isfinite(n)) return {};
 if(r.width<0 || r.height<0) return {};
 std::array<double,8> v{a.top_left_x,a.top_left_y,a.top_right_x,a.top_right_y,
  a.bottom_right_x,a.bottom_right_y,a.bottom_left_x,a.bottom_left_y};
 for(double n:v) if(!std::isfinite(n)||n<0) return {};
 Path p;
 if(r.width==0 || r.height==0) return p;
 double scale=std::min({1.0,r.width/std::max(.0001,v[0]+v[2]),
  r.width/std::max(.0001,v[4]+v[6]),r.height/std::max(.0001,v[1]+v[7]),
  r.height/std::max(.0001,v[3]+v[5])});
 for(double& n:v) n*=scale;
 constexpr double k=.5522847498307936;
 const double l=r.x,t=r.y,b=t+double(r.height),right=l+double(r.width);
 auto add=[&](SegmentKind kind,std::array<double,6> values={}) { p.segments[p.count++]={kind,values}; };
 add(SegmentKind::move,{l+v[0],t});
 add(SegmentKind::line,{right-v[2],t});
 add(SegmentKind::curve,{right-v[2]*(1-k),t,right,t+v[3]*(1-k),right,t+v[3]});
 add(SegmentKind::line,{right,b-v[5]});
 add(SegmentKind::curve,{right,b-v[5]*(1-k),right-v[4]*(1-k),b,right-v[4],b});
 add(SegmentKind::line,{l+v[6],b});
 add(SegmentKind::curve,{l+v[6]*(1-k),b,l,b-v[7]*(1-k),l,b-v[7]});
 add(SegmentKind::line,{l,t+v[1]});
 add(SegmentKind::curve,{l,t+v[1]*(1-k),l+v[0]*(1-k),t,l+v[0],t});
 add(SegmentKind::close);
 return p;
}
PlanResult preflight(const prep::PreparedFrame& frame,float dpi,prep::Color clear) noexcept {
 if(!std::isfinite(dpi)||dpi<=0) return Error{ErrorCode::invalid_matrix};
 for(float c:{clear.r,clear.g,clear.b,clear.a})
  if(!std::isfinite(c)||c<0||c>1) return Error{ErrorCode::invalid_color};
 std::size_t index=0;
 try {
  Plan plan; plan.reserve(frame.events().size());
  for(const auto& e:frame.events()) {
   const int tag=prep::command_tag(e.command);
   if(std::holds_alternative<prep::Text>(e.command)) return Error{ErrorCode::unsupported_command,index,tag};
   const auto m=classify(e.matrix,dpi);
   if(m.kind==MatrixKind::invalid) return Error{ErrorCode::invalid_matrix,index,tag};
   Operation op;op.matrix=m.value;
   std::optional<Path> path;
   if(const auto* c=std::get_if<prep::Fill>(&e.command)) {
    path=rounded_path(c->rect,c->radii);op.color=c->color;
    op.action=m.kind==MatrixKind::singular?Action::none:Action::fill;
   } else if(const auto* c=std::get_if<prep::Stroke>(&e.command)) {
    path=rounded_path(c->rect,c->radii);op.color=c->color;op.width=std::max(.25,double(c->stroke_width));
    op.action=m.kind==MatrixKind::singular?Action::none:Action::stroke;
   } else if(const auto* c=std::get_if<prep::PushClip>(&e.command)) {
    path=rounded_path(c->rect,c->radii);
    op.action=m.kind==MatrixKind::singular?Action::empty_clip:Action::push_clip;
   } else if(std::holds_alternative<prep::PopClip>(e.command)) op.action=Action::pop_clip;
   else { plan.push_back(op);++index;continue; }
   if(op.action!=Action::pop_clip) {
    if(!path) return Error{ErrorCode::invalid_geometry,index,tag};
    op.path=*path;
   }
   plan.push_back(op);++index;
  }
  return plan;
 } catch(const std::bad_alloc&) { return Error{ErrorCode::allocation_failed,index}; }
}
}
