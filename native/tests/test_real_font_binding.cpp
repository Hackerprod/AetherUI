#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/real_font_binding.h"
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
// Serialization helpers reused from Q1c; binding assertions and corpus below independent.
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
static Bytes font(const Bytes& c,unsigned count=16){auto b=sfnt(std::vector<Bytes>(count));add_table(b,tag("cmap"),c);return b;}

using Binding=std::shared_ptr<const tt::RealFontBinding>;
using E=tt::BindingError;
static_assert(!std::is_default_constructible_v<tt::RealFontBinding>);
static_assert(!std::is_copy_constructible_v<tt::RealFontBinding>);
static_assert(!std::is_move_constructible_v<tt::RealFontBinding>);
static unsigned fixtures=0;
static Binding success(std::shared_ptr<const FontSource> s,int size=1024){tt::BindingWork w;auto r=tt::RealFontBinding::create(s,size,1,nullptr,&w);assert(std::holds_alternative<Binding>(r));auto b=std::get<Binding>(r);assert(b->source()==s&&b->face()->source()==s&&b->map()->source()==s&&b->font()->source==s);assert(b->map()->face()==b->face());assert(b->digest()==s->digest&&b->font()->source_hash==s->digest);assert(b->font()->size_26_6==size&&b->font()->face_index==0);assert(w.units==w.face.units+w.cmap.units&&w.binding_created);++fixtures;return b;}
static void error(std::shared_ptr<const FontSource> s,E e,int size=1024,unsigned flags=1){auto r=tt::RealFontBinding::create(s,size,flags);assert(std::holds_alternative<E>(r)&&std::get<E>(r)==e);++fixtures;}
struct Cancel:tt::Cancellation {unsigned at,calls=0;explicit Cancel(unsigned a):at(a){}bool cancelled()noexcept override{return ++calls==at;}};
int main(int argc,char** argv){
 const auto cm=cmap({format12({{65,65,1},{233,233,2}})},{{3,10,0}});const auto bytes=font(cm);auto s=source(bytes);auto original=success(s);auto stable=original;
 error(nullptr,E::invalid_input);
 for(int n:{-2147483647,-1,0,8193,2147483647})error(s,E::invalid_input,n);
 for(unsigned f:{0u,2u,3u,0xffffffffu})error(s,E::unsupported,1024,f);
 for(int n:{1,2,63,64,65,1024,8191,8192})success(s,n);
 assert(std::get<tt::Mapped>(original->map()->lookup(65)).glyph_id==1);assert(std::get<tt::Mapped>(original->map()->lookup(233)).glyph_id==2);assert(std::holds_alternative<tt::Missing>(original->map()->lookup(66)));
 for(unsigned cp:{0xd800u,0xdfffu,0x110000u}){assert(std::get<tt::CmapError>(original->map()->lookup(cp))==tt::CmapError::invalid_input);++fixtures;}
 auto same=success(source(bytes));assert(same->source()!=s&&same->digest()==original->digest());++fixtures;
 auto otherbytes=bytes;otherbytes.back()^=1;auto other=success(source(otherbytes));assert(other->digest()!=original->digest());++fixtures;
 auto bigger=success(s,2048);assert(bigger->font()!=original->font()&&bigger->source()==original->source());++fixtures;
 std::weak_ptr<const FontSource> weak;{auto owned=source(bytes);weak=owned;auto retained=success(owned);owned.reset();assert(!weak.expired());std::thread t([b=std::move(retained)]{assert(std::get<tt::Mapped>(b->map()->lookup(65)).glyph_id==1);});t.join();}assert(weak.expired());++fixtures;
 error(source(Bytes(3)),E::malformed);auto bad=bytes;u32(bad,0,0x4f54544f);error(source(bad),E::unsupported);
 error(source(sfnt({{}})),E::unsupported);auto badcm=cm;u16(badcm,0,1);error(source(font(badcm)),E::malformed);
 error(source(font(cmap({format12({{65,65,99}})},{{3,10,0}}))),E::malformed);
 error(source(font(cmap({format12({{65,65,1}})},{{1,0,0}}))),E::unsupported);
 badcm=cm;u16(badcm,2,129);error(source(font(badcm)),E::limit_exceeded);
 unsigned cancelled=0,final_cancel=0;for(unsigned at=1;at<200;++at){Cancel c(at);tt::BindingWork w;auto r=tt::RealFontBinding::create(s,1024,1,&c,&w);if(std::holds_alternative<Binding>(r)){assert(c.calls<at);break;}assert(std::get<E>(r)==E::cancelled);final_cancel+=w.binding_created;assert(stable==original);++cancelled;}assert(cancelled>=5&&final_cancel==1);
 unsigned oom=0;for(long n=0;n<200;++n){fail_after=n;auto r=tt::RealFontBinding::create(s,1024);fail_after=-1;if(std::holds_alternative<Binding>(r))break;assert(std::get<E>(r)==E::out_of_memory);assert(stable==original&&std::get<tt::Mapped>(stable->map()->lookup(65)).glyph_id==1);++oom;}assert(oom>=4);
 unsigned facephase=0,cmaphase=0,instancephase=0,published=0;std::uint32_t rng=0xa3e71;
 for(unsigned i=0;i<20000;++i){auto m=bytes;rng=rng*1664525u+1013904223u;std::size_t pos;
 // Alternating directory, cmap, and inert/checksum mutations ensure real phase reachability.
 if(i%3==0)pos=rng%m.size();else if(i%3==1)pos=m.size()-cm.size()+(rng%cm.size());else pos=16+(rng%4);
 m[pos]^=1u<<((rng>>24)%8);tt::BindingWork w;auto r=tt::RealFontBinding::create(source(std::move(m)),1024,1,nullptr,&w);facephase+=w.face_started;cmaphase+=w.cmap_started;instancephase+=w.instance_started;published+=w.binding_created;assert(w.units==w.face.units+w.cmap.units);assert(w.binding_created==std::holds_alternative<Binding>(r));}
 assert(facephase==20000&&cmaphase>5000&&instancephase>5000&&published>5000);
 unsigned raster_equal=0;
 if(argc==2){std::ifstream in(argv[1],std::ios::binary);assert(in);Bytes local((std::istreambuf_iterator<char>(in)),{});auto ls=source(std::move(local));auto binding=success(ls);auto manualface=std::get<std::shared_ptr<const tt::ValidatedFace>>(tt::ValidatedFace::create(ls));auto manualmap=std::get<std::shared_ptr<const tt::UnicodeMap>>(tt::UnicodeMap::create(manualface));auto fi=std::make_shared<const FontInstance>(ls,0,1024,1);
 for(unsigned cp:{65u,233u}){auto gid=std::get<tt::Mapped>(binding->map()->lookup(cp)).glyph_id;assert(gid==std::get<tt::Mapped>(manualmap->lookup(cp)).glyph_id);auto o1=std::get<std::shared_ptr<const tt::Outline>>(binding->face()->outline(gid));auto o2=std::get<std::shared_ptr<const tt::Outline>>(manualface->outline(gid));auto a=std::get<tt::RasterOutput>(tt::rasterize(o1,binding->font(),0,0));auto b=std::get<tt::RasterOutput>(tt::rasterize(o2,fi,0,0));assert(a.mask->bytes==b.mask->bytes&&a.mask->width==b.mask->width&&a.mask->height==b.mask->height&&a.mask->bearing_left==b.mask->bearing_left&&a.mask->bearing_top==b.mask->bearing_top&&a.advance_26_6==b.advance_26_6);++raster_equal;}}
 assert(fixtures>=30);std::cout<<"Q2a fixtures="<<fixtures<<" mutations=20000 face_started="<<facephase<<" cmap_started="<<cmaphase<<" instance_started="<<instancephase<<" binding_created="<<published<<" cancel_positions="<<cancelled<<" oom_positions="<<oom<<" local_raster_equal="<<raster_equal<<"\n";
}
