#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/truetype_raster.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <array>
using namespace aether::rendering::text;
namespace tt=aether::rendering::text::truetype;
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
 for(unsigned i=0;i<n;++i){if(short_loca)u16(loca,i*2,glyf.size()/2);else u32(loca,i*4,glyf.size());glyf.insert(glyf.end(),glyphs[i].begin(),glyphs[i].end());if(short_loca&&glyf.size()%2)glyf.push_back(0);if(i<hmetrics){u16(hmtx,i*4,500+100*i);u16(hmtx,i*4+2,lsbs[i]);}else u16(hmtx,4*hmetrics+2*(i-hmetrics),lsbs[i]);}
 if(short_loca)u16(loca,n*2,glyf.size()/2);else u32(loca,n*4,glyf.size());
 std::vector<Table> tables{{tag("head"),head},{tag("maxp"),maxp},{tag("hhea"),hhea},{tag("hmtx"),hmtx},{tag("loca"),loca},{tag("glyf"),glyf}};
 Bytes out(12+16*tables.size());u32(out,0,0x10000);u16(out,4,tables.size());
 for(unsigned i=0;i<tables.size();++i){while(out.size()%4)out.push_back(0);u32(out,12+16*i,tables[i].tag);u32(out,20+16*i,out.size());u32(out,24+16*i,tables[i].bytes.size());out.insert(out.end(),tables[i].bytes.begin(),tables[i].bytes.end());}return out;
}
static std::size_t entry(const Bytes& b,const char* name){for(unsigned i=0;i<r16(b,4);++i)if(r32(b,12+16*i)==tag(name))return 12+16*i;std::abort();}
static std::size_t offset(const Bytes& b,const char* name){return r32(b,entry(b,name)+8);}
static auto source(Bytes b){return std::make_shared<const FontSource>(std::move(b),"synthetic Q1a test fixture");}
static auto face(const Bytes& b){auto r=tt::ValidatedFace::create(source(b));assert(std::holds_alternative<std::shared_ptr<const tt::ValidatedFace>>(r));return std::get<std::shared_ptr<const tt::ValidatedFace>>(r);}
static auto outline(const std::shared_ptr<const tt::ValidatedFace>& f,unsigned id){auto r=f->outline(id);assert(std::holds_alternative<std::shared_ptr<const tt::Outline>>(r));return std::get<std::shared_ptr<const tt::Outline>>(r);}

// Q1d-T independent synthetic corpus. Helpers above only serialize test fonts.
// Expected line geometry below is constructed from fixture vertices, not decoded output.
struct Component { unsigned gid,flags=3; int x=0,y=0; };
static Bytes compound(std::vector<Component> cs,std::array<int,4> bounds={0,0,10,10},Bytes instructions={}){
 Bytes b(10);u16(b,0,0xffff);for(int k=0;k<4;++k)u16(b,2+2*k,bounds[k]);
 bool instr=false;
 for(unsigned i=0;i<cs.size();++i){auto c=cs[i];unsigned flags=c.flags|(i+1<cs.size()?32:0);instr|=flags&256;append16(b,flags);append16(b,c.gid);
 if(flags&1){append16(b,c.x);append16(b,c.y);}else{b.push_back(c.x);b.push_back(c.y);}
 unsigned n=(flags&128)?8:(flags&64)?4:(flags&8)?2:0;while(n--)b.push_back(0);
 }
 if(instr){append16(b,instructions.size());b.insert(b.end(),instructions.begin(),instructions.end());}return b;
}
static unsigned fixtures=0;
static auto ok(std::vector<Bytes> gs,unsigned id,tt::Work* w=nullptr){auto f=face(sfnt(gs));auto r=f->outline(id,nullptr,w);if(std::holds_alternative<tt::Error>(r)){std::cerr<<"fixture "<<fixtures<<" unexpected error "<<int(std::get<tt::Error>(r))<<'\n';std::abort();}++fixtures;return std::get<std::shared_ptr<const tt::Outline>>(r);}
static void err(std::vector<Bytes> gs,unsigned id,tt::Error e){auto r=face(sfnt(gs))->outline(id);if(!std::holds_alternative<tt::Error>(r)||std::get<tt::Error>(r)!=e){std::cerr<<"fixture "<<fixtures<<" expected "<<int(e)<<" got "<<(std::holds_alternative<tt::Error>(r)?int(std::get<tt::Error>(r)):-1)<<'\n';std::abort();}++fixtures;}
static void triangle_oracle(const tt::Outline& o,std::vector<std::pair<int,int>> shifts){assert(o.contours.size()==shifts.size());assert(o.segments.size()==3*shifts.size());for(unsigned c=0;c<shifts.size();++c){auto [x,y]=shifts[c];std::array<tt::Point,3> v{{{double(x),double(y)},{double(x+10),double(y)},{double(x),double(y+10)}}};assert(o.contours[c].first_segment==3*c&&o.contours[c].segment_count==3);for(unsigned j=0;j<3;++j){auto& s=o.segments[c*3+j];assert(s.kind==tt::SegmentKind::line&&s.from==v[j]&&s.to==v[(j+1)%3]);}}}
struct Cancel:tt::Cancellation{unsigned calls=0,at;tt::Work* work;std::size_t previous=0;explicit Cancel(unsigned n,tt::Work* w=nullptr):at(n),work(w){}bool cancelled()noexcept override{if(work){assert(work->units-previous<=256);previous=work->units;}return ++calls==at;}};
int main(){
 const auto tri=glyph({{{0,0,true},{10,0,true},{0,10,true}}});
 std::vector<Bytes> seeds;
 // All four signed-byte corners, signed-word extrema, nonzero negative translations.
 for(auto [x,y]:std::vector<std::pair<int,int>>{{0,0},{-128,-128},{127,127},{-128,127},{127,-128},{-32768,-32768},{32757,32757},{-1234,2345}}){auto c=compound({{0,(x>=-128&&x<=127&&y>=-128&&y<=127)?2u:3u,x,y}},{x,y,x+10,y+10});auto o=ok({tri,c},1);triangle_oracle(*o,{{x,y}});assert(o->metrics.advance==600&&o->metrics.lsb==0&&o->metrics.pp1==x);seeds.push_back(sfnt({tri,c}));}
 for(unsigned flag:{7u,0x403u,0x803u,0x1003u}){auto o=ok({tri,compound({{0,flag,0,0}})},1);triangle_oracle(*o,{{0,0}});}
 // Nested translated geometry, repeated shared DAG leaves and reference accounting.
 auto nested=compound({{0,3,5,7}},{5,7,15,17});tt::Work w{};
 auto o=ok({tri,nested,compound({{1,3,-5,-7},{1,3,15,13}},{0,0,30,30})},2,&w);triangle_oracle(*o,{{0,0},{20,20}});assert(w.component_references==4&&w.max_component_depth==2);
 for(unsigned depth:{15u,16u,17u}){std::vector<Bytes> gs{tri};for(unsigned i=0;i<depth;++i)gs.push_back(compound({{i}}));if(depth<=16){tt::Work ww{};triangle_oracle(*ok(gs,depth,&ww),{{0,0}});assert(ww.max_component_depth==depth&&ww.component_references==depth);}else err(gs,depth,tt::Error::limit_exceeded);}
 err({compound({{0}})},0,tt::Error::malformed);err({compound({{1}}),compound({{0}})},0,tt::Error::malformed);
 for(unsigned refs:{255u,256u,257u}){std::vector<Component> cs(refs,Component{0});auto g=compound(cs,{0,0,0,0});if(refs<=256){tt::Work ww{};assert(ok({{},g},1,&ww)->segments.empty());assert(ww.component_references==refs);}else err({{},g},1,tt::Error::limit_exceeded);}
 triangle_oracle(*ok({{},tri,compound({{0},{1},{0}})},2),{{0,0}});
 // Local aggregate point limits, spread across legal simple children.
 for(unsigned n:{4095u,4096u,4097u}){std::vector<P> a(n/2,P{0,0,true}),b(n-n/2,P{0,0,true});auto g=compound({{0},{1}},{0,0,0,0});std::vector<Bytes> gs{glyph({a}),glyph({b}),g};if(n<=4096){tt::Work ww{};ok(gs,2,&ww);assert(ww.points_decoded==n);}else err(gs,2,tt::Error::limit_exceeded);}
 for(unsigned n:{255u,256u,257u}){std::vector<std::vector<P>> a(n/2,{{0,0,true}}),b(n-n/2,{{0,0,true}});std::vector<Bytes> gs{glyph(a),glyph(b),compound({{0},{1}},{0,0,0,0})};if(n<=256)assert(ok(gs,2)->contours.size()==n);else err(gs,2,tt::Error::limit_exceeded);}
 // Empty composites ignore header bbox, unlike nonempty inverted bounds.
 assert(ok({{},compound({{0}},{10,20,-10,-20})},1)->contours.empty());
 err({tri,compound({{0}},{10,20,-10,-20})},1,tt::Error::malformed);
 err({glyph({{{-1,0,true}}}),compound({{0,3,-32768,0}},{0,0,0,0})},1,tt::Error::unsupported);
 // Coordinate extrema valid exactly; one step beyond is unsupported, not overflow.
 for(int x:{-32768,32767}){auto point=glyph({{{0,0,true}}});assert(ok({point,compound({{0,3,x,0}},{x,0,x,0})},1)->bounds.x_min==x);}
 err({tri,compound({{0,3,32758,0}},{32758,0,32767,10})},1,tt::Error::unsupported);
 err({tri,nested,compound({{1,3,-32768,0}},{0,0,10,10})},2,tt::Error::unsupported);
 err({tri,compound({{0}},{1,0,10,10})},1,tt::Error::unsupported);
 // Whole-local-record validation, instructions on first or final component.
 for(unsigned first:{0u,256u}){auto c=compound({{0,3|first},{0,3|(first?0u:256u)}},{0,0,10,10},{1,2,3});triangle_oracle(*ok({tri,c},1),{{0,0},{0,0}});c.pop_back();err({tri,c},1,tt::Error::malformed);}
 for(unsigned flag:{0x13u,0x2003u,0x4003u,0x8003u,0x4bu})err({tri,compound({{0,flag}})},1,tt::Error::malformed);
 for(unsigned flag:{0xbu,0x43u,0x83u,0x1803u}){auto c=compound({{0,flag}});err({tri,c},1,tt::Error::unsupported);c.resize(15);err({tri,c},1,tt::Error::malformed);seeds.push_back(sfnt({tri,compound({{0,flag}})}));}
 err({tri,compound({{0,1}})},1,tt::Error::malformed);err({tri,compound({{0},{0,1}})},1,tt::Error::unsupported);
 err({tri,compound({{2}})},1,tt::Error::malformed);
 // Missing second record, argument, or instruction-length must never be accepted.
 auto valid=compound({{0},{0}});for(unsigned len:{10u,11u,13u,15u,17u,19u,21u,23u,25u}){auto cut=valid;cut.resize(len);err({tri,cut},1,tt::Error::malformed);}
 // Metrics provider must have parent-local identical metrics, zero shift and unique provider.
 auto neutral=sfnt({tri,compound({{0,0x203}})});u16(neutral,offset(neutral,"hmtx")+4,500);
 {auto r=outline(face(neutral),1);triangle_oracle(*r,{{0,0}});assert(r->metrics.advance==500&&r->metrics.pp1==0);++fixtures;}
 err({tri,compound({{0,0x203}})},1,tt::Error::unsupported);
 for(auto c:{compound({{0,0x203,1,0}},{1,0,11,10}),compound({{0,0x203},{0,0x203}}),compound({{0,0x203},{0,3,-1,0}},{-1,0,10,10})}){auto b=sfnt({tri,c});u16(b,offset(b,"hmtx")+4,500);auto r=face(b)->outline(1);assert(std::get<tt::Error>(r)==tt::Error::unsupported);++fixtures;}
 err({{},compound({{0,0x203}},{0,0,0,0})},1,tt::Error::unsupported);
 // A8 equivalence against independently serialized already-flat geometry, exact bytes.
 auto comp=ok({tri,nested,compound({{1,3,-5,-7},{1,3,15,13}},{0,0,30,30})},2);
 auto flat=outline(face(sfnt({glyph({{{0,0,true},{10,0,true},{0,10,true}},{{20,20,true},{30,20,true},{20,30,true}}})})),0);
 unsigned raster_checks=0;
 for(int size:{16*64,64*64,128*64})for(unsigned phase:{0u,17u,63u}){
 auto render=[&](auto obj){auto fi=std::make_shared<const FontInstance>(obj->source(),0,size,1);auto r=tt::rasterize(obj,fi,phase,phase);assert(std::holds_alternative<tt::RasterOutput>(r));return std::get<tt::RasterOutput>(r).mask;};
 auto a=render(comp),b=render(flat);assert(a->bytes==b->bytes&&a->width==b->width&&a->height==b->height&&a->stride==b->stride&&a->bearing_left==b->bearing_left&&a->bearing_top==b->bearing_top);++raster_checks;}
 // Quadratic independent literal oracle: implied midpoint, negative nested translation,
 // and control coordinates must all move, not merely endpoints.
 auto q=glyph({{{0,0,false},{100,0,false},{100,100,true},{0,100,false}}});
 auto qo=ok({q,compound({{0,3,3,-2}},{3,-2,103,98}),compound({{1,3,-10,6}},{-7,4,93,104})},2);
 std::array<tt::Segment,3> expected{{{tt::SegmentKind::quadratic,{-7,54},{-7,4},{43,4}},{tt::SegmentKind::quadratic,{43,4},{93,4},{93,104}},{tt::SegmentKind::quadratic,{93,104},{-7,104},{-7,54}}}};
 assert(qo->segments.size()==expected.size());for(unsigned i=0;i<3;++i){auto a=qo->segments[i],b=expected[i];assert(a.kind==b.kind&&a.from==b.from&&a.to==b.to&&a.control==b.control);}
 auto qflat=outline(face(sfnt({glyph({{{-7,4,false},{93,4,false},{93,104,true},{-7,104,false}}})})),0);
 for(int size:{16*64,64*64,128*64})for(unsigned phase:{0u,17u,63u}){auto render=[&](auto obj){auto fi=std::make_shared<const FontInstance>(obj->source(),0,size,1);auto r=tt::rasterize(obj,fi,phase,phase);assert(std::holds_alternative<tt::RasterOutput>(r));return std::get<tt::RasterOutput>(r).mask;};auto a=render(qo),b=render(qflat);assert(a->bytes==b->bytes&&a->width==b->width&&a->height==b->height&&a->bearing_left==b->bearing_left&&a->bearing_top==b->bearing_top);++raster_checks;}
 auto recursive_metrics=sfnt({tri,compound({{0,0x203}}),compound({{1,0x203}})});auto hm=offset(recursive_metrics,"hmtx");u16(recursive_metrics,hm+4,500);u16(recursive_metrics,hm+8,500);triangle_oracle(*outline(face(recursive_metrics),2),{{0,0}});++fixtures;
 u16(recursive_metrics,hm+4,600);assert(std::get<tt::Error>(face(recursive_metrics)->outline(2))==tt::Error::unsupported);++fixtures;
 // Larger corpus cancellation cadence includes 4096 points through depth16.
 std::vector<P> dense_points(4096,P{0,0,true});std::vector<Bytes> dense{glyph({dense_points})};for(unsigned i=0;i<16;++i)dense.push_back(compound({{i}},{0,0,0,0}));
 auto df=face(sfnt(dense));tt::Work dw{};Cancel dc(~0u,&dw);assert(std::holds_alternative<std::shared_ptr<const tt::Outline>>(df->outline(16,&dc,&dw)));assert(dw.units-dc.previous<=256);++fixtures;
 // Root identity/source lifetime separate from all component identities.
 std::weak_ptr<const FontSource> weak;std::shared_ptr<const tt::Outline> survivor;{auto src=source(neutral);weak=src;auto f=std::get<std::shared_ptr<const tt::ValidatedFace>>(tt::ValidatedFace::create(src));survivor=outline(f,1);assert(survivor->source()==src&&survivor->glyph_id==1&&survivor->units_per_em()==1024&&survivor->face_index()==0);}assert(!weak.expired());survivor.reset();assert(weak.expired());++fixtures;
 // Every observable cancellation checkpoint and allocation fails atomically.
 auto f=face(sfnt({tri,nested,compound({{1,3,-5,-7},{1,3,15,13}},{0,0,30,30})}));tt::Work cadence{};Cancel count(~0u,&cadence);assert(std::holds_alternative<std::shared_ptr<const tt::Outline>>(f->outline(2,&count,&cadence)));
 for(unsigned i=1;i<=count.calls;++i){Cancel c(i);auto r=f->outline(2,&c);assert(std::holds_alternative<tt::Error>(r)&&std::get<tt::Error>(r)==tt::Error::cancelled);}
 unsigned allocations=0;for(;allocations<500;++allocations){fail_after=allocations;auto r=f->outline(2);fail_after=-1;if(std::holds_alternative<std::shared_ptr<const tt::Outline>>(r))break;assert(std::get<tt::Error>(r)==tt::Error::out_of_memory);}assert(allocations>0&&allocations<500);
 seeds.push_back(neutral);seeds.push_back(sfnt({tri,nested,compound({{1,3,-5,-7},{1,3,15,13}},{0,0,30,30})}));
 // Deterministic structure-preserving outer fonts: mutations focus on glyf payload,
 // so malformed face rejection cannot hide composite decoder coverage.
 std::uint32_t rng=0x514454u;unsigned faces=0,accepted=0;std::size_t max_units=0;auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
 for(unsigned i=0;i<20000;++i){auto b=seeds[i%seeds.size()];auto begin=offset(b,"glyf");auto length=r32(b,entry(b,"glyf")+12);for(unsigned j=0,n=1+next()%4;j<n;++j)b[begin+next()%length]^=1u<<(next()%8);auto fr=tt::ValidatedFace::create(source(std::move(b)));if(auto fp=std::get_if<std::shared_ptr<const tt::ValidatedFace>>(&fr)){++faces;tt::Work work{};auto r=(*fp)->outline((*fp)->glyph_count()-1,nullptr,&work);max_units=std::max(max_units,work.units);assert(work.component_references<=256&&work.max_component_depth<=16&&work.units<=1000000);if(auto out=std::get_if<std::shared_ptr<const tt::Outline>>(&r)){++accepted;assert((*out)->segments.size()<=tt::max_segments&&(*out)->contours.size()<=tt::max_contours&&(*out)->glyph_id==(*fp)->glyph_count()-1);}}}
 assert(fixtures>=40&&faces==20000);std::cout<<"Q1d-T independent fixtures="<<fixtures<<" mutations=20000 decoded_faces="<<faces<<" mutation_success="<<accepted<<" cancel_points="<<count.calls<<" dense_units="<<dw.units<<" dense_checks="<<dc.calls<<" max_mutation_units="<<max_units<<" exact_A8="<<raster_checks<<" OOM_points="<<allocations<<" PASS\n";
 // Segment cap8192 is dominated by4096 decoded points; no fictitious boundary claim.
 // Hard1M budget boundary is separately assessed; no production seam added here.
}
