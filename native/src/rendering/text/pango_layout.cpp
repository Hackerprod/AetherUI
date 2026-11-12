#include "pango_layout.h"
#include <exception>
#include <new>
namespace aether::rendering::text {
PangoLayoutOwner::~PangoLayoutOwner() {
 if(std::this_thread::get_id()!=owner_) std::terminate();
 if(layout_) g_object_unref(layout_);
 if(context_) g_object_unref(context_);
}
PangoLayoutOwner::CreateResult PangoLayoutOwner::create(const Request& r) noexcept {
 auto converted=pango_input(r,PANGO_SCALE);
 if(auto* error=std::get_if<PangoError>(&converted)) return *error;
 try {
  auto result=std::unique_ptr<PangoLayoutOwner>(new PangoLayoutOwner(r));
  auto* map=pango_cairo_font_map_get_default(); // borrowed
  if(!map) return PangoError::native_failure;
  result->context_=pango_font_map_create_context(map);
  if(!result->context_) return PangoError::native_failure;
  pango_cairo_context_set_resolution(result->context_,96);
  result->layout_=pango_layout_new(result->context_);
  if(!result->layout_) return PangoError::native_failure;
  std::unique_ptr<PangoFontDescription,decltype(&pango_font_description_free)> font(
   pango_font_description_new(),pango_font_description_free);
  if(!font) return PangoError::native_failure;
  pango_font_description_set_family(font.get(),r.family().c_str());
  pango_font_description_set_absolute_size(font.get(),std::get<PangoInput>(converted).absolute_size);
  pango_font_description_set_weight(font.get(),static_cast<PangoWeight>(r.weight()));
  pango_layout_set_font_description(result->layout_,font.get());
  pango_layout_set_text(result->layout_,r.text().c_str(),static_cast<int>(r.text().size()));
  pango_layout_set_width(result->layout_,std::get<PangoInput>(converted).width);
  pango_layout_set_auto_dir(result->layout_,TRUE);
  pango_layout_set_alignment(result->layout_,PANGO_ALIGN_LEFT);
  return result;
 } catch(const std::bad_alloc&) {return PangoError::allocation_failed;}
}
MetricsResult PangoLayoutOwner::metrics() const noexcept {
 if(std::this_thread::get_id()!=owner_) return PangoError::wrong_thread;
 PangoRectangle logical{};
 pango_layout_get_extents(layout_,nullptr,&logical);
 return pango_metrics(logical.x,logical.y,logical.width,logical.height,
  pango_layout_get_baseline(layout_),PANGO_SCALE,request_.used_line_height());
}
namespace {
struct DrawGuard {
 cairo_t* cr;bool group{};bool closed{};cairo_path_t* original_path{};
 explicit DrawGuard(cairo_t* c):cr(c),original_path(cairo_copy_path(c)) {cairo_save(cr);}
 ~DrawGuard() {close();}
 void close() noexcept {
  if(closed) return;
  closed=true;
  if(group) {auto* p=cairo_pop_group(cr);if(p)cairo_pattern_destroy(p);}
  cairo_restore(cr);
  if(original_path) {
   if(original_path->status==CAIRO_STATUS_SUCCESS && cairo_status(cr)==CAIRO_STATUS_SUCCESS) {
    cairo_new_path(cr);cairo_append_path(cr,original_path);
   }
   cairo_path_destroy(original_path);
  }
 }
};
}
std::optional<PangoError> PangoLayoutOwner::draw(cairo_t* cr,double x,double y,prep::Color color) const noexcept {
 if(std::this_thread::get_id()!=owner_) return PangoError::wrong_thread;
 if(!cr||cairo_status(cr)!=CAIRO_STATUS_SUCCESS) return PangoError::invalid_target;
 auto m=metrics();if(auto* e=std::get_if<PangoError>(&m)) return *e;
 auto placement=pango_placement(request_,std::get<LogicalMetrics>(m),x,y,color);
 if(auto* e=std::get_if<PangoError>(&placement)) return *e;
 const auto p=std::get<DrawPlacement>(placement);if(p.empty) return {};
 DrawGuard guard(cr);
 if(!guard.original_path||guard.original_path->status!=CAIRO_STATUS_SUCCESS||cairo_status(cr)!=CAIRO_STATUS_SUCCESS) return PangoError::native_failure;
 cairo_new_path(cr);cairo_rectangle(cr,p.x,p.y,p.width,p.height);cairo_clip(cr);
 cairo_push_group(cr);guard.group=true;
 if(cairo_status(cr)!=CAIRO_STATUS_SUCCESS) return PangoError::native_failure;
 cairo_set_operator(cr,CAIRO_OPERATOR_OVER);
 cairo_set_source_rgba(cr,color.r,color.g,color.b,1);
 cairo_move_to(cr,p.layout_x,p.layout_y);
 // Do not update_layout: drawing CTM must not mutate the shared DIP layout.
 pango_cairo_show_layout(cr,layout_);
 if(cairo_status(cr)!=CAIRO_STATUS_SUCCESS) return PangoError::native_failure;
 std::unique_ptr<cairo_pattern_t,decltype(&cairo_pattern_destroy)> pattern(cairo_pop_group(cr),cairo_pattern_destroy);
 guard.group=false;
 if(!pattern||cairo_pattern_status(pattern.get())!=CAIRO_STATUS_SUCCESS||cairo_status(cr)!=CAIRO_STATUS_SUCCESS)
  return PangoError::native_failure;
 cairo_set_operator(cr,CAIRO_OPERATOR_OVER);cairo_set_source(cr,pattern.get());
 cairo_paint_with_alpha(cr,color.a);
 if(cairo_status(cr)!=CAIRO_STATUS_SUCCESS) return PangoError::native_failure;
 guard.close();
 if(cairo_status(cr)!=CAIRO_STATUS_SUCCESS) return PangoError::native_failure;
 return {};
}
}
