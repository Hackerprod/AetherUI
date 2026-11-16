#ifdef NDEBUG
#error Assertions required
#endif
#include "real_font_metrics.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <iterator>
using namespace aether::rendering::text;
namespace tt=aether::rendering::text::truetype;
using Bytes=std::vector<std::uint8_t>;
void put16(Bytes& b,std::size_t o,int v){b.at(o)=unsigned(v)>>8;b.at(o+1)=v;}
void put32(Bytes& b,std::size_t o,unsigned v){put16(b,o,v>>16);put16(b,o+2,v);}
unsigned tag(const char* s){return unsigned(s[0])<<24|unsigned(s[1])<<16|unsigned(s[2])<<8|unsigned(s[3]);}
Bytes fixture(int version,int len,bool bit,int asc=800,int desc=-200,int gap=17){
 Bytes head(54),maxp(32),hh(36),hm(4),lo(8),cm(40),os(len<0?0:len);
 put32(head,0,0x10000);put32(head,12,0x5f0f3cf5);put16(head,18,1000);put16(head,50,1);
 put32(maxp,0,0x10000);put16(maxp,4,1);
 put32(hh,0,0x10000);put16(hh,4,900);put16(hh,6,-250);put16(hh,8,-12);put16(hh,34,1);put16(hm,0,500);
 put16(cm,2,1);put16(cm,4,3);put16(cm,6,10);put32(cm,8,12);put16(cm,12,12);put32(cm,16,28);put32(cm,24,1);put32(cm,28,65);put32(cm,32,65);put32(cm,36,0);
 if(len>=2)put16(os,0,version);
 if(len>=64)put16(os,62,bit?128:0);
 if(len>=74){put16(os,68,asc);put16(os,70,desc);put16(os,72,gap);}
 std::vector<std::pair<unsigned,Bytes>> tables{{tag("head"),head},{tag("maxp"),maxp},{tag("hhea"),hh},{tag("hmtx"),hm},{tag("loca"),lo},{tag("glyf"),{}},{tag("cmap"),cm}};
 if(len>=0)tables.push_back({tag("OS/2"),os});
 Bytes out(12+16*tables.size());put32(out,0,0x10000);put16(out,4,tables.size());
 for(unsigned i=0;i<tables.size();++i){while(out.size()%4)out.push_back(0);put32(out,12+16*i,tables[i].first);put32(out,20+16*i,out.size());put32(out,24+16*i,tables[i].second.size());out.insert(out.end(),tables[i].second.begin(),tables[i].second.end());}return out;
}
auto read(Bytes b){auto source=std::make_shared<const FontSource>(std::move(b));auto v=tt::RealFontBinding::create(source,1024);assert(std::holds_alternative<std::shared_ptr<const tt::RealFontBinding>>(v));return tt::read_vertical_metrics(std::get<std::shared_ptr<const tt::RealFontBinding>>(v));}
unsigned checks=0;
void expect(Bytes b,bool typo){auto r=read(std::move(b));assert(std::holds_alternative<tt::FontVerticalMetrics>(r));auto m=std::get<tt::FontVerticalMetrics>(r);assert(m.units_per_em==1000);assert(m.ascent==(typo?800:900)&&m.descent==(typo?200:250)&&m.line_gap==(typo?17:-12));assert(m.policy==(typo?tt::VerticalMetricPolicy::os2_typo:tt::VerticalMetricPolicy::hhea));++checks;}
void error(Bytes b,tt::BindingError e){assert(std::get<tt::BindingError>(read(std::move(b)))==e);++checks;}
int main(int argc,char** argv){
 assert(std::get<tt::BindingError>(tt::read_vertical_metrics({}))==tt::BindingError::invalid_input);++checks;
 expect(fixture(0,-1,false),false);expect(fixture(0,68,true),false);
 int lengths[]{78,86,96,96,96,100};
 for(int v=0;v<=5;++v){for(bool bit:{false,true})expect(fixture(v,lengths[v],bit),v>=4&&bit);for(int n=0;n<lengths[v];++n){if(v==0&&n==68)continue;error(fixture(v,n,true),tt::BindingError::malformed);}}
 error(fixture(6,100,true),tt::BindingError::unsupported);
 error(fixture(4,96,true,-1,-200),tt::BindingError::unsupported);error(fixture(4,96,true,800,1),tt::BindingError::unsupported);error(fixture(4,96,true,0,0),tt::BindingError::unsupported);
 auto edge=std::get<tt::FontVerticalMetrics>(read(fixture(4,96,true,32767,-32768,-32768)));assert(edge.ascent==32767&&edge.descent==32768&&edge.line_gap==-32768);++checks;
 std::cout<<"metric policy checks="<<checks<<"\n";
 if(argc==2){std::ifstream f(argv[1],std::ios::binary);Bytes b((std::istreambuf_iterator<char>(f)),{});auto m=std::get<tt::FontVerticalMetrics>(read(std::move(b)));std::cout<<m.units_per_em<<' '<<m.ascent<<' '<<m.descent<<' '<<m.line_gap<<' '<<int(m.policy)<<' '<<m.os2_version<<'\n';}
}
