#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/real_html_text.h"
#include "html_text_real_cases.h"
#include "../src/rendering/text/real_html_admission.h"
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
static long fail_after=-1;static std::size_t allocation_count=0;
void* operator new(std::size_t n){++allocation_count;if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
// Complete the scalar nothrow allocation family: core legally uses new(nothrow)
// followed by ordinary delete. Keep ASan mismatch detection fully enabled.
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(const std::bad_alloc&){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {::operator delete(p);}
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
static unsigned fixtures=0;
static Bytes testfont(bool missing=false,unsigned advance=600,unsigned units=1024){
 auto b=sfnt({{},glyph({{{-80,-120,true},{800,-120,true},{800,600,true},{-80,600,true}}}),{}},false,0,{0,-80,0});
 for(unsigned i=0;i<r16(b,4);++i)if(r32(b,12+16*i)==tag("head"))u16(b,r32(b,20+16*i)+18,units);
 // Deliberately overlap the rectangle glyph at small advances; whitespace is empty.
 for(unsigned i=0;i<r16(b,4);++i)if(r32(b,12+16*i)==tag("hmtx")){auto off=r32(b,20+16*i);u16(b,off+4,advance);u16(b,off+8,256);}
 std::vector<Group> groups;for(unsigned cp=32;cp<=255;++cp)if((cp<=126||cp>=160)&&cp!=173&&!(missing&&cp==66))groups.push_back({cp,cp,(cp==32||cp==160)?2u:1u});
 add_table(b,tag("cmap"),cmap({format12(groups)},{{3,10,0}}));return b;
}
static Binding bind(unsigned size=1024,bool missing=false,unsigned adv=600,unsigned units=1024){auto r=tt::RealFontBinding::create(source(testfont(missing,adv,units)),size);assert(std::holds_alternative<Binding>(r));return std::get<Binding>(r);}
using HF=std::shared_ptr<const tt::HtmlTextFrame>;
static HF ok(const std::string& html,Binding b){auto r=tt::render_real_html_text(html,b,{48*64,64*64},256,128);if(!std::holds_alternative<HF>(r)){std::cerr<<"expected success got error "<<int(std::get<tt::HtmlTextError>(r))<<"\n";std::abort();}return std::get<HF>(r);}
struct Cancel:tt::Cancellation{unsigned at,calls=0;explicit Cancel(unsigned a):at(a){}bool cancelled()noexcept override{return ++calls==at;}};
int main(){
 auto b=bind();const auto cases=q2c_cases::corpus();assert(cases.size()>=60);
 for(const auto& c:cases){auto result=tt::render_real_html_text(c.html,b,{48*64,64*64},256,128);bool accepted=std::holds_alternative<HF>(result);if(accepted!=c.accepted){std::cerr<<"FAIL raw fixture "<<c.name<<" accepted="<<accepted<<" error="<<(accepted?-1:int(std::get<tt::HtmlTextError>(result)))<<"\n";return 1;}if(accepted){auto f=std::get<HF>(result);assert(f&&f->run&&f->run->source_text==c.decoded);assert(f->run->binding==b);assert(f->work.image_commands==0);assert(c.decoded.empty()?f->work.native_glyph_commands==0:f->work.native_glyph_commands>0);assert(f->frame.viewport.width==256&&f->frame.viewport.height==128&&f->frame.pixels.size()==f->frame.stride*128);assert(f->work.run_canvas_bytes==f->run->canvas.rgba.size());assert(f->work.font_bytes>0);assert(f->work.software.status==aether::rendering::software::Status::ok);}++fixtures;}
 auto html=q2c_cases::document(q2c_cases::owner("AAAAAAAA"));auto stable=ok(html,b);const auto pixels=stable->frame.pixels;
 // Fixture advance600 at size1024 gives600/64px, hence phases cycle8;
 // choose1024units,advance1024 to force all8 identical phase0 keys.
 auto integral=bind(1024,false,1024);auto repeated=ok(html,integral);
 assert(repeated->run->glyphs.size()==8);assert(repeated->work.registry_entries==1);assert(repeated->work.software.mask_count==1);assert(repeated->work.software.glyph_uses==8);++fixtures;
 auto zero_advance=ok(q2c_cases::document(q2c_cases::owner("AAAA")),bind(1024,false,0));
 assert(zero_advance->run->width_26_6==0&&zero_advance->work.registry_entries==1&&zero_advance->work.native_glyph_commands==1&&zero_advance->work.software.glyph_uses==4);
 assert(!zero_advance->run->glyphs[0].mask->bytes.empty());++fixtures;
 auto pieces=ok(q2c_cases::document(q2c_cases::owner("A<!--zero-->&#233;")),b);
 assert(pieces->run->source_text=="A\xc3\xa9"&&pieces->run->glyphs.size()==2&&pieces->run->width_26_6==1200);/* Reviewer clarified original v2: comments register empty 0-byte pieces, preserving child ordinal. */assert(pieces->source_map.size()==3);assert(pieces->source_map[0].ordinal==0&&pieces->source_map[0].begin==0&&pieces->source_map[0].end==1);assert(pieces->source_map[1].ordinal==1&&pieces->source_map[1].begin==1&&pieces->source_map[1].end==1);assert(pieces->source_map[2].ordinal==2&&pieces->source_map[2].begin==1&&pieces->source_map[2].end==3);++fixtures;
 auto empty=ok(q2c_cases::document(q2c_cases::owner("<!--empty-->")),b);assert(empty->run->glyphs.empty());assert(empty->work.native_glyph_commands==0&&empty->work.software.glyph_uses==0);bool found=false;for(const auto& box:empty->boxes)if(box.text){assert(box.content.width==0&&box.content.height==64);found=true;}assert(found);++fixtures;
 // Geometry oracle derives only from authored integer CSS: border(3,2)+padding(5,7).
 auto geometry_css=q2c_cases::style();
 for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"padding-left:0px","padding-left:5px"},{"padding-top:0px","padding-top:7px"},{"border-left-width:0px","border-left-width:3px"},{"border-top-width:0px","border-top-width:2px"}}){auto p=geometry_css.find(pair.first);assert(p!=std::string::npos);geometry_css.replace(p,pair.first.size(),pair.second);}
 auto geometry=ok(q2c_cases::document(q2c_cases::owner("A",geometry_css)),b);bool text_geometry=false;
 for(const auto& box:geometry->boxes)if(box.text){assert(box.content.x==8&&box.content.y==9&&box.content.width==600.0f/64&&box.content.height==64);text_geometry=true;}
 assert(text_geometry);++fixtures;
 for(auto dims:std::vector<std::pair<unsigned,unsigned>>{{0,128},{256,0},{2049,128},{256,2049}}){auto r=tt::render_real_html_text(html,b,{3072,4096},dims.first,dims.second);assert(std::holds_alternative<tt::HtmlTextError>(r));++fixtures;}
 assert(std::holds_alternative<tt::HtmlTextError>(tt::render_real_html_text(html,nullptr,{3072,4096},256,128)));++fixtures;
 for(const auto line:std::vector<tt::LineBox>{{-1,4096},{4097,4096},{3072,524289},{0,0}}){auto r=tt::render_real_html_text(html,b,line,256,128);assert(std::holds_alternative<tt::HtmlTextError>(r));++fixtures;}
 auto bad_size=tt::admit_real_html(html,8193,{3072,4096});assert(std::holds_alternative<tt::HtmlAdmissionError>(bad_size));++fixtures;
 Cancel probe(~0u);assert(std::holds_alternative<HF>(tt::render_real_html_text(html,b,{3072,4096},256,128,&probe)));assert(probe.calls<20000);
 unsigned cancelled=0;for(unsigned i=1;i<=probe.calls;++i){Cancel c(i);auto r=tt::render_real_html_text(html,b,{3072,4096},256,128,&c);if(std::holds_alternative<tt::HtmlTextError>(r)){assert(std::get<tt::HtmlTextError>(r)==tt::HtmlTextError::cancelled);++cancelled;}assert(stable->frame.pixels==pixels);}assert(cancelled>0);
 allocation_count=0;auto oom_probe=tt::render_real_html_text(html,b,{3072,4096},256,128);const auto allocations=allocation_count;assert(std::holds_alternative<HF>(oom_probe));
 std::vector<std::size_t> sites;for(std::size_t i=0;i<allocations;++i)sites.push_back(i);
 unsigned ooms=0;for(auto i:sites){fail_after=i;auto r=tt::render_real_html_text(html,b,{3072,4096},256,128);fail_after=-1;if(std::holds_alternative<tt::HtmlTextError>(r)){if(std::get<tt::HtmlTextError>(r)!=tt::HtmlTextError::out_of_memory){std::cerr<<"OOM site="<<i<<"/"<<allocations<<" wrong typed error="<<int(std::get<tt::HtmlTextError>(r))<<"\n";return 1;}++ooms;}assert(stable->frame.pixels==pixels);}assert(ooms>0);
 std::cout<<"Q2c independent fixtures="<<fixtures<<" cancelled="<<cancelled<<" allocation_failures="<<ooms<<" tested_sites="<<sites.size()<<" total_sites="<<allocations<<"\n";
}
