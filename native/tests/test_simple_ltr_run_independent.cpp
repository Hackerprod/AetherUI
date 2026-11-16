#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/simple_ltr_run.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <fstream>
#include <iterator>
#include <type_traits>
using namespace aether::rendering::text;
namespace tt=aether::rendering::text::truetype;
// Shared fixture serialization only. Run oracle does not call production UTF8, cmap, metrics or composition logic.
using Bytes=std::vector<std::uint8_t>;
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static void u16(Bytes& b,std::size_t p,unsigned v){assert(p+2<=b.size());b[p]=v>>8;b[p+1]=v;}
static void u32(Bytes& b,std::size_t p,std::uint32_t v){assert(p+4<=b.size());u16(b,p,v>>16);u16(b,p+2,v);}
static unsigned r16(const Bytes& b,std::size_t p){return (unsigned(b[p])<<8)|b[p+1];}
static std::uint32_t r32(const Bytes& b,std::size_t p){return (std::uint32_t(r16(b,p))<<16)|r16(b,p+2);}
static void append16(Bytes& b,int v){auto p=b.size();b.resize(p+2);u16(b,p,unsigned(v));}
struct Table{std::uint32_t tag;Bytes bytes;};
static constexpr std::uint32_t tag(const char* s){return std::uint32_t(s[0])<<24|std::uint32_t(s[1])<<16|std::uint32_t(s[2])<<8|std::uint32_t(s[3]);}
static Bytes sfnt(std::vector<Bytes> glyphs,bool short_loca=false,unsigned hmetrics=0,std::vector<int> lsbs={}){
 const unsigned n=glyphs.size();if(!hmetrics)hmetrics=n;if(lsbs.empty())lsbs.resize(n);
 Bytes head(54),maxp(32),hhea(36),hmtx(4*hmetrics+2*(n-hmetrics)),loca((n+1)*(short_loca?2:4)),glyf;
 u32(head,0,0x10000);u32(head,12,0x5f0f3cf5);u16(head,18,1024);u16(head,50,short_loca?0:1);u32(maxp,0,0x10000);u16(maxp,4,n);u32(hhea,0,0x10000);u16(hhea,34,hmetrics);
 for(unsigned i=0;i<n;++i){if(short_loca)u16(loca,i*2,glyf.size()/2);else u32(loca,i*4,glyf.size());glyf.insert(glyf.end(),glyphs[i].begin(),glyphs[i].end());if(glyf.size()%2)glyf.push_back(0);if(i<hmetrics){u16(hmtx,i*4,500+100*i);u16(hmtx,i*4+2,lsbs[i]);}else u16(hmtx,4*hmetrics+2*(i-hmetrics),lsbs[i]);}
 if(short_loca)u16(loca,n*2,glyf.size()/2);else u32(loca,n*4,glyf.size());
 std::vector<Table> tables{{tag("head"),head},{tag("maxp"),maxp},{tag("hhea"),hhea},{tag("hmtx"),hmtx},{tag("loca"),loca},{tag("glyf"),glyf}};
 Bytes out(12+16*tables.size());u32(out,0,0x10000);u16(out,4,tables.size());
 for(unsigned i=0;i<tables.size();++i){while(out.size()%4)out.push_back(0);u32(out,12+16*i,tables[i].tag);u32(out,20+16*i,out.size());u32(out,24+16*i,tables[i].bytes.size());out.insert(out.end(),tables[i].bytes.begin(),tables[i].bytes.end());}return out;
}
static void add_table(Bytes& b,std::uint32_t name,Bytes payload={0,0,0,0}){
 const unsigned n=r16(b,4);const unsigned end=12+16*n;
 b.insert(b.begin()+end,16,0);u16(b,4,n+1);
 for(unsigned i=0;i<n;++i)u32(b,20+16*i,r32(b,20+16*i)+16);
 while(b.size()%4){b.push_back(0);}
 u32(b,end,name);u32(b,end+8,b.size());u32(b,end+12,payload.size());b.insert(b.end(),payload.begin(),payload.end());
}
static auto source(Bytes b){return std::make_shared<const FontSource>(std::move(b),"synthetic Q1c test fixture");}
[[maybe_unused]] static auto face(const Bytes& b){auto r=tt::ValidatedFace::create(source(b));assert(std::holds_alternative<std::shared_ptr<const tt::ValidatedFace>>(r));return std::get<std::shared_ptr<const tt::ValidatedFace>>(r);}
struct Group { unsigned first,last,gid; };
static Bytes format12(std::vector<Group> groups){
 Bytes b(16+12*groups.size());u16(b,0,12);u32(b,4,b.size());u32(b,12,groups.size());
 for(unsigned i=0;i<groups.size();++i){u32(b,16+12*i,groups[i].first);u32(b,20+12*i,groups[i].last);u32(b,24+12*i,groups[i].gid);}return b;
}
struct Seg {unsigned first,last,delta;std::vector<unsigned> gids;};
[[maybe_unused]] static Bytes format4(std::vector<Seg> segments){
 const unsigned n=segments.size();Bytes b(16+8*n);u16(b,0,4);u16(b,6,n*2);
 // Search accelerators deliberately zero: readers must not trust them.
 for(unsigned i=0;i<n;++i){auto& s=segments[i];u16(b,14+2*i,s.last);u16(b,16+2*n+2*i,s.first);u16(b,16+4*n+2*i,s.delta);
  if(!s.gids.empty()){const auto pos=16+6*n+2*i;u16(b,pos,b.size()-pos);for(auto g:s.gids)append16(b,g);}}
 u16(b,2,b.size());return b;
}
struct Rec {unsigned platform,encoding,table;};
static Bytes cmap(std::vector<Bytes> tables,std::vector<Rec> records){
 Bytes b(4+8*records.size());u16(b,2,records.size());std::vector<unsigned> offsets;
 for(const auto& t:tables){offsets.push_back(b.size());b.insert(b.end(),t.begin(),t.end());}
 for(unsigned i=0;i<records.size();++i){u16(b,4+8*i,records[i].platform);u16(b,6+8*i,records[i].encoding);u32(b,8+8*i,offsets.at(records[i].table));}return b;
}
[[maybe_unused]] static Bytes font(const Bytes& c,unsigned count=16){auto b=sfnt(std::vector<Bytes>(count));add_table(b,tag("cmap"),c);return b;}

struct P{int x,y;bool on;};
// Independent fixture writer uses signed 16-bit deltas (no compact flag tricks).
static Bytes glyph(const std::vector<std::vector<P>>& contours){
 Bytes b(10);u16(b,0,contours.size());int xmin=32767,ymin=32767,xmax=-32768,ymax=-32768;unsigned count=0;
 for(const auto& c:contours){assert(!c.empty());count+=c.size();append16(b,count-1);for(auto p:c){xmin=std::min(xmin,p.x);ymin=std::min(ymin,p.y);xmax=std::max(xmax,p.x);ymax=std::max(ymax,p.y);}}
 if(!contours.empty()){u16(b,2,xmin);u16(b,4,ymin);u16(b,6,xmax);u16(b,8,ymax);}append16(b,0);
 for(const auto& c:contours)for(auto p:c)b.push_back(p.on?1:0);
 int x=0,y=0;for(const auto& c:contours)for(auto p:c){assert(p.x-x>=-32768&&p.x-x<=32767);append16(b,p.x-x);x=p.x;}
 for(const auto& c:contours)for(auto p:c){assert(p.y-y>=-32768&&p.y-y<=32767);append16(b,p.y-y);y=p.y;}
 return b;
}
using Binding=std::shared_ptr<const tt::RealFontBinding>;
using Run=std::shared_ptr<const tt::PreparedSimpleRun>;
using E=tt::RunError;
static_assert(!std::is_default_constructible_v<tt::PreparedSimpleRun>);
static_assert(!std::is_copy_constructible_v<tt::PreparedSimpleRun>);
static_assert(!std::is_move_constructible_v<tt::PreparedSimpleRun>);
static unsigned fixtures=0,successes=0;
static Bytes testfont(bool missing=false,unsigned advance=600,unsigned units=1024){
 auto b=sfnt({{},glyph({{{-80,-120,true},{800,-120,true},{800,600,true},{-80,600,true}}}),{}},false,0,{0,-80,0});
 for(unsigned i=0;i<r16(b,4);++i)if(r32(b,12+16*i)==tag("head"))u16(b,r32(b,20+16*i)+18,units);
 // Deliberately overlap the rectangle glyph at small advances; whitespace is empty.
 for(unsigned i=0;i<r16(b,4);++i)if(r32(b,12+16*i)==tag("hmtx")){auto off=r32(b,20+16*i);u16(b,off+4,advance);u16(b,off+8,256);}
 std::vector<Group> groups;for(unsigned cp=32;cp<=255;++cp)if((cp<=126||cp>=160)&&cp!=173&&!(missing&&cp==66))groups.push_back({cp,cp,(cp==32||cp==160)?2u:1u});
 add_table(b,tag("cmap"),cmap({format12(groups)},{{3,10,0}}));return b;
}
static Binding bind(unsigned size=1024,bool missing=false,unsigned adv=600,unsigned units=1024){auto r=tt::RealFontBinding::create(source(testfont(missing,adv,units)),size);assert(std::holds_alternative<Binding>(r));return std::get<Binding>(r);}
// Independent decoder via leading-byte length, scalar re-encoding checks.
static std::vector<std::pair<unsigned,unsigned>> decode(std::string_view s){std::vector<std::pair<unsigned,unsigned>> out;for(unsigned p=0;p<s.size();){unsigned begin=p,c=(unsigned char)s[p++],n=0,min=0;if(c>=128){if(c<224){n=1;c&=31;min=128;}else if(c<240){n=2;c&=15;min=2048;}else{n=3;c&=7;min=65536;}if(begin+n>=s.size())throw 1;for(unsigned i=0;i<n;++i){auto d=(unsigned char)s[p++];if((d&192)!=128)throw 1;c=c*64+(d&63);}if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))throw 1;}if((unsigned char)s[begin]>=128&&(unsigned char)s[begin]<194)throw 1;if((unsigned char)s[begin]>244)throw 1;out.push_back({c,begin});}return out;}
static Run verify(Binding b,std::string_view s,tt::LineBox box,unsigned advance=600,unsigned units=1024){
 tt::RunWork work;auto result=tt::build_simple_ltr_run(b,s,box,nullptr,&work);assert(std::holds_alternative<Run>(result));auto r=std::get<Run>(result);auto cps=decode(s);assert(r->binding==b&&r->source_text==s&&r->glyphs.size()==cps.size());
 std::int64_t pen=0;Bytes alpha(r->canvas.width*r->canvas.height);std::int64_t xmin=0,ymin=0,xmax=0,ymax=0;bool bounds=false;
 for(unsigned i=0;i<cps.size();++i){auto [cp,cluster]=cps[i];const unsigned gid=(cp==32||cp==160)?2:1;const std::int64_t adv=(std::int64_t(gid==2?256:advance)*b->font()->size_26_6+units/2)/units;const auto& g=r->glyphs[i];assert(g.glyph_id==gid&&g.cluster_byte==cluster&&g.pen_26_6==pen&&g.advance_26_6==adv);assert(g.mask->phase_x==pen%64&&g.mask->phase_y==box.baseline26_6%64);assert(g.bitmap_x==pen/64+g.mask->bearing_left&&g.bitmap_y==box.baseline26_6/64-g.mask->bearing_top);
 if(gid==2)assert(g.mask->bytes.empty());
 // Independent analytic coverage of authored rectangle, not Outline/raster output.
 if(gid==1){const long double scale=static_cast<long double>(b->font()->size_26_6)/(units*64);
 const long double l=-80*scale+static_cast<long double>(pen%64)/64,rr=800*scale+static_cast<long double>(pen%64)/64;
 const long double t=-600*scale+static_cast<long double>(box.baseline26_6%64)/64,bb=120*scale+static_cast<long double>(box.baseline26_6%64)/64;
 for(unsigned yy=0;yy<g.mask->height;++yy)for(unsigned xx=0;xx<g.mask->width;++xx){long double x=g.mask->bearing_left+int(xx),y=-g.mask->bearing_top+int(yy);
 const auto area=std::max(0.L,std::min(x+1,rr)-std::max(x,l))*std::max(0.L,std::min(y+1,bb)-std::max(y,t));
 const int expected=static_cast<int>(area*255+0.5L);assert(std::abs(int(g.mask->bytes[yy*g.mask->stride+xx])-expected)<=2);}}

 if(g.mask->width&&g.mask->height){auto x=g.bitmap_x,y=g.bitmap_y;auto right=x+g.mask->width,bottom=y+g.mask->height;if(!bounds){xmin=x;ymin=y;xmax=right;ymax=bottom;bounds=true;}else{xmin=std::min(xmin,x);ymin=std::min(ymin,y);xmax=std::max(xmax,right);ymax=std::max(ymax,bottom);}}
 for(unsigned y=0;y<g.mask->height;++y)for(unsigned x=0;x<g.mask->width;++x){const auto dx=g.bitmap_x+x-r->canvas.origin_x,dy=g.bitmap_y+y-r->canvas.origin_y;assert(dx>=0&&dy>=0&&dx<r->canvas.width&&dy<r->canvas.height);auto& a=alpha[dy*r->canvas.width+dx];auto v=g.mask->bytes[y*g.mask->stride+x];a=255-((255-a)*(255-v)+127)/255;}
 pen+=adv;
 }
 assert(work.canvas_complete&&work.composition_started&&work.decoded==cps.size()&&work.lookups==cps.size());assert(cps.empty()?work.lookup_comparisons==0:work.lookup_comparisons>0);assert(work.clear_pixels==alpha.size()&&work.conversion_pixels==alpha.size());
 assert(r->width_26_6==pen);if(bounds)assert(r->mask_bounds.x==xmin&&r->mask_bounds.y==ymin&&r->mask_bounds.width==xmax-xmin&&r->mask_bounds.height==ymax-ymin);
 const auto cx=std::min<std::int64_t>(0,xmin)-4,cy=std::min<std::int64_t>(0,ymin)-4;
 assert(r->canvas.origin_x==cx&&r->canvas.origin_y==cy);
 assert(r->canvas.width==std::max<std::int64_t>(std::max<std::int64_t>(1,(pen+63)/64),xmax)+4-cx);
 assert(r->canvas.height==std::max<std::int64_t>((box.height26_6+63)/64,ymax)+4-cy);
 assert(r->canvas.rgba.size()==alpha.size()*4);for(unsigned i=0;i<alpha.size();++i){assert(r->canvas.rgba[4*i]==0&&r->canvas.rgba[4*i+1]==0&&r->canvas.rgba[4*i+2]==0&&r->canvas.rgba[4*i+3]==alpha[i]);}
 ++fixtures;++successes;return r;
}
static void error(Binding b,std::string_view s,tt::LineBox box,E e){auto r=tt::build_simple_ltr_run(b,s,box);assert(std::holds_alternative<E>(r));if(std::get<E>(r)!=e){std::cerr<<"wrong error "<<int(std::get<E>(r))<<" wanted "<<int(e)<<" bytes "<<s.size()<<"\n";std::abort();}++fixtures;}
struct Cancel:tt::Cancellation{unsigned at,calls=0;explicit Cancel(unsigned a):at(a){}bool cancelled()noexcept override{return ++calls==at;}};
int main(){
 auto b=bind();tt::LineBox box{48*64,64*64};
 // Independent fixed diagnostic oracle: fixture has190 ordered singleton ranges.
 // Lower-bound tree depths for A,space,A,NBSP,eacute are8+8+8+7+7.
 // These literals are not obtained from a production lookup or its counters.
 tt::RunWork diagnostic;auto diagnostic_run=tt::build_simple_ltr_run(b,"A A\xc2\xa0\xc3\xa9",box,nullptr,&diagnostic);
 assert(std::holds_alternative<Run>(diagnostic_run)&&diagnostic.lookups==5&&diagnostic.lookup_comparisons==38);++fixtures;
 auto stable=verify(b,"A\xc3\xa9 AV fi",box);const auto old=stable->canvas.rgba;
 std::string owned="A A";auto retained=verify(b,owned,box);owned="BBBB";assert(retained->source_text=="A A");
 for(std::string s:{""," ","  "," A","A "," A ","AAA","AV","fi","\xc2\xa0"," \xc2\xa0 ","caf\xc3\xa9","d\xc3\xa9j\xc3\xa0"})verify(b,s,box);
 for(int size:{1,31,32,33,63,64,65,127,128,129,1023,1024})for(int phase:{0,1,31,32,63})verify(bind(size)," AA \xc3\xa9 ",{48*64+phase,64*64});
 for(unsigned size:{31u,32u,33u})verify(bind(size,false,16),"AAA",box,16);
 for(unsigned adv:{15u,16u})verify(bind(1,false,adv,31),"AAA",box,adv,31);
 verify(bind(1024,false,600,1001),"A A",box,600,1001);
 verify(bind(1,false,65535),"AAA",box,65535);
 verify(bind(1024,false,0),"AAAA",box,0);
 for(auto line:{tt::LineBox{-1,64},tt::LineBox{65,64},tt::LineBox{0,0},tt::LineBox{0,-1},tt::LineBox{0,8192*64+1}})error(b,"",line,E::invalid_input);
 error(nullptr,"",box,E::invalid_input);
 for(std::string s:{std::string("\xc0\x80",2),std::string("\xc1\xbf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xff",1),std::string("\x80",1),std::string("\xe2\x82",2),std::string("\xc3",1),std::string("\xc3 ",2)})error(b,s,box,E::invalid_input);
 for(std::string s:std::vector<std::string>{"\n","\t","\x7f","\xc2\x80","\xc2\xad","\xc4\x80","\xf0\x9f\x98\x80",std::string(1,'\0')})error(b,s,box,E::unsupported);
 error(bind(1024,true),"AB",box,E::missing_glyph);
 error(b,std::string(4097,'A'),box,E::limit_exceeded);error(b,std::string(257,'A'),box,E::limit_exceeded);
 verify(b,std::string(256,' '),box);
 error(bind(8192),std::string(256,' '),{64,128},E::limit_exceeded);
 verify(bind(8192),std::string(255,' '),{64,128});
 error(bind(4096),std::string(256,' '),{0,4096*64},E::limit_exceeded);
 error(b,"",{0,8192*64},E::limit_exceeded); // padding makes canvas axis exceed8192
 // Exact16Mi source visits accepted; one additional overlapping1024-square exceeds
 // only source visits (canvas is~1Mi, retained masks17Mi, raster reserve<512M).
 auto largebytes=sfnt({{},glyph({{{0,0,true},{8192,0,true},{8192,8192,true},{0,8192,true}}})});
 for(unsigned i=0;i<r16(largebytes,4);++i)if(r32(largebytes,12+16*i)==tag("hmtx"))u16(largebytes,r32(largebytes,20+16*i)+4,0);
 add_table(largebytes,tag("cmap"),cmap({format12({{65,65,1}})},{{3,10,0}}));
 auto lb=std::get<Binding>(tt::RealFontBinding::create(source(largebytes),8192));
 tt::RunWork lw;auto lr=tt::build_simple_ltr_run(lb,std::string(16,'A'),{0,64},nullptr,&lw);
 assert(std::holds_alternative<Run>(lr)&&lw.source_pixel_visits==16ULL*1024*1024&&lw.canvas_complete);++fixtures;
 auto lx=tt::build_simple_ltr_run(lb,std::string(17,'A'),{0,64},nullptr,&lw);
 assert(std::holds_alternative<E>(lx)&&std::get<E>(lx)==E::limit_exceeded&&!lw.composition_started&&!lw.canvas_complete);++fixtures;
 unsigned cancels=0,late_cancel=0;for(unsigned at=1;at<10000;++at){Cancel c(at);tt::RunWork w;auto r=tt::build_simple_ltr_run(b,"A A",box,&c,&w);if(std::holds_alternative<Run>(r))break;assert(std::get<E>(r)==E::cancelled);assert(!w.canvas_complete);late_cancel+=w.conversion_pixels>0;assert(stable->canvas.rgba==old);++cancels;}assert(cancels>=5&&late_cancel>=2);
 unsigned oom=0;for(long n=0;n<1000;++n){fail_after=n;auto r=tt::build_simple_ltr_run(b,"A A",box);fail_after=-1;if(std::holds_alternative<Run>(r))break;assert(std::get<E>(r)==E::out_of_memory);assert(stable->canvas.rgba==old);++oom;}assert(oom>=5);
 // Mutate request bytes, not a production parser-derived expected sequence.
 unsigned valid=0,rejected=0,decoded=0,lookups=0,composing=0,complete=0;auto small=bind(64);std::uint32_t rng=0x8eca231;for(unsigned i=0;i<20000;++i){std::string s="A A";rng=rng*1664525u+1013904223u;s[rng%3]=char(rng>>24);bool good=true;try{for(auto [cp,p]:decode(s)){(void)p;if(!((cp>=32&&cp<=126)||(cp>=160&&cp<=255&&cp!=173)))good=false;}}catch(...){good=false;}tt::RunWork mw;auto r=tt::build_simple_ltr_run(small,s,{64,128},nullptr,&mw);decoded+=mw.decoded>0;lookups+=mw.lookups>0;composing+=mw.composition_started;complete+=mw.canvas_complete;if(good){assert(std::holds_alternative<Run>(r));++valid;}else{assert(std::holds_alternative<E>(r));++rejected;}assert(stable->canvas.rgba==old);}
 assert(composing==valid&&complete==valid&&lookups==valid&&decoded>=valid);
 assert(valid>5000&&rejected>5000&&fixtures>=50);std::cout<<"Q2b independent fixtures="<<fixtures<<" success="<<successes<<" mutations=20000 accepted="<<valid<<" rejected="<<rejected<<" decoded="<<decoded<<" lookups="<<lookups<<" composition="<<composing<<" complete="<<complete<<" cancellation="<<cancels<<" late_cancel="<<late_cancel<<" OOM="<<oom<<" HTML=EXCLUDED LSan=NOT_RUN\n";
}
