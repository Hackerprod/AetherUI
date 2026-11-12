#include "cairo_frame_renderer.h"
#include <cairo.h>
#include <bit>
#include <memory>
namespace aether::rendering::cairo_adapter {
namespace {
struct CandidateGuard {
 FrameStore& store; FrameStore::Token token; bool active{true};
 ~CandidateGuard() { if(active) (void)store.discard(token); }
};
void path(cairo_t* cr,const Path& p) {
 cairo_new_path(cr);
 for(std::size_t i=0;i<p.count;++i) {
  const auto& s=p.segments[i];const auto& c=s.coordinates;
  switch(s.kind) {
   case SegmentKind::move:cairo_move_to(cr,c[0],c[1]);break;
   case SegmentKind::line:cairo_line_to(cr,c[0],c[1]);break;
   case SegmentKind::curve:cairo_curve_to(cr,c[0],c[1],c[2],c[3],c[4],c[5]);break;
   case SegmentKind::close:cairo_close_path(cr);break;
  }
 }
}
void matrix(cairo_t* cr,const Matrix& m) {
 const cairo_matrix_t value{m.xx,m.yx,m.xy,m.yy,m.x0,m.y0};
 cairo_set_matrix(cr,&value);
}
RenderResult frame_failure(FrameStatus s) {
 return {s==FrameStatus::wrong_thread?RenderStatus::wrong_thread:RenderStatus::frame_failed,{},s};
}
RenderResult cairo_failure(cairo_status_t s) {return {RenderStatus::cairo_failed,{},FrameStatus::ok,int(s)};}
}
RenderResult render_cairo(FrameStore& store,const prep::PreparedFrame& frame,prep::Color clear) noexcept {
 Viewport viewport;
 if(auto s=store.current_viewport(viewport);s!=FrameStatus::ok) return frame_failure(s);
 auto prepared=preflight(frame,viewport.dpi,clear);
 if(auto* e=std::get_if<Error>(&prepared)) return {RenderStatus::preflight_failed,*e};
 if constexpr(std::endian::native!=std::endian::little) return {RenderStatus::incompatible_platform};
 if(!viewport.width || !viewport.height) return {RenderStatus::suspended};
 FrameStore::Token token;
 if(auto s=store.prepare(token);s!=FrameStatus::ok) return frame_failure(s);
 CandidateGuard guard{store,token};
 std::span<std::uint8_t> bytes;
 if(auto s=store.candidate(token,bytes);s!=FrameStatus::ok) return frame_failure(s);
 {
  const int stride=cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32,viewport.width);
  if(stride<=0 || std::size_t(stride)!=std::size_t(viewport.width)*4 ||
     bytes.size()!=std::size_t(stride)*std::size_t(viewport.height))
   return cairo_failure(CAIRO_STATUS_INVALID_STRIDE);
  std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)> surface(
   cairo_image_surface_create_for_data(bytes.data(),CAIRO_FORMAT_ARGB32,
    viewport.width,viewport.height,stride),cairo_surface_destroy);
  if(!surface) return cairo_failure(CAIRO_STATUS_NO_MEMORY);
  if(auto s=cairo_surface_status(surface.get());s!=CAIRO_STATUS_SUCCESS) return cairo_failure(s);
  std::unique_ptr<cairo_t,decltype(&cairo_destroy)> cr(cairo_create(surface.get()),cairo_destroy);
  if(!cr) return cairo_failure(CAIRO_STATUS_NO_MEMORY);
  if(auto s=cairo_status(cr.get());s!=CAIRO_STATUS_SUCCESS) return cairo_failure(s);
  cairo_set_operator(cr.get(),CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr.get(),clear.r,clear.g,clear.b,clear.a);
  cairo_paint(cr.get());
  cairo_set_operator(cr.get(),CAIRO_OPERATOR_OVER);
  for(const auto& op:std::get<Plan>(prepared)) {
   switch(op.action) {
    case Action::fill: case Action::stroke:
     matrix(cr.get(),op.matrix);path(cr.get(),op.path);
     cairo_set_source_rgba(cr.get(),op.color.r,op.color.g,op.color.b,op.color.a);
     if(op.action==Action::fill) cairo_fill(cr.get());
     else {cairo_set_line_width(cr.get(),op.width);cairo_stroke(cr.get());}
     break;
    case Action::push_clip:
     cairo_save(cr.get());matrix(cr.get(),op.matrix);path(cr.get(),op.path);cairo_clip(cr.get());break;
    case Action::empty_clip:
     cairo_save(cr.get());cairo_new_path(cr.get());cairo_clip(cr.get());break;
    case Action::pop_clip:cairo_restore(cr.get());break;
    case Action::none:break;
   }
   if(auto s=cairo_status(cr.get());s!=CAIRO_STATUS_SUCCESS) return cairo_failure(s);
  }
  if(auto s=cairo_status(cr.get());s!=CAIRO_STATUS_SUCCESS) return cairo_failure(s);
  if(auto s=cairo_surface_status(surface.get());s!=CAIRO_STATUS_SUCCESS) return cairo_failure(s);
  cr.reset();
  cairo_surface_flush(surface.get());
  if(auto s=cairo_surface_status(surface.get());s!=CAIRO_STATUS_SUCCESS) return cairo_failure(s);
 } // Context and surface MUST die before candidate ownership changes.
 bytes={};
 if(auto s=store.commit(token);s!=FrameStatus::ok) return frame_failure(s);
 guard.active=false;
 return {};
}
}
