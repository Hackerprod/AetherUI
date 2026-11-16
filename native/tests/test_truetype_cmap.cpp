#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/truetype_cmap.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>
#include <tuple>
using namespace aether::rendering::text;
namespace tt=aether::rendering::text::truetype;
static_assert(!std::is_default_constructible_v<tt::UnicodeMap>);
static_assert(!std::is_copy_constructible_v<tt::UnicodeMap>);
static_assert(!std::is_move_constructible_v<tt::UnicodeMap>);
static_assert(!std::is_copy_assignable_v<tt::UnicodeMap>);
static_assert(!std::is_move_assignable_v<tt::UnicodeMap>);
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
static auto face(const Bytes& b){auto r=tt::ValidatedFace::create(source(b));assert(std::holds_alternative<std::shared_ptr<const tt::ValidatedFace>>(r));return std::get<std::shared_ptr<const tt::ValidatedFace>>(r);}
struct Group { unsigned first,last,gid; };
static Bytes format12(std::vector<Group> groups){
 Bytes b(16+12*groups.size());u16(b,0,12);u32(b,4,b.size());u32(b,12,groups.size());
 for(unsigned i=0;i<groups.size();++i){u32(b,16+12*i,groups[i].first);u32(b,20+12*i,groups[i].last);u32(b,24+12*i,groups[i].gid);}return b;
}
struct Seg {unsigned first,last,delta;std::vector<unsigned> gids;};
static Bytes format4(std::vector<Seg> segments){
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
static Bytes font(const Bytes& c,unsigned count=16){auto b=sfnt(std::vector<Bytes>(count));add_table(b,tag("cmap"),c);return b;}
using Map=std::shared_ptr<const tt::UnicodeMap>;
static Map map(const Bytes& c,unsigned count=16){auto r=tt::UnicodeMap::create(face(font(c,count)));if(!std::holds_alternative<Map>(r)){std::cerr<<"unexpected cmap error "<<int(std::get<tt::CmapError>(r))<<'\n';std::abort();}return std::get<Map>(r);}
static unsigned errors=0,lookups=0;
static void error(const Bytes& c,tt::CmapError e,unsigned count=16){auto r=tt::UnicodeMap::create(face(font(c,count)));if(!std::holds_alternative<tt::CmapError>(r)||std::get<tt::CmapError>(r)!=e){std::cerr<<"error case "<<errors<<" expected "<<int(e)<<" got "<<(std::holds_alternative<tt::CmapError>(r)?int(std::get<tt::CmapError>(r)):-1)<<'\n';std::abort();}++errors;}
static void mapped(const Map& m,unsigned cp,unsigned gid){tt::CmapWork w;auto r=m->lookup(cp,&w);assert(std::holds_alternative<tt::Mapped>(r)&&std::get<tt::Mapped>(r).glyph_id==gid&&gid!=0&&w.comparisons<=17);++lookups;}
static void missing(const Map& m,unsigned cp){tt::CmapWork w;assert(std::holds_alternative<tt::Missing>(m->lookup(cp,&w)));assert(w.comparisons<=17);++lookups;}
struct Cancel:tt::Cancellation{unsigned calls=0,at;explicit Cancel(unsigned a):at(a){}bool cancelled()noexcept override{return ++calls==at;}};
int main(){
 using E=tt::CmapError;
 const auto a=format12({{0,0,1},{32,34,0},{65,67,4},{0xd800,0xd801,7},{0x1f600,0x1f601,9},{0x10ffff,0x10ffff,12}});
 const auto c=cmap({a},{{3,10,0}});auto m=map(c);assert(m->format()==12&&m->face()->source()==m->source()&&m->digest()==m->source()->digest);
 mapped(m,0,1);missing(m,31);missing(m,32);mapped(m,33,1);mapped(m,34,2);missing(m,35);mapped(m,65,4);mapped(m,67,6);missing(m,68);mapped(m,0x1f600,9);mapped(m,0x1f601,10);mapped(m,0x10ffff,12);
 for(unsigned cp:{0xd800u,0xdbffu,0xdc00u,0xdfffu,0x110000u,0xffffffffu})assert(std::get<E>(m->lookup(cp))==E::invalid_input);
 const auto four=format4({{0,0,1,{}},{65,67,65535,{0,1,3}},{100,101,65436,{}},{0xffff,0xffff,1,{}}});
 auto m4=map(cmap({four},{{3,1,0}}));assert(m4->format()==4);mapped(m4,0,1);missing(m4,65);missing(m4,66);mapped(m4,67,2);missing(m4,100);mapped(m4,101,1);missing(m4,0xffff);missing(m4,0x10000);
 // Fixed selection priority independent of record order and exact aliases.
 const auto f4=format4({{65,65,65535,{}},{0xffff,0xffff,1,{}}});
 const auto f12=format12({{65,65,3}});
 std::vector<Rec> rs{{0,3,0},{3,1,0},{0,4,1},{3,10,2},{3,10,2}};
 const auto f12top=format12({{65,65,5}});
 auto rec_less=[](auto x,auto y){return std::tie(x.platform,x.encoding,x.table)<std::tie(y.platform,y.encoding,y.table);};
 std::sort(rs.begin(),rs.end(),rec_less);
 do {mapped(map(cmap({f4,f12,f12top},rs),128),65,5);}while(std::next_permutation(rs.begin(),rs.end(),rec_less));
 mapped(map(cmap({f4,f12},{{3,1,0},{0,4,1}}),128),65,3);
 mapped(map(cmap({f4},{{0,3,0},{3,1,0}}),128),65,64);
 error(cmap({f12,f12},{{3,10,0},{3,10,1}}),E::unsupported);
 error(cmap({four,f12},{{3,10,0},{0,4,1}}),E::unsupported);
 error(cmap({f12},{{1,0,0}}),E::unsupported);
 assert(std::get<E>(tt::UnicodeMap::create(face(sfnt({{}}))))==E::unsupported);
 assert(std::get<E>(tt::UnicodeMap::create(nullptr))==E::invalid_input);
 auto bad=a;u32(bad,24,16);error(cmap({bad,f12},{{3,10,0},{0,4,1}}),E::malformed); // No fallback.
 bad=c;u16(bad,0,1);error(bad,E::malformed);
 bad=a;u32(bad,8,1);error(cmap({bad},{{3,10,0}}),E::malformed);
 bad=a;u16(bad,2,1);error(cmap({bad},{{3,10,0}}),E::malformed);
 bad=a;u32(bad,4,a.size()-1);error(cmap({bad},{{3,10,0}}),E::malformed);
 bad=a;u32(bad,20,0x110000);error(cmap({bad},{{3,10,0}}),E::malformed);
 bad=a;u32(bad,16,1);error(cmap({bad},{{3,10,0}}),E::malformed);
 error(cmap({format12({{3,4,1},{4,5,2}})},{{3,10,0}}),E::malformed);
 error(cmap({format12({{3,4,15}})},{{3,10,0}}),E::malformed);
 error(cmap({format12({{3,4,0xffffffff}})},{{3,10,0}}),E::malformed);
 error(cmap({format12({{0xd800,0xd800,16}})},{{3,10,0}}),E::malformed);
 bad=c;u32(bad,8,4);error(bad,E::malformed);bad=c;u32(bad,8,0xffffffff);error(bad,E::malformed);
 // Valid format at an odd offset is unsupported under Q1c's conservative profile.
 bad=c;bad.insert(bad.begin()+12,0);u32(bad,8,13);error(bad,E::unsupported);
 bad=four;u16(bad,4,1);error(cmap({bad},{{3,1,0}}),E::malformed);
 bad=four;u16(bad,14+2*4,1);error(cmap({bad},{{3,1,0}}),E::malformed);
 bad=four;u16(bad,6,7);error(cmap({bad},{{3,1,0}}),E::malformed);
 bad=four;u16(bad,20,0xfffe);error(cmap({bad},{{3,1,0}}),E::malformed);
 bad=four;u16(bad,16+6*4+2,1);error(cmap({bad},{{3,1,0}}),E::unsupported);
 bad=four;u16(bad,16+6*4,2);error(cmap({bad},{{3,1,0}}),E::malformed); // Metadata, not glyph array.
 bad=four;u16(bad,16+6*4+2,65534);error(cmap({bad},{{3,1,0}}),E::malformed);
 error(cmap({format4({{0xd800,0xd800,0, {16}},{0xffff,0xffff,1,{}}})},{{3,1,0}}),E::malformed);
 // Every strict truncation of a selected table is rejected, never published.
 for(std::size_t n=0;n<a.size();++n)error(cmap({Bytes(a.begin(),a.begin()+n)},{{3,10,0}}),E::malformed);
 for(std::size_t n=0;n<four.size();++n)error(cmap({Bytes(four.begin(),four.begin()+n)},{{3,1,0}}),E::malformed);
 // Declared eligible subtable overlap is a conservative unsupported profile.
 bad=cmap({f12,f12},{{3,10,0},{0,4,1}});u32(bad,r32(bad,8)+4,2*f12.size());error(bad,E::unsupported);
 std::vector<Rec> aliases(128,{3,10,0});mapped(map(cmap({f12},aliases)),65,3);
 error(cmap({format4({{65,70,0,{}},{70,71,0,{}},{0xffff,0xffff,1,{}}})},{{3,1,0}}),E::malformed,128);
 error(cmap({format4({{71,70,0,{}},{0xffff,0xffff,1,{}}})},{{3,1,0}}),E::malformed,128);
 // A large valid format4 fitting uint16 length exercises bounded search/cancel.
 std::vector<Seg> segments;for(unsigned cp=0;cp<8188;++cp)segments.push_back({cp,cp, (65537-cp)%65536,{}});segments.push_back({65535,65535,1,{}});
 auto large4face=face(font(cmap({format4(segments)},{{3,1,0}})));auto large4=std::get<Map>(tt::UnicodeMap::create(large4face));
 for(unsigned cp=0;cp<8188;++cp)mapped(large4,cp,1);
 Cancel count4(0xffffffff);assert(std::holds_alternative<Map>(tt::UnicodeMap::create(large4face,&count4)));assert(count4.calls>8188/64);
 for(unsigned i:{1u,count4.calls/2,count4.calls}){Cancel cc(i);assert(std::get<E>(tt::UnicodeMap::create(large4face,&cc))==E::cancelled);}
 // Limits precede retaining untrusted arrays.
 bad=c;u16(bad,2,129);error(bad,E::limit_exceeded);
 bad=c;bad.resize(tt::max_cmap_bytes+1);error(bad,E::limit_exceeded);
 bad=a;u32(bad,12,65537);error(cmap({bad},{{3,10,0}}),E::limit_exceeded);
 bad=four;u16(bad,6,2*8193);error(cmap({bad},{{3,1,0}}),E::limit_exceeded);
 std::vector<Group> groups;for(unsigned i=0;i<65536;++i)groups.push_back({i*2,i*2,i%15});auto bigface=face(font(cmap({format12(groups)},{{3,10,0}})));
 tt::CmapWork w;struct Cadence:tt::Cancellation {tt::CmapWork& w;std::size_t prev=0;explicit Cadence(tt::CmapWork& q):w(q){}bool cancelled()noexcept override {assert(w.units-prev<=64);prev=w.units;return false;}} cadence(w);
 auto big=tt::UnicodeMap::create(bigface,&cadence,&w);assert(std::holds_alternative<Map>(big));assert(w.units<=tt::max_cmap_work&&w.units-cadence.prev<=64);
 auto bm=std::get<Map>(big);for(unsigned i=0;i<65536;++i){auto cp=i*2;if(cp>=0xd800&&cp<=0xdfff)continue;if(i%15)mapped(bm,cp,i%15);else missing(bm,cp);missing(bm,cp+1);}
 // Cancel at every callback of a moderate fixture, including final publication check.
 std::vector<Group> moderate(groups.begin(),groups.begin()+200);auto mf=face(font(cmap({format12(moderate)},{{3,10,0}})));
 Cancel count(0xffffffff);auto ok=tt::UnicodeMap::create(mf,&count);assert(std::holds_alternative<Map>(ok));assert(count.calls>3);
 for(unsigned i=1;i<=count.calls;++i){Cancel cancel(i);assert(std::get<E>(tt::UnicodeMap::create(mf,&cancel))==E::cancelled);}
 unsigned oom=0;bool success=false;for(int i=0;i<100;++i){fail_after=i;auto r=tt::UnicodeMap::create(mf);fail_after=-1;if(std::holds_alternative<Map>(r)){success=true;break;}assert(std::get<E>(r)==E::out_of_memory);++oom;}assert(success&&oom>0);
 mapped(m,65,4);mapped(m4,67,2);assert(m->source()->bytes==font(c));
 std::weak_ptr<const tt::ValidatedFace> weak;Map survivor;{auto f=face(font(c));weak=f;survivor=std::get<Map>(tt::UnicodeMap::create(f));}assert(!weak.expired());mapped(survivor,65,4);survivor.reset();assert(weak.expired());
 std::cout<<"Q1c synthetic errors="<<errors<<" lookups="<<lookups<<" cancellation_points="<<count.calls<<" OOM="<<oom<<" PASS\n";
}
