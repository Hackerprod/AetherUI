#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/truetype_raster.h"
#include <cmath>
#include <chrono>
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
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static void release_test_allocation(void* p) noexcept {std::free(p);}
void operator delete(void* p)noexcept{release_test_allocation(p);}void operator delete(void* p,std::size_t)noexcept{release_test_allocation(p);}
static void u16(Bytes& b,std::size_t p,unsigned v){assert(p+2<=b.size());b[p]=v>>8;b[p+1]=v;}
static void u32(Bytes& b,std::size_t p,std::uint32_t v){assert(p+4<=b.size());u16(b,p,v>>16);u16(b,p+2,v);}
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
static Bytes sfnt(std::vector<Bytes> glyphs,bool short_loca=false,unsigned hmetrics=0,std::vector<int> lsbs={},unsigned units=256){
 const unsigned n=glyphs.size();if(!hmetrics)hmetrics=n;if(lsbs.empty())lsbs.resize(n);
 Bytes head(54),maxp(32),hhea(36),hmtx(4*hmetrics+2*(n-hmetrics)),loca((n+1)*(short_loca?2:4)),glyf;
 u32(head,0,0x10000);u32(head,12,0x5f0f3cf5);u16(head,18,units);u16(head,50,short_loca?0:1);u32(maxp,0,0x10000);u16(maxp,4,n);u32(hhea,0,0x10000);u16(hhea,34,hmetrics);
 for(unsigned i=0;i<n;++i){if(short_loca)u16(loca,i*2,glyf.size()/2);else u32(loca,i*4,glyf.size());glyf.insert(glyf.end(),glyphs[i].begin(),glyphs[i].end());if(glyf.size()%2)glyf.push_back(0);if(i<hmetrics){u16(hmtx,i*4,500+100*i);u16(hmtx,i*4+2,lsbs[i]);}else u16(hmtx,4*hmetrics+2*(i-hmetrics),lsbs[i]);}
 if(short_loca)u16(loca,n*2,glyf.size()/2);else u32(loca,n*4,glyf.size());
 std::vector<Table> tables{{tag("head"),head},{tag("maxp"),maxp},{tag("hhea"),hhea},{tag("hmtx"),hmtx},{tag("loca"),loca},{tag("glyf"),glyf}};
 Bytes out(12+16*tables.size());u32(out,0,0x10000);u16(out,4,tables.size());
 for(unsigned i=0;i<tables.size();++i){while(out.size()%4)out.push_back(0);u32(out,12+16*i,tables[i].tag);u32(out,20+16*i,out.size());u32(out,24+16*i,tables[i].bytes.size());out.insert(out.end(),tables[i].bytes.begin(),tables[i].bytes.end());}return out;
}
static auto source(Bytes b){return std::make_shared<const FontSource>(std::move(b),"synthetic Q1a test fixture");}
static auto face(const Bytes& b){auto r=tt::ValidatedFace::create(source(b));assert(std::holds_alternative<std::shared_ptr<const tt::ValidatedFace>>(r));return std::get<std::shared_ptr<const tt::ValidatedFace>>(r);}
static auto outline(const std::shared_ptr<const tt::ValidatedFace>& f,unsigned id){auto r=f->outline(id);assert(std::holds_alternative<std::shared_ptr<const tt::Outline>>(r));return std::get<std::shared_ptr<const tt::Outline>>(r);}
struct Edge{tt::Point a,c,b;bool quad;};
// Explicit source curves are independent of parser conversion and raster splitting.
static const std::vector<Edge> curve_edges{{{16,32},{112,224},{208,32},true},{{208,32},{},{16,32},false}};
static std::vector<Edge> transformed(const std::vector<Edge>& edges,double scale,double px,double py,int pp1){auto out=edges;for(auto& e:out)for(auto* p:{&e.a,&e.c,&e.b}){p->x=(p->x-pp1)*scale+px;p->y=-p->y*scale+py;}return out;}
struct Crossing{long double x;int sign;};
static std::vector<std::pair<long double,long double>> intervals(const std::vector<Edge>& edges,long double y){
 std::vector<Crossing> events;
 for(const auto& e:edges){
  if(!e.quad){if(e.a.y==e.b.y||y<std::min(e.a.y,e.b.y)||y>=std::max(e.a.y,e.b.y))continue;long double t=(y-e.a.y)/(e.b.y-e.a.y);events.push_back({e.a.x+t*(e.b.x-e.a.x),e.b.y>e.a.y?1:-1});continue;}
  const long double a=e.a.y-2.L*e.c.y+e.b.y,b=2.L*(e.c.y-e.a.y),c=e.a.y-y;
  std::vector<long double> roots;if(a==0){if(b!=0)roots.push_back(-c/b);}else{long double d=b*b-4*a*c;if(d>0){roots.push_back((-b-std::sqrt(d))/(2*a));roots.push_back((-b+std::sqrt(d))/(2*a));}}
  for(long double t:roots){if(t<0||t>1)continue;long double dy=2*a*t+b;if(dy==0||(t==0&&dy<0)||(t==1&&dy>0))continue;long double u=1-t;events.push_back({u*u*e.a.x+2*u*t*e.c.x+t*t*e.b.x,dy>0?1:-1});}
 }
 std::sort(events.begin(),events.end(),[](auto a,auto b){return a.x<b.x;});std::vector<std::pair<long double,long double>> out;int winding=0;long double start=0;
 for(std::size_t i=0;i<events.size();){auto x=events[i].x;int change=0;while(i<events.size()&&events[i].x==x)change+=events[i++].sign;const auto before=winding;winding+=change;if(!before&&winding)start=x;if(before&&!winding)out.push_back({start,x});}return out;
}
static std::vector<long double> reference_slow(const GlyphMaskA8& m,const std::vector<Edge>& edges,unsigned rows,bool grid){
 std::vector<long double> out(m.width*m.height);
 for(unsigned py=0;py<m.height;++py)for(unsigned sample=0;sample<rows;++sample){auto ranges=intervals(edges,-static_cast<long double>(m.bearing_top)+py+(sample+.5L)/rows);for(unsigned px=0;px<m.width;++px){long double x=m.bearing_left+px,covered=0;for(auto [left,right]:ranges){if(grid){for(unsigned sx=0;sx<16;++sx){auto v=x+(sx+.5L)/16;covered+=(v>=left&&v<right)?1.L/16:0;}}else covered+=std::max(0.L,std::min(x+1,right)-std::max(x,left));}out[py*m.width+px]+=covered/rows;}}
 return out;
}
// Equivalent area accumulation: whole interior pixels use a difference array.
// Boundary pixels still integrate the exact analytic horizontal intervals.
static std::vector<long double> reference(const GlyphMaskA8& m,const std::vector<Edge>& edges,unsigned rows,bool grid){
 if(grid)return reference_slow(m,edges,rows,true);
 std::vector<long double> out(m.width*m.height);
 for(unsigned py=0;py<m.height;++py){std::vector<long double> partial(m.width),delta(m.width+1);
  for(unsigned sample=0;sample<rows;++sample){auto ranges=intervals(edges,-static_cast<long double>(m.bearing_top)+py+(sample+.5L)/rows);
   for(auto [left,right]:ranges){left=std::clamp(left-m.bearing_left,0.L,(long double)m.width);right=std::clamp(right-m.bearing_left,0.L,(long double)m.width);if(right<=left)continue;
    auto first=unsigned(std::floor(left)),last=unsigned(std::ceil(right)-1);
    if(first==last)partial[first]+=right-left;
    else{partial[first]+=first+1-left;partial[last]+=right-last;if(last>first+1){delta[first+1]+=1;delta[last]-=1;}}
   }
  }
  long double full=0;for(unsigned x=0;x<m.width;++x){full+=delta[x];out[py*m.width+x]=(partial[x]+full)/rows;}
 }
 return out;
}
static auto font(const std::shared_ptr<const tt::Outline>& o,int size,unsigned flags=1,unsigned face_id=0){return std::make_shared<const FontInstance>(o->source(),face_id,size,flags);}
static tt::RasterOutput render(const std::shared_ptr<const tt::Outline>& o,int size,unsigned px=0,unsigned py=0,tt::RasterWork* w=nullptr){auto r=tt::rasterize(o,font(o,size),px,py,nullptr,w);assert(std::holds_alternative<tt::RasterOutput>(r));return std::get<tt::RasterOutput>(r);}
static void same(const GlyphMaskA8& a,const GlyphMaskA8& b){assert(a.width==b.width&&a.height==b.height&&a.stride==b.stride&&a.bearing_left==b.bearing_left&&a.bearing_top==b.bearing_top&&a.bytes==b.bytes);}
static void same_ink(const GlyphMaskA8& a,const GlyphMaskA8& b){
 auto pixel=[](const GlyphMaskA8& m,int x,int y){x-=m.bearing_left;y+=m.bearing_top;return x>=0&&y>=0&&x<int(m.width)&&y<int(m.height)?m.bytes[y*m.stride+x]:0;};
 for(int y=std::min(-a.bearing_top,-b.bearing_top);y<std::max(-a.bearing_top+int(a.height),-b.bearing_top+int(b.height));++y)
  for(int x=std::min(a.bearing_left,b.bearing_left);x<std::max(a.bearing_left+int(a.width),b.bearing_left+int(b.width));++x)assert(pixel(a,x,y)==pixel(b,x,y));
}
static void counters(const tt::RasterWork& w,const GlyphMaskA8& m){
 std::uint64_t l=0;for(std::uint64_t n=1;n<std::max<std::uint64_t>(w.split_segments,1);n*=2)++l;
 const std::uint64_t scans=64ULL*m.height,pixels=std::uint64_t(m.width)*m.height,s=w.split_segments;
 assert(w.generated_pieces<=8192&&s<=w.generated_pieces);
 assert(w.reserved==scans*s*(96+8*(l+1))+12*pixels+80*m.height+16*m.width+8*(w.transformed_segments+w.generated_pieces)+256);
 assert(w.edge_checks<=scans*s&&w.root_steps<=32*scans*s);
 // Output initialization, two row arrays (delta has sentinel), resets and quantization.
 assert(w.sample_tests==0&&w.output_operations==4*pixels+2*m.width+m.height+1);
 assert(w.sort_comparisons<=4*(l+1)*scans*s&&w.sort_swaps<=2*(l+1)*scans*s);
 assert(w.sweep_operations<=16*scans*s&&w.units<=w.reserved);
}
struct Cancel:tt::Cancellation{std::uint64_t previous=0;unsigned calls=0,at;tt::RasterWork* work;Cancel(unsigned n,tt::RasterWork* w=nullptr):at(n),work(w){}bool cancelled()noexcept override{if(work){assert(work->units>=previous&&work->units-previous<=64);previous=work->units;}return ++calls==at;}};
int main(){
 const std::vector<P> curve{{16,32,true},{112,224,false},{208,32,true}};
 auto make=[&](std::vector<std::vector<P>> contours,int lsb=16){return outline(face(sfnt({glyph(contours)},false,1,{lsb})),0);};
 const auto o=make({curve});unsigned cases=0;long double worst=0,convergence=0;
 for(unsigned size:{8u,12u,16u,32u,128u})for(unsigned phase:{0u,1u,31u,63u}){
  tt::RasterWork work;auto r=render(o,size*64,phase,phase,&work);const auto& m=*r.mask;const double scale=size/256.;
  assert(m.font->source==o->source()&&m.glyph_id==0&&m.flags==1&&m.phase_x==phase&&m.phase_y==phase);
  assert(m.bearing_left==int(std::floor(16*scale+phase/64.))&&m.bearing_top==-int(std::floor(-224*scale+phase/64.)));
  assert(m.width==std::ceil(208*scale+phase/64.)-m.bearing_left&&m.height==std::ceil(-32*scale+phase/64.)+m.bearing_top);
  assert(r.advance_px==500*scale&&r.advance_26_6==int(std::floor(500*scale*64+.5)));
  auto edges=transformed(curve_edges,scale,phase/64.,phase/64.,0);auto grid=reference_slow(m,edges,64,false),coarse=reference(m,edges,4096,false),fine=reference(m,edges,8192,false);
  if(size==8&&phase==0){auto slow=reference_slow(m,edges,8192,false);for(std::size_t i=0;i<slow.size();++i)assert(std::abs(slow[i]-fine[i])<1e-12L);}
  for(std::size_t i=0;i<fine.size();++i){if(std::abs(int(m.bytes[i])-int(std::floor(grid[i]*255+.5L)))>1){std::cerr<<"GRID FAIL size="<<size<<" phase="<<phase<<" index="<<i<<" actual="<<int(m.bytes[i])<<" expected="<<double(grid[i]*255)<<" bbox="<<m.width<<","<<m.height<<","<<m.bearing_left<<","<<m.bearing_top<<"\n";std::abort();}worst=std::max(worst,std::abs(m.bytes[i]/255.L-fine[i]));convergence=std::max(convergence,std::abs(coarse[i]-fine[i]));}
  assert(work.reserved<=tt::max_raster_work);counters(work,m);++cases;
 }
 assert(worst<=8.L/255&&convergence<.1L/255);
 // Broader fixed geometry corpus uses explicit curves, never renderer output.
 struct Fixture{std::vector<std::vector<P>> contours;std::vector<Edge> edges;};
 auto polygon=[](const std::vector<P>& points){std::vector<Edge> out;for(std::size_t i=0;i<points.size();++i){auto a=points[i],b=points[(i+1)%points.size()];out.push_back({{double(a.x),double(a.y)},{},{double(b.x),double(b.y)},false});}return out;};
 const std::vector<P> rectangle{{32,32,true},{224,32,true},{224,224,true},{32,224,true}};
 const std::vector<P> triangle_points{{32,32,true},{224,64,true},{80,224,true}};
 const std::vector<P> thin{{16,32,true},{18,224,true},{20,32,true}};
 const std::vector<P> bowtie{{32,32,true},{224,224,true},{224,32,true},{32,224,true}};
 const std::vector<P> hole{{96,96,true},{96,160,true},{160,160,true},{160,96,true}};
 std::vector<Fixture> fixtures;for(const auto& p:{rectangle,triangle_points,thin,bowtie})fixtures.push_back({{p},polygon(p)});
 auto hole_edges=polygon(rectangle),hole_inner=polygon(hole);hole_edges.insert(hole_edges.end(),hole_inner.begin(),hole_inner.end());fixtures.push_back({{rectangle,hole},hole_edges});
 const std::vector<P> rounded{{128,32,true},{224,32,false},{224,128,true},{224,224,false},{128,224,true},{32,224,false},{32,128,true},{32,32,false}};
 const std::vector<Edge> rounded_edges{{{128,32},{224,32},{224,128},true},{{224,128},{224,224},{128,224},true},{{128,224},{32,224},{32,128},true},{{32,128},{32,32},{128,32},true}};
 fixtures.push_back({{rounded},rounded_edges});
 fixtures.push_back({{{{32,32,false},{224,32,false},{224,224,false},{32,224,false}}},{{{32,128},{32,32},{128,32},true},{{128,32},{224,32},{224,128},true},{{224,128},{224,224},{128,224},true},{{128,224},{32,224},{32,128},true}}});
 // Two quadratic loops tangent at one point, preserving nonzero winding.
 auto tangent_points=rounded;for(auto& p:tangent_points)p.x+=192;auto tangent_edges=rounded_edges;for(auto& e:tangent_edges)for(auto* p:{&e.a,&e.c,&e.b})p->x+=192;auto tangent_all=rounded_edges;tangent_all.insert(tangent_all.end(),tangent_edges.begin(),tangent_edges.end());fixtures.push_back({{rounded,tangent_points},tangent_all});
 unsigned fixture_index=0;
 for(const auto& fixture:fixtures){for(unsigned size:{8u,12u,16u,32u,128u})for(unsigned phase:{0u,1u,31u,63u}){
  auto shape=make(fixture.contours);tt::RasterWork detail;auto result=render(shape,size*64,phase,phase,&detail);const auto& mask=*result.mask;counters(detail,mask);
  auto edges=transformed(fixture.edges,size/256.,phase/64.,phase/64.,shape->metrics.pp1);
  auto coarse=reference(mask,edges,4096,false),fine=reference(mask,edges,8192,false),grid=reference_slow(mask,edges,64,false);
  for(std::size_t i=0;i<fine.size();++i){assert(std::abs(int(mask.bytes[i])-int(std::floor(grid[i]*255+.5L)))<=1);worst=std::max(worst,std::abs(mask.bytes[i]/255.L-fine[i]));convergence=std::max(convergence,std::abs(coarse[i]-fine[i]));}
  if(size==8&&phase==0&&fixture_index<=4){auto slow=reference_slow(mask,edges,8192,false);for(std::size_t i=0;i<slow.size();++i)assert(std::abs(slow[i]-fine[i])<1e-12L);}
  ++cases;
 }++fixture_index;}
 assert(worst<=8.L/255&&convergence<.1L/255);
 // Exact topology is not covered by a visual error tolerance.
 auto reverse=curve;std::reverse(reverse.begin(),reverse.end());auto duplicate=make({curve,curve}),reversed=make({reverse}),cancelled=make({curve,reverse});
 const std::vector<P> inner{{64,64,true},{112,112,false},{160,64,true}};auto permutation_a=make({curve,inner}),permutation_b=make({inner,curve});
 for(unsigned size:{8u,12u,16u,32u,128u})for(unsigned phase:{0u,1u,31u,63u}){auto base=render(o,size*64,phase,phase);same(*base.mask,*render(duplicate,size*64,phase,phase).mask);same(*base.mask,*render(reversed,size*64,phase,phase).mask);auto zero=render(cancelled,size*64,phase,phase);for(auto b:zero.mask->bytes)assert(b==0);same(*render(permutation_a,size*64,phase,phase).mask,*render(permutation_b,size*64,phase,phase).mask);}
 // Equivalent explicit midpoint subdivision may tighten control bbox, but ink is exact.
 const std::vector<P> split_curve{{16,32,true},{64,128,false},{112,128,true},{160,128,false},{208,32,true}};
 auto split_reverse=split_curve;std::reverse(split_reverse.begin(),split_reverse.end());auto subdivided=make({split_curve}),subdivided_reverse=make({split_reverse});
 for(unsigned size:{8u,12u,16u,32u,128u})for(unsigned phase:{0u,1u,31u,63u}){auto original=render(o,size*64,phase,phase),split=render(subdivided,size*64,phase,phase),rev=render(subdivided_reverse,size*64,phase,phase);same_ink(*original.mask,*split.mask);same(*split.mask,*rev.mask);}
 auto singleton=make({{{32,64,true}}},0);auto zero=render(singleton,16*64);assert(zero.mask->width==0&&zero.mask->height==0&&zero.mask->bearing_left==0&&zero.mask->bearing_top==0&&zero.mask->bytes.empty());
 // S=0 while bbox has area: two horizontal retraced contours, no coverage.
 auto horizontal=make({{{0,0,true},{256,0,true}},{{0,256,true},{256,256,true}}},0);
 tt::RasterWork horizontal_work;auto horizontal_mask=render(horizontal,8*64,0,0,&horizontal_work);
 assert(horizontal_mask.mask->width==8&&horizontal_mask.mask->height==8&&horizontal_work.split_segments==0);
 counters(horizontal_work,*horizontal_mask.mask);for(auto byte:horizontal_mask.mask->bytes)assert(byte==0);
 // Integer interval endpoints hit exactly 0 and W; full row is exactly opaque.
 auto full_box=make({{{0,0,true},{256,0,true},{256,256,true},{0,256,true}}},0);
 auto full_mask=render(full_box,8*64);for(auto byte:full_mask.mask->bytes)assert(byte==255);
 auto retraced=make({{{16,32,true},{112,224,false},{208,32,true},{112,224,false}}});auto retraced_result=render(retraced,32*64,31,63);for(auto b:retraced_result.mask->bytes)assert(b==0);
 // Phase and pp1 are placement, never a hidden baseline translation.
 auto shifted=make({curve},-4);auto shifted_mask=render(shifted,16*64,1,63);assert(shifted->metrics.pp1==20);assert(shifted_mask.mask->bearing_left==int(std::floor((16-20)*16/256.+1/64.)));
 for(int size:{1,8192}){assert(render(o,size).mask);}
 auto f=font(o,16*64);
 auto invalid=[&](std::shared_ptr<const tt::Outline> outline,std::shared_ptr<const FontInstance> instance,unsigned px,unsigned py,tt::RasterError expected=tt::RasterError::invalid_input){assert(std::get<tt::RasterError>(tt::rasterize(outline,instance,px,py))==expected);};
 invalid(nullptr,f,0,0);invalid(o,nullptr,0,0);invalid(o,f,64,0);invalid(o,f,0,64);invalid(o,font(o,8193),0,0,tt::RasterError::unsupported);invalid(o,font(o,16*64,0),0,0,tt::RasterError::unsupported);invalid(o,font(o,16*64,1,1),0,0,tt::RasterError::unsupported);
 auto other_source=std::make_shared<const FontSource>(o->source()->bytes);invalid(o,std::make_shared<const FontInstance>(other_source,0,16*64,1),0,0);
 // Nonrefundable aggregate debit and exact reservation threshold.
 tt::RasterWork work;auto saved=render(o,16*64,31,63,&work);const auto charge=work.reserved;tt::AggregateBudget insufficient{charge-1};assert(std::get<tt::RasterError>(tt::rasterize(o,f,31,63,nullptr,nullptr,&insufficient))==tt::RasterError::budget_exceeded);assert(insufficient.remaining==charge-1);tt::AggregateBudget exact{charge};assert(std::holds_alternative<tt::RasterOutput>(tt::rasterize(o,f,31,63,nullptr,nullptr,&exact))&&exact.remaining==0);
 tt::RasterWork cadence;Cancel count(std::numeric_limits<unsigned>::max(),&cadence);assert(std::holds_alternative<tt::RasterOutput>(tt::rasterize(o,f,31,63,&count,&cadence)));assert(cadence.units-count.previous<=64);
 for(unsigned i=1;i<=count.calls;++i){Cancel c(i);auto r=tt::rasterize(o,f,31,63,&c);assert(std::get<tt::RasterError>(r)==tt::RasterError::cancelled);assert(saved.mask->bytes==render(o,16*64,31,63).mask->bytes);}
 unsigned oom=0;bool success=false;for(long i=0;i<200;++i){tt::AggregateBudget budget{charge};tt::RasterWork progress;fail_after=i;auto r=tt::rasterize(o,f,31,63,nullptr,&progress,&budget);fail_after=-1;if(std::holds_alternative<tt::RasterOutput>(r)){success=true;break;}assert(std::get<tt::RasterError>(r)==tt::RasterError::out_of_memory);assert(budget.remaining==charge-progress.aggregate_charged);++oom;}assert(success&&oom>0);
 // Device-space bounds at exact 2048px width and 65536px bearing.
 const std::vector<P> wide{{0,0,true},{256,0,true},{256,1,true},{0,1,true}};
 auto wide_o=outline(face(sfnt({glyph({wide})},false,1,{0},16)),0);assert(render(wide_o,8192).mask->width==2048);
 tt::RasterWork rejected;assert(std::get<tt::RasterError>(tt::rasterize(wide_o,font(wide_o,8192),1,0,nullptr,&rejected))==tt::RasterError::budget_exceeded&&rejected.output_operations==0);
 const auto tiny=glyph({{{0,0,true},{1,0,true},{0,1,true}}});
 auto exact_bearing=outline(face(sfnt({tiny},false,1,{8192},16)),0);assert(render(exact_bearing,8192).mask->bearing_left==65536);
 auto excess_bearing=outline(face(sfnt({tiny},false,1,{8193},16)),0);assert(std::get<tt::RasterError>(tt::rasterize(excess_bearing,font(excess_bearing,8192),0,0))==tt::RasterError::budget_exceeded);
 auto huge=outline(face(sfnt({glyph({{{0,0,true},{256,0,true},{256,256,true},{0,256,true}}})},false,1,{0},16)),0);assert(std::get<tt::RasterError>(tt::rasterize(huge,font(huge,8192),0,0,nullptr,&rejected))==tt::RasterError::budget_exceeded&&rejected.output_operations==0);
 auto space=outline(face(sfnt({{}})),0);auto empty_space=render(space,16*64,63,63);assert(empty_space.mask->bytes.empty()&&empty_space.mask->width==0&&empty_space.mask->height==0&&empty_space.advance_px==31.25);
 // Q1bR retains legacy128 case as explicit rejection; new adjacent26/27 threshold.
 auto legacy128=make(std::vector<std::vector<P>>(128,curve));
 assert(std::get<tt::RasterError>(tt::rasterize(legacy128,font(legacy128,8192),0,0))==tt::RasterError::budget_exceeded);
 auto near=make(std::vector<std::vector<P>>(26,curve)),above=make(std::vector<std::vector<P>>(27,curve));
 constexpr std::uint64_t near_cost=64ULL*96*52*(96+8*(6+1))+12ULL*96*96+80*96+16*96+8*(52+78)+256;
 static_assert(near_cost<tt::max_raster_work);
 assert(std::get<tt::RasterError>(tt::rasterize(above,font(above,8192),0,0))==tt::RasterError::budget_exceeded);
 struct Deadline:tt::Cancellation{using Clock=std::chrono::steady_clock;Clock::time_point start=Clock::now(),last=start;double max_gap_ms=0;bool cancelled()noexcept override{auto now=Clock::now();max_gap_ms=std::max(max_gap_ms,std::chrono::duration<double,std::milli>(now-last).count());last=now;return now-start>=std::chrono::milliseconds(20);}} deadline;
 tt::RasterWork near_work;auto timed=tt::rasterize(near,font(near,8192),0,0,&deadline,&near_work);const double cancel_ms=std::chrono::duration<double,std::milli>(Deadline::Clock::now()-deadline.start).count();assert(std::get<tt::RasterError>(timed)==tt::RasterError::cancelled&&near_work.reserved==near_cost&&cancel_ms<=250&&deadline.max_gap_ms<=250);
 std::cout<<"Q1b oracle_cases="<<cases<<" max_error="<<double(worst)<<" convergence="<<double(convergence)<<" near_cost="<<near_cost<<" cancel_ms="<<cancel_ms<<" cancel="<<count.calls<<" OOM="<<oom<<" PASS\n";
}
