#include "../src/rendering/text/real_font_metrics.h"
#include "../src/rendering/text/truetype_raster.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace aether::rendering::text;namespace tt=aether::rendering::text::truetype;
unsigned checks;void need(bool b){++checks;if(!b)throw std::runtime_error("budget gate");}
struct Stop:tt::Cancellation{tt::RasterWork*w;bool saw{};bool cancelled()noexcept override{if(w->prepass_reserved&&w->prepass_work&&!w->main_reserved){saw=true;return true;}return false;}};
int main()try{std::ifstream f("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",std::ios::binary);auto s=std::make_shared<const FontSource>(std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{}));auto b=std::get<std::shared_ptr<const tt::RealFontBinding>>(tt::RealFontBinding::create(s,16*64,1));auto gid=std::get<tt::Mapped>(b->map()->lookup('@')).glyph_id;auto o=std::get<std::shared_ptr<const tt::Outline>>(b->face()->outline(gid));tt::RasterWork w;tt::AggregateBudget r{512000000},m{64000000};auto good=tt::rasterize_document_budgeted(o,b->font(),0,0,r,m,nullptr,&w);need(std::holds_alternative<tt::RasterOutput>(good));auto B=w.prepass_reserved,M=w.main_reserved;need(B&&M&&w.reserved==B+M&&w.aggregate_charged==B+M&&w.units<=B+M);
for(auto cap:{B-1,B,B+M-1,B+M}){tt::AggregateBudget a{cap},z{64000000};tt::RasterWork x;auto v=tt::rasterize_document_budgeted(o,b->font(),0,0,a,z,nullptr,&x);if(cap==B+M){need(std::holds_alternative<tt::RasterOutput>(v));need(a.remaining==0&&x.aggregate_charged==cap);}else{need(std::get<tt::RasterError>(v)==tt::RasterError::budget_exceeded);need(x.aggregate_charged==(cap<B?0:B));need(a.remaining==cap-x.aggregate_charged&&x.main_reserved==0&&z.remaining==64000000);}}
{tt::AggregateBudget a{B+M},z{0};tt::RasterWork x;auto v=tt::rasterize_document_budgeted(o,b->font(),0,0,a,z,nullptr,&x);need(std::get<tt::RasterError>(v)==tt::RasterError::budget_exceeded);need(a.remaining==0&&x.aggregate_charged==B+M);}
{tt::AggregateBudget a{512000000},z{64000000};tt::RasterWork x;Stop stop;stop.w=&x;auto v=tt::rasterize_document_budgeted(o,b->font(),0,0,a,z,&stop,&x);need(stop.saw&&std::get<tt::RasterError>(v)==tt::RasterError::cancelled);need(x.aggregate_charged==B&&a.remaining==512000000-B&&x.main_reserved==0&&z.remaining==64000000);}
std::cout<<"Document reservation gates="<<checks<<" B="<<B<<" M="<<M<<" PASS\n";}
catch(const std::exception& e){std::cerr<<"checks="<<checks<<" error="<<e.what()<<"\\n";return 1;}
