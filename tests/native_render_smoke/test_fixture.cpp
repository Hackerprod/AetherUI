#ifdef NDEBUG
#error "Assertions required"
#endif
#include "fixture.h"
#include "../../native/src/rendering/cairo/cairo_geometry.h"
#include "../../native/src/rendering/text/pango_request_conversion.h"
#include <cassert>
int main() {
 namespace sm=aether::smoke;namespace ar=aether::rendering;
 auto prepared=sm::prep::prepare(sm::geometry_fixture());
 assert(std::holds_alternative<sm::prep::PreparedFrame>(prepared));
 auto plan=ar::cairo_adapter::preflight(std::get<sm::prep::PreparedFrame>(prepared),1,{1,1,1,1});
 assert(std::holds_alternative<ar::cairo_adapter::Plan>(plan));
 assert(std::get<ar::cairo_adapter::Plan>(plan).size()==6);
 for(auto p:sm::geometry_probes)assert(p.x>=0&&p.x<sm::geometry_width&&p.y>=0&&p.y<sm::geometry_height);
 auto text=ar::text::paint(sm::text_fixture());assert(std::holds_alternative<ar::text::Request>(text));
 assert(std::holds_alternative<ar::text::PangoInput>(ar::text::pango_input(std::get<ar::text::Request>(text),1024)));
 assert(sm::text_fixture().text=="Aether 123");
}
