#include "../src/rendering/text/selected_html_capture.h"
#include "../src/rendering/text/font_source.h"
#include <fstream>
#include <iterator>
#include <iostream>
#include <cassert>
#include <cmath>
using namespace aether::rendering;
std::string read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {(std::istreambuf_iterator<char>(f)),{}};}
void replace(std::string& s,const std::string&a,const std::string&b){auto p=s.find(a);assert(p!=s.npos);s.replace(p,a.size(),b);}
int main(int argc,char**argv){assert(argc==4);auto raw=read(argv[1]);assert(raw.size()==474495);auto fontbytes=read(argv[2]);auto font=std::make_shared<const text::FontSource>(std::vector<std::uint8_t>(fontbytes.begin(),fontbytes.end()));
 std::shared_ptr<const SelectedCaptureFrame> baseline;
 for(int mutated=0;mutated<2;++mutated){std::string html=raw;if(mutated){replace(html,"#FLEX-001 .flex-target { display:flex; width:420px; min-height:72px; }","#FLEX-001 .flex-target { display:flex; width:421px; min-height:72px; }");replace(html,".item:nth-child(2n) { background: #d9efd9; }",".item:nth-child(2n) { background: #112233; }");replace(html,"<div class=\"item\">A</div>","<div class=\"item\">W</div>");replace(html,"</head>","<style>#FLEX-001 .item{min-width:0}</style></head>");}
 auto result=capture_selected_html(html,"FLEX-001",".stage > .flex-target",font,1280,720,mutated?421:420,72);
 if(auto* e=std::get_if<SelectedCaptureFailure>(&result)){std::cerr<<"FAIL capture mutation="<<mutated<<" code="<<int(e->code)<<" reason="<<e->reason<<"\n";return 1;}
 auto frame=std::get<std::shared_ptr<const SelectedCaptureFrame>>(result);assert(frame);if(!mutated)baseline=frame;assert(!frame->coverage_complete);assert(frame->work.unknown_rules>0);assert(frame->text.size()==3);assert(frame->frame.viewport.width==(mutated?421:420)&&frame->frame.viewport.height==72);
 std::cout<<"scope="<<(mutated?"mutated":"original")<<" coverage=INCOMPLETE unknown="<<frame->work.unknown_rules<<" glyphs="<<frame->work.glyph_commands<<"\n";
 for(size_t i=0;i<frame->text.size();++i){const auto&t=frame->text[i];const float expectedX[2][3]={{15.f,57.f,99.f},{15.f,56.828125f,93.8125f}};const float expectedAdvance[2][3]={{10.953125f,10.984375f,11.171875f},{15.828125f,10.984375f,11.171875f}};assert(std::abs(t.logical_rect.x-expectedX[mutated][i])<.0001f);assert(std::abs(t.logical_rect.y-12.f)<.0001f);assert(std::abs(t.advance-expectedAdvance[mutated][i])<.0001f);assert(t.source==(i==0?(mutated?"W":"A"):(i==1?"B":"C")));assert(std::abs(t.logical_rect.height-23.2f)<.001f);assert(std::abs(t.logical_baseline-(t.logical_rect.y+17.1390625f))<.001f);assert(t.phase_x<64&&t.phase_y<64);const auto expectedx=std::llround(double(t.logical_rect.x)*64);const auto expectedy=std::llround(double(t.logical_baseline)*64);assert(t.raster_x26_6==expectedx&&t.raster_y26_6==expectedy);std::cout<<"text="<<t.source<<" rect="<<t.logical_rect.x<<","<<t.logical_rect.y<<","<<t.logical_rect.width<<","<<t.logical_rect.height<<" baseline="<<t.logical_baseline<<" raster="<<t.raster_x26_6<<","<<t.raster_y26_6<<" phase="<<t.phase_x<<","<<t.phase_y<<"\n";}
 std::ofstream out(std::string(argv[3])+(mutated?"-mutated.bgra":"-original.bgra"),std::ios::binary);out.write((const char*)frame->frame.pixels.data(),frame->frame.pixels.size());
 }
 for(const std::string css:{"text-align:left","text-align:end","text-align:center","direction:rtl"}){auto input=raw;replace(input,"</head>","<style>#FLEX-001 .item{"+css+"}</style></head>");auto r=capture_selected_html(input,"FLEX-001",".stage > .flex-target",font,1280,720,420,72);if(css=="text-align:left"){assert(std::holds_alternative<std::shared_ptr<const SelectedCaptureFrame>>(r));auto other=std::get<std::shared_ptr<const SelectedCaptureFrame>>(r);assert(other->frame.pixels==baseline->frame.pixels);assert(other->text.size()==baseline->text.size());for(size_t i=0;i<other->text.size();++i){assert(other->text[i].logical_rect.x==baseline->text[i].logical_rect.x);assert(other->text[i].logical_baseline==baseline->text[i].logical_baseline);}std::cout<<"PASS START/LEFT pixels and logical placements identical\n";}else{assert(std::holds_alternative<SelectedCaptureFailure>(r));std::cout<<"PASS rejected "<<css<<" reason="<<std::get<SelectedCaptureFailure>(r).reason<<"\n";}}
 std::cout<<"PASS original and semantic mutation captures; both CSS INCOMPLETE, not parity\n";
}
