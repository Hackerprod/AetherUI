#pragma once
#ifdef AETHER_HTML_LTR_TESTING
#include "../../html_ltr/session.h"
#endif
#include "../truetype_raster.h"
#include "../raster_active_bound.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
namespace aether::rendering::text::truetype::raster_kernel {
struct Run {Cancellation* cancel;RasterWork& work;unsigned since{};
 void check(){++work.cancel_checks;since=0;if(cancel&&cancel->cancelled())throw RasterError::cancelled;}
 void tick(){++work.units;if(++since==64)check();}
};
struct Edge {SegmentKind kind;Point a,b,c;int winding;};
struct Cross {double x;int winding;};
inline Point lerp(Point a,Point b,double t){return{(1-t)*a.x+t*b.x,(1-t)*a.y+t*b.y};}
inline Point eval(const Edge& e,double t){return lerp(lerp(e.a,e.b,t),lerp(e.b,e.c,t),t);}
inline bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
inline std::uint64_t mul(std::uint64_t a,std::uint64_t b){if(b&&a>std::numeric_limits<std::uint64_t>::max()/b)throw RasterError::budget_exceeded;return a*b;}
inline std::uint64_t add(std::uint64_t a,std::uint64_t b){if(a>std::numeric_limits<std::uint64_t>::max()-b)throw RasterError::budget_exceeded;return a+b;}
template<class Vector> void heap_sort(Vector& v,Run& run){
 auto less=[&](std::size_t a,std::size_t b){run.tick();++run.work.sort_comparisons;return v[a].x<v[b].x;};
 auto swap=[&](std::size_t a,std::size_t b){run.tick();++run.work.sort_swaps;std::swap(v[a],v[b]);};
 auto sift=[&](std::size_t root,std::size_t n){while(root<n/2){run.tick();auto child=2*root+1;if(child+1<n&&less(child,child+1))++child;if(!less(root,child))break;swap(root,child);root=child;}};
 for(std::size_t i=v.size()/2;i>0;--i){run.tick();sift(i-1,v.size());}
 for(std::size_t n=v.size();n>1;--n){run.tick();swap(0,n-1);sift(0,n-1);}
}
template<class Storage> typename Storage::Result rasterize_impl(Storage storage,std::shared_ptr<const typename Storage::Outline> outline,std::shared_ptr<const typename Storage::Font> font,unsigned phase_x,unsigned phase_y,Cancellation* cancel,RasterWork* diagnostic,AggregateBudget* aggregate,AggregateBudget* mask_bytes,bool active_bound=false) noexcept {
 RasterWork local{};RasterWork& work=diagnostic?*diagnostic:local;work={};Run run{cancel,work};
 try {
  run.check();if(!outline||!font||phase_x>63||phase_y>63)throw RasterError::invalid_input;
  if(!storage.identity(*outline,*font))throw RasterError::invalid_input;
  if(font->face_index!=0||storage.face_index(*outline)!=0||font->raster_flags!=q1_unhinted||font->size_26_6<1||font->size_26_6>8192)throw RasterError::unsupported;
  const auto units=outline->units_per_em();if(units<16||units>16384)throw RasterError::invalid_input;
  const double scale=double(font->size_26_6)/(64*double(units));
  const double advance=double(outline->metrics.advance)*scale,advance64=std::round(double(outline->metrics.advance)*font->size_26_6/units);
  if(!std::isfinite(advance)||!std::isfinite(advance64)||advance64<std::numeric_limits<std::int32_t>::min()||advance64>std::numeric_limits<std::int32_t>::max())throw RasterError::numeric_failure;
  if(outline->segments.size()>max_segments)throw RasterError::budget_exceeded;
  run.check();auto edges=storage.template vector<Edge>();edges.reserve(std::min(max_segments,outline->segments.size()*2));
  double minx=std::numeric_limits<double>::infinity(),miny=minx,maxx=-minx,maxy=-minx;
  auto transform=[&](Point p){return Point{(p.x-outline->metrics.pp1)*scale+double(phase_x)/64,-p.y*scale+double(phase_y)/64};};
  auto bound=[&](Point p){if(!finite(p))throw RasterError::numeric_failure;minx=std::min(minx,p.x);maxx=std::max(maxx,p.x);miny=std::min(miny,p.y);maxy=std::max(maxy,p.y);};
  auto piece=[&](SegmentKind kind,Point a,Point b,Point c,int sign){run.tick();
   if(++work.generated_pieces>max_segments)throw RasterError::budget_exceeded;
   if(!finite(a)||!finite(b)||!finite(c))throw RasterError::numeric_failure;
   if(a.y==c.y)return;
   if(a.y>c.y){std::swap(a,c);sign=-sign;}
   if(edges.size()>=max_segments)throw RasterError::budget_exceeded;
   edges.push_back({kind,a,b,c,sign});++work.split_segments;
  };
  for(const auto& segment:outline->segments){run.tick();++work.transformed_segments;
   Point a=transform(segment.from),c=transform(segment.to),b{};bound(a);bound(c);
   if(segment.kind==SegmentKind::quadratic){b=transform(segment.control);bound(b);}else if(segment.kind!=SegmentKind::line)throw RasterError::invalid_input;
   int sign=1;if(a.y>c.y||(a.y==c.y&&a.x>c.x)){std::swap(a,c);sign=-1;}
   if(segment.kind==SegmentKind::line){piece(segment.kind,a,{},c,sign);continue;}
   const double d0=b.y-a.y,d1=c.y-b.y,denom=d0-d1;
   const double t=denom==0?0:d0/denom;if(!std::isfinite(t))throw RasterError::numeric_failure;
   if(t>0&&t<1){const auto ab=lerp(a,b,t),bc=lerp(b,c,t),middle=lerp(ab,bc,t);piece(segment.kind,a,ab,middle,sign);piece(segment.kind,middle,bc,c,sign);}
   else piece(segment.kind,a,b,c,sign);
  }
  std::uint32_t width=0,height=0;std::int32_t left=0,top=0;
  if(!outline->segments.empty()&&minx!=maxx&&miny!=maxy){
   const auto l=std::floor(minx),t=std::floor(miny),w=std::ceil(maxx)-l,h=std::ceil(maxy)-t;
   if(!std::isfinite(w)||!std::isfinite(h)||w<0||h<0)throw RasterError::numeric_failure;
   if(w>2048||h>2048||std::abs(l)>65536||std::abs(t)>65536)throw RasterError::budget_exceeded;
   if(w&&h){width=std::uint32_t(w);height=std::uint32_t(h);left=std::int32_t(l);top=-std::int32_t(t);}
  }
  const std::uint64_t pixels=mul(width,height),s=edges.size(),scans=mul(64,height);
  if(pixels>4*1024*1024)throw RasterError::budget_exceeded;
  unsigned log=0;for(std::uint64_t n=1;n<std::max<std::uint64_t>(s,1);n*=2){run.tick();++log;}
  if(active_bound){
   work.edge_scan_pairs=mul(scans,s);
   const auto k=active_bound_detail::search_bound(scans);
   const auto prepass=add(add(mul(mul(2,s),k),mul(4,s)),32);
   if(prepass>max_raster_work)throw RasterError::budget_exceeded;
   if(aggregate){if(aggregate->remaining<prepass)throw RasterError::budget_exceeded;aggregate->remaining-=prepass;work.aggregate_charged=prepass;}
   work.prepass_reserved=prepass;work.reserved=prepass;
   // Tick sites: init/final=2; per edge begin/accumulate=2; two lower-bound
   // starts=2; at most 2*K comparisons. B=2*s*K+4*s+32 dominates exactly.
   auto tick=[&]{if(work.prepass_work>=prepass)throw RasterError::numeric_failure;++work.prepass_work;run.tick();};
   tick();for(const auto& edge:edges){tick();const auto pairs=active_bound_detail::count(top,scans,edge.a.y,edge.c.y,tick);tick();work.active_pairs=add(work.active_pairs,pairs);}tick();
   if(work.active_pairs>work.edge_scan_pairs)throw RasterError::numeric_failure;
   const auto residual=add(add(add(add(mul(12,pixels),mul(80,height)),mul(16,width)),mul(8,add(work.transformed_segments,work.generated_pieces))),256);
   const auto main=add(add(work.edge_scan_pairs,mul(work.active_pairs,96+8*(log+1))),residual);
   const auto total=add(prepass,main);if(total>max_raster_work)throw RasterError::budget_exceeded;
   if(aggregate){if(aggregate->remaining<main)throw RasterError::budget_exceeded;aggregate->remaining-=main;work.aggregate_charged=total;}
   work.main_reserved=main;work.reserved=total;
  }else{
  work.reserved=add(add(add(add(add(mul(mul(scans,s),96+8*(log+1)),mul(12,pixels)),mul(80,height)),mul(16,width)),mul(8,add(work.transformed_segments,work.generated_pieces))),256);
  if(work.reserved>max_raster_work)throw RasterError::budget_exceeded;
  if(aggregate){if(aggregate->remaining<work.reserved)throw RasterError::budget_exceeded;aggregate->remaining-=work.reserved;work.aggregate_charged=work.reserved;}
  }
  if(mask_bytes){if(pixels>mask_bytes->remaining)throw RasterError::budget_exceeded;mask_bytes->remaining-=pixels;work.mask_charged=pixels;}
  run.check();auto bytes=storage.bytes();
#ifdef AETHER_HTML_LTR_TESTING
  HTML_LTR_STAGE(a8_pixels);
#endif
  bytes.reserve(pixels);
  for(std::uint64_t i=0;i<pixels;++i){run.tick();bytes.push_back(0);++work.output_operations;}
  run.check();auto crosses=storage.template vector<Cross>();crosses.reserve(s);
  run.check();auto partial=storage.template vector<double>();auto delta=storage.template vector<double>();partial.reserve(width);delta.reserve(width+1);
  for(std::uint32_t x=0;x<width;++x){run.tick();partial.push_back(0);++work.output_operations;}
  for(std::uint32_t x=0;x<=width;++x){run.tick();delta.push_back(0);++work.output_operations;}
  const double epsilon=128*std::numeric_limits<double>::epsilon()*64*std::max<std::uint64_t>(s,1);
  for(std::uint32_t y=0;y<height;++y){run.tick();
   for(auto& n:partial){run.tick();n=0;++work.output_operations;}
   for(auto& n:delta){run.tick();n=0;++work.output_operations;}
   for(unsigned suby=0;suby<64;++suby){run.tick();crosses.clear();const double sample_y=-double(top)+y+(suby+.5)/64;
    for(const auto& edge:edges){run.tick();++work.edge_checks;if(sample_y<edge.a.y||sample_y>=edge.c.y)continue;
     double x;
     if(sample_y==edge.a.y)x=edge.a.x;
     else if(sample_y==edge.c.y)x=edge.c.x;
     else if(edge.kind==SegmentKind::line)x=edge.a.x+(sample_y-edge.a.y)/(edge.c.y-edge.a.y)*(edge.c.x-edge.a.x);
     else{double low=0,high=1;for(unsigned k=0;k<32;++k){run.tick();++work.root_steps;const auto t=(low+high)*.5;if(eval(edge,t).y<sample_y)low=t;else high=t;}x=eval(edge,(low+high)*.5).x;}
     if(!std::isfinite(x))throw RasterError::numeric_failure;
     crosses.push_back({x,edge.winding});++work.sweep_operations;
    }
    heap_sort(crosses,run);std::size_t cursor=0;int winding=0;double start=0;
    auto interval=[&](double lo,double hi){run.tick();++work.sweep_operations;
     lo=std::clamp(lo-double(left),0.,double(width));hi=std::clamp(hi-double(left),0.,double(width));
     if(hi<=lo)return;
     const auto first=std::uint32_t(std::floor(lo)),last=std::uint32_t(std::ceil(hi)-1);
     if(first>=width||last>=width||last<first)throw RasterError::numeric_failure;
     auto write=[&](double& slot,double value){run.tick();++work.sweep_operations;slot+=value;};
     if(first==last)write(partial[first],hi-lo);
     else{write(partial[first],first+1-lo);write(partial[last],hi-last);
      if(last>first+1){write(delta[first+1],1);write(delta[last],-1);}}
    };
    while(cursor<crosses.size()){run.tick();++work.sweep_operations;const auto x=crosses[cursor].x;int change=0;
     do{run.tick();change+=crosses[cursor++].winding;++work.sweep_operations;}while(cursor<crosses.size()&&crosses[cursor].x==x);
     const auto before=winding;winding+=change;
     if(!before&&winding)start=x;
     if(before&&!winding)interval(start,x);
    }
    if(winding)throw RasterError::numeric_failure;
   }
   double full=0;
   for(std::uint32_t x=0;x<width;++x){run.tick();full+=delta[x];const double coverage=partial[x]+full;
    if(!std::isfinite(coverage)||coverage < -epsilon||coverage >64+epsilon)throw RasterError::numeric_failure;
    bytes[std::size_t(y)*width+x]=std::uint8_t(std::floor(std::clamp(coverage,0.,64.)*255/64+.5));++work.output_operations;
   }
  }
  const auto expensive_pairs=active_bound?work.active_pairs:scans*s;
  if(work.edge_checks>scans*s||work.root_steps>expensive_pairs*32||work.sort_comparisons>expensive_pairs*4*(log+1)||work.sort_swaps>expensive_pairs*2*(log+1)||work.sweep_operations>expensive_pairs*16||work.sample_tests!=0||work.units>work.reserved)throw RasterError::numeric_failure;
  run.check();auto mask=storage.mask(std::move(font),outline->glyph_id,q1_unhinted,phase_x,phase_y,width,height,width,left,top,std::move(bytes));
  run.check();return typename Storage::Output{std::move(mask),advance,std::int32_t(advance64)};
 }catch(RasterError e){return e;}catch(const std::bad_alloc&){return RasterError::out_of_memory;}catch(...){return storage.unexpected();}
}
}
