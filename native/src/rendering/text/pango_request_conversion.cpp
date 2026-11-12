#include "pango_request_conversion.h"
#include <cmath>
namespace aether::rendering::text {
PangoInputResult pango_input(const Request& r,int scale) noexcept {
 if(scale<=0) return PangoError::numeric_error;
 if(r.font_size()>16384 || double(r.text().size())*r.font_size()>1000000)
  return PangoError::input_limit;
 if(r.purpose()==Purpose::intrinsic) {
  if(r.width()) return PangoError::unsupported_constraint;
 } else {
  if(r.wrap() || !r.alignment() ||
     (*r.alignment()!=AETHER_TEXT_ALIGN_LEFT && *r.alignment()!=AETHER_TEXT_ALIGN_CENTER &&
      *r.alignment()!=AETHER_TEXT_ALIGN_RIGHT)) return PangoError::unsupported_constraint;
  const auto& t=r.text();
  if(t.find_first_of("\r\n\v\f")!=std::string::npos || t.find("\xc2\x85")!=std::string::npos ||
     t.find("\xe2\x80\xa8")!=std::string::npos || t.find("\xe2\x80\xa9")!=std::string::npos)
   return PangoError::unsupported_constraint;
 }
 double size=double(r.font_size())*scale;
 if(!std::isfinite(size)) return PangoError::numeric_error;
 return PangoInput{size,-1};
}
MetricsResult pango_metrics(int x,int y,int w,int h,int baseline,int scale,std::optional<float> line) noexcept {
 if(scale<=0 || w<0 || h<0 || (line && (!std::isfinite(*line)||*line<0))) return PangoError::numeric_error;
 return LogicalMetrics{double(x)/scale,double(y)/scale,double(w)/scale,double(h)/scale,double(baseline)/scale,line};
}
PlacementResult pango_placement(const Request& r,const LogicalMetrics& m,double x,double y,prep::Color c) noexcept {
 if(r.purpose()!=Purpose::paint) return PangoError::wrong_purpose;
 if(!std::isfinite(x)||!std::isfinite(y)) return PangoError::invalid_draw;
 for(float v:{c.r,c.g,c.b,c.a}) if(!std::isfinite(v)||v<0||v>1) return PangoError::invalid_draw;
 for(double v:{m.x,m.y,m.width,m.height,m.baseline}) if(!std::isfinite(v)) return PangoError::numeric_error;
 if(m.width<0||m.height<0||!r.width()||!r.height()||!r.alignment()) return PangoError::numeric_error;
 double factor;
 switch(*r.alignment()) {case AETHER_TEXT_ALIGN_LEFT:factor=0;break;
 case AETHER_TEXT_ALIGN_CENTER:factor=.5;break;case AETHER_TEXT_ALIGN_RIGHT:factor=1;break;
 default:return PangoError::unsupported_constraint;}
 DrawPlacement p{x,y,*r.width(),*r.height(),x+factor*(*r.width()-m.width)-m.x,y-m.y,
  *r.width()==0||*r.height()==0||r.text().empty()||c.a==0};
 if(!std::isfinite(p.layout_x)||!std::isfinite(p.layout_y)||!std::isfinite(x+p.width)||!std::isfinite(y+p.height))
  return PangoError::numeric_error;
 return p;
}
}
