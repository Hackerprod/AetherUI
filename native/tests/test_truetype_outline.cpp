#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/truetype_outline.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>
using namespace aether::rendering::text;
namespace tt=aether::rendering::text::truetype;
static_assert(!std::is_default_constructible_v<tt::Outline>);
static_assert(!std::is_copy_constructible_v<tt::Outline>);
static_assert(!std::is_move_constructible_v<tt::Outline>);
static_assert(!std::is_copy_assignable_v<tt::Outline>);
static_assert(!std::is_move_assignable_v<tt::Outline>);
using Bytes=std::vector<std::uint8_t>;
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static void u16(Bytes& b,std::size_t p,unsigned v){assert(p+2<=b.size());b[p]=v>>8;b[p+1]=v;}
static void u32(Bytes& b,std::size_t p,std::uint32_t v){assert(p+4<=b.size());u16(b,p,v>>16);u16(b,p+2,v);}
static unsigned r16(const Bytes& b,std::size_t p){return (unsigned(b[p])<<8)|b[p+1];}
static std::uint32_t r32(const Bytes& b,std::size_t p){return (std::uint32_t(r16(b,p))<<16)|r16(b,p+2);}
static void append16(Bytes& b,int v){auto p=b.size();b.resize(p+2);u16(b,p,unsigned(v));}
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
static std::size_t entry(const Bytes& b,const char* name){for(unsigned i=0;i<r16(b,4);++i)if(r32(b,12+16*i)==tag(name))return 12+16*i;std::abort();}
static std::size_t offset(const Bytes& b,const char* name){return r32(b,entry(b,name)+8);}
static void add_table(Bytes& b,std::uint32_t name,Bytes payload={0,0,0,0}){
 const unsigned n=r16(b,4);const unsigned end=12+16*n;
 b.insert(b.begin()+end,16,0);u16(b,4,n+1);
 for(unsigned i=0;i<n;++i)u32(b,20+16*i,r32(b,20+16*i)+16);
 while(b.size()%4){b.push_back(0);}
 u32(b,end,name);u32(b,end+8,b.size());u32(b,end+12,payload.size());b.insert(b.end(),payload.begin(),payload.end());
}
static auto source(Bytes b){return std::make_shared<const FontSource>(std::move(b),"synthetic Q1a test fixture");}
static auto face(const Bytes& b){auto r=tt::ValidatedFace::create(source(b));assert(std::holds_alternative<std::shared_ptr<const tt::ValidatedFace>>(r));return std::get<std::shared_ptr<const tt::ValidatedFace>>(r);}
static auto outline(const std::shared_ptr<const tt::ValidatedFace>& f,unsigned id){auto r=f->outline(id);assert(std::holds_alternative<std::shared_ptr<const tt::Outline>>(r));return std::get<std::shared_ptr<const tt::Outline>>(r);}
static unsigned errors=0;
static void face_error(Bytes b,tt::Error e){auto r=tt::ValidatedFace::create(source(std::move(b)));assert(std::holds_alternative<tt::Error>(r)&&std::get<tt::Error>(r)==e);++errors;}
static void glyph_error(Bytes g,tt::Error e){auto f=face(sfnt({g}));auto r=f->outline(0);assert(std::holds_alternative<tt::Error>(r)&&std::get<tt::Error>(r)==e);++errors;}
static void segment(const tt::Segment& s,tt::SegmentKind k,tt::Point from,tt::Point to,tt::Point control={}){assert(s.kind==k&&s.from==from&&s.to==to);if(k==tt::SegmentKind::quadratic)assert(s.control==control);}
struct Cancel:tt::Cancellation{unsigned calls=0,at;explicit Cancel(unsigned a):at(a){}bool cancelled()noexcept override{return ++calls==at;}};
int main(){
 const auto triangle=glyph({{{10,20,true},{110,20,true},{10,120,true}}});
 Bytes composite(10);u16(composite,0,0xffff);
 const auto bytes=sfnt({{},triangle,triangle,composite},false,2,{0,-7,13,0});
 auto src=source(bytes);auto created=tt::ValidatedFace::create(src);assert(std::holds_alternative<std::shared_ptr<const tt::ValidatedFace>>(created));auto f=std::get<std::shared_ptr<const tt::ValidatedFace>>(created);
 assert(f->source()==src&&f->digest()==src->digest&&f->face_index()==0&&f->glyph_count()==4&&f->units_per_em()==1024);
 auto a=outline(f,1);assert(a->glyph_id==1&&a->metrics.advance==600&&a->metrics.lsb==-7&&a->metrics.pp1==17);assert(a->bounds.x_min==10&&a->bounds.y_min==20&&a->bounds.x_max==110&&a->bounds.y_max==120);assert(a->contours.size()==1&&a->contours[0].first_segment==0&&a->contours[0].segment_count==3&&a->segments.size()==3);
 segment(a->segments[0],tt::SegmentKind::line,{10,20},{110,20});segment(a->segments[1],tt::SegmentKind::line,{110,20},{10,120});segment(a->segments[2],tt::SegmentKind::line,{10,120},{10,20});
 auto trailing=outline(f,2);assert(trailing->metrics.advance==600&&trailing->metrics.lsb==13&&trailing->metrics.pp1==-3);
 auto empty=outline(f,0);assert(empty->segments.empty()&&empty->contours.empty()&&empty->metrics.advance==500&&empty->metrics.pp1==0);
 // Q1d-T validates component payload: this header-only composite is truncated,
 // hence malformed rather than the previous blanket unsupported classification.
 assert(std::get<tt::Error>(f->outline(3))==tt::Error::malformed);assert(std::get<tt::Error>(f->outline(4))==tt::Error::malformed);assert(std::get<tt::Error>(tt::ValidatedFace::create(src,1))==tt::Error::unsupported);
 auto short_face=face(sfnt({triangle},true));assert(outline(short_face,0)->segments.size()==3);
 // Product owns immutable provenance even after its face and caller source die.
 std::shared_ptr<const tt::Outline> survivor;std::weak_ptr<const FontSource> weak_source;std::string survivor_digest;
 {auto local_source=source(sfnt({triangle}));weak_source=local_source;survivor_digest=local_source->digest;auto local_face=std::get<std::shared_ptr<const tt::ValidatedFace>>(tt::ValidatedFace::create(local_source));survivor=outline(local_face,0);assert(survivor->source()==local_source);}
 assert(!weak_source.expired()&&survivor->digest()==survivor_digest&&survivor->units_per_em()==1024&&survivor->face_index()==0&&survivor->glyph_id==0&&survivor->segments.size()==3);
 auto different_bytes=sfnt({triangle});u16(different_bytes,offset(different_bytes,"head")+18,2048);auto different=outline(face(different_bytes),0);assert(different->glyph_id==survivor->glyph_id&&different->digest()!=survivor->digest()&&different->units_per_em()==2048);survivor.reset();assert(weak_source.expired());
 auto singleton_on=outline(face(sfnt({glyph({{{4,7,true}}})})),0);assert(singleton_on->segments.empty()&&singleton_on->contours.size()==1&&singleton_on->contours[0].segment_count==0);
 auto singleton_off=outline(face(sfnt({glyph({{{4,7,false}}})})),0);assert(singleton_off->segments.size()==1&&singleton_off->contours.size()==1&&singleton_off->contours[0].segment_count==1);segment(singleton_off->segments[0],tt::SegmentKind::quadratic,{4,7},{4,7},{4,7});
 // First and last off-curve: start at their midpoint; interior off-curve pair also splits.
 auto curves=outline(face(sfnt({glyph({{{0,0,false},{100,0,false},{100,100,true},{0,100,false}}})})),0);
 assert(curves->segments.size()==3);segment(curves->segments[0],tt::SegmentKind::quadratic,{0,50},{50,0},{0,0});segment(curves->segments[1],tt::SegmentKind::quadratic,{50,0},{100,100},{100,0});segment(curves->segments[2],tt::SegmentKind::quadratic,{100,100},{0,50},{0,100});
 // First off-curve and last on-curve starts at last, not the first control.
 auto first_off=outline(face(sfnt({glyph({{{0,0,false},{100,0,true},{100,100,true}}})})),0);assert(first_off->segments.size()==2);segment(first_off->segments[0],tt::SegmentKind::quadratic,{100,100},{100,0},{0,0});segment(first_off->segments[1],tt::SegmentKind::line,{100,0},{100,100});
 auto multiple=outline(face(sfnt({glyph({{{0,0,true},{50,0,true},{0,50,true}},{{100,100,true},{150,100,true},{100,150,true}}})})),0);assert(multiple->contours.size()==2&&multiple->contours[1].first_segment==3&&multiple->segments.size()==6);
 // Empty contour header and optional instruction bytes do not manufacture geometry.
 Bytes zero(10);assert(outline(face(sfnt({zero})),0)->segments.empty());zero.resize(14);u16(zero,10,2);assert(outline(face(sfnt({zero})),0)->segments.empty());u16(zero,10,3);glyph_error(zero,tt::Error::malformed);
 auto empty_lsb=face(sfnt({{}},false,1,{1}));assert(std::get<tt::Error>(empty_lsb->outline(0))==tt::Error::unsupported);
 // Safe unknown tables ignored; duplicate tags and overlapping ranges rejected.
 auto unknown=bytes; // Rebuild a directory slot by shifting payload and every table offset.
 unknown.insert(unknown.begin()+108,16,0);u16(unknown,4,7);for(unsigned i=0;i<6;++i)u32(unknown,20+16*i,r32(unknown,20+16*i)+16);u32(unknown,108,tag("TEST"));u32(unknown,116,unknown.size());u32(unknown,120,4);unknown.resize(unknown.size()+4);assert(face(unknown)->glyph_count()==4);
 auto bad=unknown;u32(bad,108,tag("head"));face_error(bad,tt::Error::malformed);bad=unknown;u32(bad,116,offset(bad,"head"));face_error(bad,tt::Error::malformed);
 bad=bytes;u32(bad,entry(bad,"head")+8,4);face_error(bad,tt::Error::malformed);bad=bytes;u32(bad,entry(bad,"head")+12,0xffffffff);face_error(bad,tt::Error::malformed);
 bad=bytes;u32(bad,offset(bad,"head")+12,0);face_error(bad,tt::Error::malformed);bad=bytes;u16(bad,offset(bad,"head")+18,15);face_error(bad,tt::Error::malformed);
 for(unsigned units:{16u,16384u}){bad=bytes;u16(bad,offset(bad,"head")+18,units);assert(face(bad)->units_per_em()==units);}bad=bytes;u16(bad,offset(bad,"head")+18,16385);face_error(bad,tt::Error::malformed);
 bad=bytes;u16(bad,offset(bad,"hhea")+34,0);face_error(bad,tt::Error::malformed);bad=bytes;u16(bad,offset(bad,"hhea")+34,5);face_error(bad,tt::Error::malformed);bad=bytes;u32(bad,offset(bad,"loca")+8,0xffffffff);face_error(bad,tt::Error::malformed);
 // SFNT envelopes unsupported rather than mistaken for TrueType glyf.
 for(auto signature:{tag("OTTO"),tag("ttcf"),tag("wOFF"),tag("wOF2")}){bad=bytes;u32(bad,0,signature);face_error(bad,tt::Error::unsupported);}
 bad=triangle;u16(bad,6,111);glyph_error(bad,tt::Error::unsupported);bad=triangle;bad[14]|=128;glyph_error(bad,tt::Error::malformed);bad=triangle;bad[14]|=64;assert(outline(face(sfnt({bad})),0)->segments.size()==3);
 bad=triangle;bad.resize(11);glyph_error(bad,tt::Error::malformed);bad=triangle;u16(bad,12,0xffff);glyph_error(bad,tt::Error::malformed);
 // Repeated compact flags, zero-delta on-curve points and signed short deltas.
 Bytes compact(14);u16(compact,0,1);u16(compact,6,10);u16(compact,8,10);u16(compact,10,2);compact.insert(compact.end(),{0x31,0x33,0x27,10,10,10});assert(outline(face(sfnt({compact})),0)->segments.size()==3);
 Bytes repeated(14);u16(repeated,0,1);u16(repeated,10,2);repeated.insert(repeated.end(),{0x39,2});assert(outline(face(sfnt({repeated})),0)->contours.size()==1);repeated.back()=3;glyph_error(repeated,tt::Error::malformed);
 // Hard caps: 4096 points and 256 contours accepted; one above rejected.
 std::vector<P> many;for(unsigned i=0;i<4096;++i)many.push_back({int(i%100),int((i/100)%100),true});auto big=face(sfnt({glyph({many})}));assert(outline(big,0)->segments.size()==4096);many.push_back({0,0,true});glyph_error(glyph({many}),tt::Error::limit_exceeded);
 std::vector<std::vector<P>> contours(256,{{0,0,true},{1,0,true},{0,1,true}});assert(outline(face(sfnt({glyph(contours)})),0)->contours.size()==256);contours.push_back(contours.back());glyph_error(glyph(contours),tt::Error::limit_exceeded);
 // Directory caps, unknown duplicates, monotonic loca and signed accumulation.
 bad=bytes;for(unsigned i=0;i<122;++i)add_table(bad,0x70000000+i);assert(r16(bad,4)==128&&face(bad)->glyph_count()==4);add_table(bad,0x70000100);face_error(bad,tt::Error::limit_exceeded);
 bad=bytes;add_table(bad,tag("TEST"));add_table(bad,tag("TEST"));face_error(bad,tt::Error::malformed);
 bad=bytes;u32(bad,offset(bad,"loca")+12,2);face_error(bad,tt::Error::malformed);
 bad=bytes;u32(bad,entry(bad,"loca")+12,4);face_error(bad,tt::Error::malformed);
 bad=bytes;u32(bad,entry(bad,"hmtx")+12,3);face_error(bad,tt::Error::malformed);
 auto overflow=glyph({{{32767,0,true},{32767,1,true},{32766,0,true}}});u16(overflow,19,1);glyph_error(overflow,tt::Error::malformed);
 auto underflow=glyph({{{-32768,0,true},{-32768,1,true},{-32767,0,true}}});u16(underflow,19,0xffff);glyph_error(underflow,tt::Error::malformed);
 auto extremes=glyph({{{-32768,0,true},{-1,1,true},{32766,0,true}}});assert(outline(face(sfnt({extremes})),0)->bounds.x_min==-32768);
 // Full-size immutable source accepted; oversized storage is rejected at ownership boundary.
 bad=bytes;bad.resize(max_font_bytes);assert(face(bad)->glyph_count()==4);
 bool over_source=false;try{auto too_large=source(Bytes(max_font_bytes+1));(void)too_large;}catch(const std::invalid_argument&){over_source=true;}assert(over_source);
 // Outline-only profile explicitly rejects tables requiring another renderer/model.
 for(const char* name:{"CFF ","CFF2","fvar","gvar","avar","HVAR","VVAR","MVAR","COLR","CPAL","SVG ","CBDT","CBLC","sbix","EBDT","EBLC"}){bad=bytes;add_table(bad,tag(name));face_error(bad,tt::Error::unsupported);}
 // Missing required table and duplicate contour endpoints are malformed.
 for(const char* required:{"head","maxp","hhea","hmtx","loca","glyf"}){bad=bytes;u32(bad,entry(bad,required),tag("MISS"));face_error(bad,tt::Error::malformed);}
 auto endpoints=glyph({{{0,0,true},{10,0,true},{0,10,true}},{{20,20,true},{30,20,true},{20,30,true}}});u16(endpoints,12,2);glyph_error(endpoints,tt::Error::malformed);
 Bytes empty_inverted(10);u16(empty_inverted,2,100);u16(empty_inverted,6,0);assert(outline(face(sfnt({empty_inverted})),0)->segments.empty());
 // Deterministic one-bit mutations test robustness, not semantic acceptance.
 unsigned mutations=0;for(std::size_t index=0;index<bytes.size();++index)for(unsigned bit=0;bit<8;++bit){
  auto input=bytes;input[index]^=1u<<bit;auto owned=source(input);auto result=tt::ValidatedFace::create(owned);
  if(auto candidate=std::get_if<std::shared_ptr<const tt::ValidatedFace>>(&result)){
   for(unsigned id=0;id<(*candidate)->glyph_count();++id){auto shape=(*candidate)->outline(id);if(auto ok=std::get_if<std::shared_ptr<const tt::Outline>>(&shape)){assert((*ok)->segments.size()<=tt::max_segments&&(*ok)->contours.size()<=tt::max_contours);}}
  }
  assert(owned->bytes==input);++mutations;
 }
 // All strict SFNT prefixes are malformed; no partially retained face escapes.
 for(std::size_t length=1;length<bytes.size();++length)face_error(Bytes(bytes.begin(),bytes.begin()+length),tt::Error::malformed);
 // Diagnostic counters also expose the <=256 work-unit cancellation cadence.
 struct Cadence:tt::Cancellation{tt::Work& w;std::size_t previous=0,checks=0;explicit Cadence(tt::Work& v):w(v){}bool cancelled()noexcept override{assert(w.units>=previous&&w.units-previous<=256);previous=w.units;++checks;return false;}};
 tt::Work fw;Cadence fc(fw);assert(std::holds_alternative<std::shared_ptr<const tt::ValidatedFace>>(tt::ValidatedFace::create(src,0,&fc,&fw)));assert(fw.tables_checked==6&&fw.units-fc.previous<=256&&fw.cancel_checks==fc.checks);
 tt::Work ow;Cadence gc(ow);assert(std::holds_alternative<std::shared_ptr<const tt::Outline>>(big->outline(0,&gc,&ow)));assert(ow.points_decoded==4096&&ow.contours_retained==1&&ow.segments_retained==4096&&ow.units-gc.previous<=256&&ow.cancel_checks==gc.checks);
 // Cancellation and allocation failure never invalidate an already retained product.
 const auto digest=f->digest();auto preserved=a;unsigned face_cancels=0,outline_cancels=0,face_oom=0,outline_oom=0;
 Cancel count(std::numeric_limits<unsigned>::max());assert(std::holds_alternative<std::shared_ptr<const tt::ValidatedFace>>(tt::ValidatedFace::create(src,0,&count)));face_cancels=count.calls;
 for(unsigned i=1;i<=face_cancels;++i){Cancel c(i);assert(std::get<tt::Error>(tt::ValidatedFace::create(src,0,&c))==tt::Error::cancelled);}
 Cancel oc(std::numeric_limits<unsigned>::max());assert(std::holds_alternative<std::shared_ptr<const tt::Outline>>(big->outline(0,&oc)));outline_cancels=oc.calls;assert(outline_cancels>=4096/256);
 for(unsigned i=1;i<=outline_cancels;++i){Cancel c(i);assert(std::get<tt::Error>(big->outline(0,&c))==tt::Error::cancelled);}
 bool success=false;for(long i=0;i<200;++i){fail_after=i;auto r=tt::ValidatedFace::create(src);fail_after=-1;if(std::holds_alternative<std::shared_ptr<const tt::ValidatedFace>>(r)){success=true;break;}assert(std::get<tt::Error>(r)==tt::Error::out_of_memory);++face_oom;}assert(success&&face_oom>0);
 success=false;for(long i=0;i<200;++i){fail_after=i;auto r=big->outline(0);fail_after=-1;if(std::holds_alternative<std::shared_ptr<const tt::Outline>>(r)){success=true;break;}assert(std::get<tt::Error>(r)==tt::Error::out_of_memory);++outline_oom;}assert(success&&outline_oom>0);
 assert(f->digest()==digest&&preserved==a&&a->segments.size()==3&&a->segments[0].from==(tt::Point{10,20}));
 std::cout<<"Q1a synthetic errors="<<errors<<" mutations="<<mutations<<" face_cancel="<<face_cancels<<" outline_cancel="<<outline_cancels<<" face_OOM="<<face_oom<<" outline_OOM="<<outline_oom<<" PASS\n";
}
