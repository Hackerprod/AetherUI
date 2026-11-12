#ifdef NDEBUG
#error "Assertions required"
#endif
#include "../src/rendering/text/pango_request_conversion.h"
#include <cassert>
#include <limits>
using namespace aether::rendering;
namespace tx=text;
const tx::Request& request(const tx::Result& r) {assert(std::holds_alternative<tx::Request>(r));return std::get<tx::Request>(r);}
int main() {
 auto intrinsic=tx::intrinsic({"a\nb","sans",12,400,14,{}});
 auto cv=tx::pango_input(request(intrinsic),1024);
 assert(std::get<tx::PangoInput>(cv).absolute_size==12288 && std::get<tx::PangoInput>(cv).width==-1);
 auto width0=tx::intrinsic({"a","sans",12,400,0,0});
 assert(std::get<tx::PangoError>(tx::pango_input(request(width0),1024))==tx::PangoError::unsupported_constraint);
 assert(std::get<tx::PangoError>(tx::pango_input(request(intrinsic),0))==tx::PangoError::numeric_error);
 auto huge=tx::intrinsic({"a","sans",16385,400,0,{}});
 assert(std::get<tx::PangoError>(tx::pango_input(request(huge),1024))==tx::PangoError::input_limit);
 std::string bytes(1000,'x');auto budget=tx::intrinsic({bytes,"sans",1001,400,0,{}});
 assert(std::get<tx::PangoError>(tx::pango_input(request(budget),1024))==tx::PangoError::input_limit);
 preparation::Text t{"\xd7\x90",{0,0,100,30},"sans",12,400,AETHER_TEXT_ALIGN_LEFT,{0,0,0,1},AETHER_FALSE};
 auto paint=tx::paint(t);assert(std::holds_alternative<tx::PangoInput>(tx::pango_input(request(paint),1024)));
 auto metrics=tx::pango_metrics(-2048,1024,20480,10240,8192,1024,{});
 auto m=std::get<tx::LogicalMetrics>(metrics);assert(m.x==-2&&m.y==1&&m.width==20&&m.height==10&&m.baseline==8);
 auto p=std::get<tx::DrawPlacement>(tx::pango_placement(request(paint),m,10,20,{0,0,0,1}));
 assert(p.layout_x==12&&p.layout_y==19);
 t.text_align=AETHER_TEXT_ALIGN_CENTER;
 p=std::get<tx::DrawPlacement>(tx::pango_placement(request(tx::paint(t)),m,10,20,{0,0,0,1}));assert(p.layout_x==52);
 t.text_align=AETHER_TEXT_ALIGN_RIGHT;
 p=std::get<tx::DrawPlacement>(tx::pango_placement(request(tx::paint(t)),m,10,20,{0,0,0,1}));assert(p.layout_x==92);
 for(auto align:{AETHER_TEXT_ALIGN_START,AETHER_TEXT_ALIGN_END,AETHER_TEXT_ALIGN_JUSTIFY}) {
  t.text_align=align;assert(std::get<tx::PangoError>(tx::pango_input(request(tx::paint(t)),1024))==tx::PangoError::unsupported_constraint);
 }
 t.text_align=AETHER_TEXT_ALIGN_LEFT;t.wrap=AETHER_TRUE;
 assert(std::get<tx::PangoError>(tx::pango_input(request(tx::paint(t)),1024))==tx::PangoError::unsupported_constraint);t.wrap=AETHER_FALSE;
 for(auto sep:{"\n","\r","\v","\f","\xc2\x85","\xe2\x80\xa8","\xe2\x80\xa9"}) {
  t.text=sep;assert(std::get<tx::PangoError>(tx::pango_input(request(tx::paint(t)),1024))==tx::PangoError::unsupported_constraint);
 }
 t.text="a";t.rect.width=0;
 auto zero=tx::paint(t);assert(std::get<tx::PangoInput>(tx::pango_input(request(zero),1024)).width==-1);
 assert(std::get<tx::DrawPlacement>(tx::pango_placement(request(zero),m,0,0,{0,0,0,1})).empty);
 assert(std::get<tx::PangoError>(tx::pango_placement(request(intrinsic),m,0,0,{0,0,0,1}))==tx::PangoError::wrong_purpose);
 assert(std::get<tx::PangoError>(tx::pango_metrics(0,0,-1,0,0,1024,{}))==tx::PangoError::numeric_error);
 assert(std::get<tx::PangoError>(tx::pango_placement(request(paint),m,std::numeric_limits<double>::infinity(),0,{0,0,0,1}))==tx::PangoError::invalid_draw);
 assert(std::get<tx::PangoError>(tx::pango_placement(request(paint),m,0,0,{2,0,0,1}))==tx::PangoError::invalid_draw);
}
