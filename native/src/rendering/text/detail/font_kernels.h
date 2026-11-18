#pragma once
#include "../truetype_cmap.h"
#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <new>
namespace aether::rendering::text::truetype::detail {
namespace outline_detail {

struct Run {
 Cancellation* cancel; Work& work; std::size_t since{}; bool bounded{};
 void check(){++work.cancel_checks;since=0;if(cancel&&cancel->cancelled())throw Error::cancelled;}
 void tick(){if(bounded&&work.units>=1000000)throw Error::limit_exceeded;++work.units;if(++since>=256)check();}
};
struct Reader {
 std::span<const std::uint8_t> bytes; Run& run;
 void range(std::size_t at,std::size_t n)const {
  run.tick();if(at>bytes.size()||n>bytes.size()-at)throw Error::malformed;
  run.work.bytes_checked+=n;
 }
 std::uint8_t u8(std::size_t at)const{range(at,1);return bytes[at];}
 std::uint16_t u16(std::size_t at)const{range(at,2);return (std::uint16_t(bytes[at])<<8)|bytes[at+1];}
 std::int32_t s16(std::size_t at)const {auto v=u16(at);return v<32768?v:std::int32_t(v)-65536;}
 std::uint32_t u32(std::size_t at)const{range(at,4);return(std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|(std::uint32_t(bytes[at+2])<<8)|bytes[at+3];}
 Reader sub(std::size_t at,std::size_t n)const{range(at,n);return {bytes.subspan(at,n),run};}
};
inline constexpr std::uint32_t tag(char a,char b,char c,char d){return(std::uint32_t(a)<<24)|(std::uint32_t(b)<<16)|(std::uint32_t(c)<<8)|std::uint32_t(d);}
inline bool denied(std::uint32_t t){
 constexpr std::array tags{tag('C','F','F',' '),tag('C','F','F','2'),tag('f','v','a','r'),tag('g','v','a','r'),tag('a','v','a','r'),tag('H','V','A','R'),tag('V','V','A','R'),tag('M','V','A','R'),tag('C','O','L','R'),tag('C','P','A','L'),tag('S','V','G',' '),tag('C','B','D','T'),tag('C','B','L','C'),tag('s','b','i','x'),tag('E','B','D','T'),tag('E','B','L','C')};
 return std::find(tags.begin(),tags.end(),t)!=tags.end();
}
inline Point midpoint(Point a,Point b){return{(a.x+b.x)*.5,(a.y+b.y)*.5};}
}
namespace cmap_detail {

struct Run {
 Cancellation* cancel; CmapWork& w; std::size_t since{};
 void check(){++w.cancel_checks;since=0;if(cancel&&cancel->cancelled())throw CmapError::cancelled;}
 void tick(){if(w.units>=max_cmap_work)throw CmapError::limit_exceeded;++w.units;if(++since>=64)check();}
};
struct Reader {
 std::span<const std::uint8_t> b; Run& run;
 void bounds(std::size_t at,std::size_t n)const{run.tick();if(at>b.size()||n>b.size()-at)throw CmapError::malformed;}
 std::uint16_t u16(std::size_t a)const{bounds(a,2);return(std::uint16_t(b[a])<<8)|b[a+1];}
 std::uint32_t u32(std::size_t a)const{bounds(a,4);return(std::uint32_t(b[a])<<24)|(std::uint32_t(b[a+1])<<16)|(std::uint32_t(b[a+2])<<8)|b[a+3];}
};
inline int rank(std::uint16_t p,std::uint16_t e){if(p==3&&e==10)return 0;if(p==0&&e==4)return 1;if(p==3&&e==1)return 2;if(p==0&&e==3)return 3;return 4;}
}
struct FontKernel {
 template<class S> static std::variant<std::shared_ptr<const typename S::Face>,Error> face(std::shared_ptr<const typename S::Source> source,uint32_t face_index,Cancellation* cancel,Work* diagnostic) {

 using Table=typename S::Face::Table;
 using namespace outline_detail;
 Work local{};Work& work=diagnostic?*diagnostic:local;work={};Run run{cancel,work};
 try {
  run.check();if(!source)throw Error::malformed;
  if(source->byte_view().size()>max_font_bytes)throw Error::limit_exceeded;
  if(face_index!=0)throw Error::unsupported;
  Reader r{source->byte_view(),run};if(r.u32(0)!=0x10000)throw Error::unsupported;
  r.range(0,12);const auto count=r.u16(4);if(!count)throw Error::malformed;if(count>max_tables)throw Error::limit_exceeded;
  const std::size_t directory_end=12+std::size_t(count)*16;r.range(0,directory_end);
  struct Entry{std::uint32_t tag;Table table;};std::array<Entry,max_tables> entries{};
  bool unsupported=false;
  for(std::size_t i=0;i<count;++i){
   const auto at=12+16*i;auto& e=entries[i];e={r.u32(at),{r.u32(at+8),r.u32(at+12)}};
   r.range(e.table.offset,e.table.length);if(e.table.length&&e.table.offset<directory_end)throw Error::malformed;
   for(std::size_t j=0;j<i;++j){run.tick();const auto& p=entries[j];
    if(e.tag==p.tag)throw Error::malformed;
    if(e.table.length&&p.table.length&&e.table.offset<p.table.offset+p.table.length&&p.table.offset<e.table.offset+e.table.length)throw Error::malformed;
   }
   unsupported|=denied(e.tag);++work.tables_checked;
  }
  if(unsupported)throw Error::unsupported;
  auto table=[&](std::uint32_t name){for(std::size_t i=0;i<count;++i){run.tick();if(entries[i].tag==name)return entries[i].table;}throw Error::malformed;};
  const auto head=table(tag('h','e','a','d')),maxp=table(tag('m','a','x','p')),hhea=table(tag('h','h','e','a')),loca=table(tag('l','o','c','a')),glyf=table(tag('g','l','y','f')),hmtx=table(tag('h','m','t','x'));
  auto h=r.sub(head.offset,head.length),m=r.sub(maxp.offset,maxp.length),hh=r.sub(hhea.offset,hhea.length),lo=r.sub(loca.offset,loca.length);
  h.range(0,54);m.range(0,32);hh.range(0,36);
  if(h.u32(0)!=0x10000||h.u32(12)!=0x5f0f3cf5||m.u32(0)!=0x10000||hh.u32(0)!=0x10000)throw Error::malformed;
  const auto units=h.u16(18),glyphs=m.u16(4),metrics=hh.u16(34);const auto format=h.s16(50);
  if(units<16||units>16384||!glyphs||!metrics||metrics>glyphs||(format!=0&&format!=1))throw Error::malformed;
  if(h.s16(52)!=0||hh.s16(32)!=0)throw Error::unsupported;
  const std::size_t loca_size=(std::size_t(glyphs)+1)*(format==0?2:4);lo.range(0,loca_size);
  if(hmtx.length<std::size_t(metrics)*4+std::size_t(glyphs-metrics)*2)throw Error::malformed;
  run.check();auto owned=S::make_face(std::move(source));
  run.check();auto f=S::share_face(std::move(owned));
  for(std::size_t i=0;i<count;++i){run.tick();if(entries[i].tag==tag('c','m','a','p')){f->cmap_=entries[i].table;f->has_cmap_=true;}}
  f->units_=units;f->glyphs_=glyphs;f->horizontal_metrics_=metrics;f->glyf_=glyf;f->hmtx_=hmtx;
  run.check();f->locations_.reserve(std::size_t(glyphs)+1);
  for(std::size_t i=0;i<=glyphs;++i){const auto offset=format==0?std::uint32_t(lo.u16(i*2))*2:lo.u32(i*4);
   if(offset>glyf.length||(i&&offset<f->locations_.back()))throw Error::malformed;
   f->locations_.push_back(offset);
  }
  run.check();return std::shared_ptr<const typename S::Face>(std::move(f));
 }catch(Error e){return e;}catch(const std::bad_alloc&){return Error::out_of_memory;}catch(...){return S::unexpected_outline();}

 }
 template<class S> static std::variant<std::shared_ptr<const typename S::Outline>,Error> outline(const typename S::Face& face,uint32_t glyph_id,Cancellation* cancel,Work* diagnostic) {

 using namespace outline_detail;
 Work local{};Work& work=diagnostic?*diagnostic:local;work={};Run run{cancel,work,0,true};
 try{
  std::array<std::uint32_t,17> path{};
  auto decode=[&](auto&& self,std::uint32_t glyph_id,std::size_t depth)->typename S::OutlineMutable{
  run.check();if(depth>16)throw Error::limit_exceeded;
  work.max_component_depth=std::max(work.max_component_depth,depth);
  for(std::size_t i=0;i<depth;++i){run.tick();if(path[i]==glyph_id)throw Error::malformed;}path[depth]=glyph_id;
  if(glyph_id>=face.glyphs_)throw Error::malformed;
  Reader r{face.source_->byte_view(),run};auto hm=r.sub(face.hmtx_.offset,face.hmtx_.length);
  const std::size_t metric=std::min<std::size_t>(glyph_id,face.horizontal_metrics_-1);
  Metrics metrics{hm.u16(metric*4),std::int16_t(hm.s16(glyph_id<face.horizontal_metrics_?glyph_id*4+2:std::size_t(face.horizontal_metrics_)*4+(glyph_id-face.horizontal_metrics_)*2)),0};
  const auto offset=face.locations_[glyph_id],length=face.locations_[glyph_id+1]-offset;
  auto g=r.sub(face.glyf_.offset+offset,length);std::int32_t count=0;Bounds bounds{};
  if(length){g.range(0,10);count=g.s16(0);if(count>0&&std::size_t(count)>max_contours)throw Error::limit_exceeded;
   bounds={std::int16_t(g.s16(2)),std::int16_t(g.s16(4)),std::int16_t(g.s16(6)),std::int16_t(g.s16(8))};
   if(count>0&&(bounds.x_min>bounds.x_max||bounds.y_min>bounds.y_max))throw Error::malformed;
  }
  if(count<0){
   run.check();auto out=S::make_outline(face.source_,face.units_,std::uint16_t(glyph_id));
   std::size_t cursor=10;bool more=true,instructions=false,unsupported=false,has_bounds=false,has_provider=false;
   Bounds actual{};Metrics provider_metrics{};
   struct Component {std::uint16_t flags,id;std::int32_t x,y;};
   std::array<Component,256> components{};std::size_t component_count=0;
   // Validate the entire local record sequence before reporting unsupported payloads.
   while(more){
    run.tick();if(component_count==components.size())throw Error::limit_exceeded;
    const auto flags=g.u16(cursor),id=g.u16(cursor+2);cursor+=4;
    if(flags&0xe010)throw Error::malformed;
    const unsigned matrices=!!(flags&8)+!!(flags&64)+!!(flags&128);
    if(matrices>1)throw Error::malformed;
    const bool words=flags&1,xy=flags&2;std::int32_t x=0,y=0;
    if(words){x=g.s16(cursor);y=g.s16(cursor+2);cursor+=4;}
    else {const auto bx=g.u8(cursor),by=g.u8(cursor+1);x=bx<128?bx:int(bx)-256;y=by<128?by:int(by)-256;cursor+=2;}
    if(!xy&&!component_count)throw Error::malformed;
    const std::size_t matrix_bytes=(flags&128)?8:(flags&64)?4:(flags&8)?2:0;
    g.range(cursor,matrix_bytes);cursor+=matrix_bytes;
    if(id>=face.glyphs_)throw Error::malformed;
    unsupported|=!xy||matrices||((flags&0x1800)==0x1800);
    components[component_count++]={flags,id,x,y};more=flags&32;instructions|=flags&256;
   }
   if(instructions){const auto n=g.u16(cursor);cursor+=2;g.range(cursor,n);}
   if(unsupported)throw Error::unsupported;
   // ROUND_XY_TO_GRID is intentionally ignored: this is unhinted design-unit
   // geometry, not pixel placement. No instructions are executed.
   for(std::size_t i=0;i<component_count;++i){
    run.tick();if(work.component_references>=256)throw Error::limit_exceeded;++work.component_references;
    const auto c=components[i];auto child=self(self,c.id,depth+1);
    const bool empty=child->contours.empty();
    if(c.flags&512){
     if(has_provider||empty||c.x||c.y)throw Error::unsupported;
     has_provider=true;provider_metrics=child->metrics;
    }
    if(empty)continue;
    // Each child has validated original-point bounds; translating their union
    // preserves off-curve extrema that segment endpoints alone would lose.
    auto shifted=[](std::int16_t v,std::int32_t d){const std::int64_t n=std::int64_t(v)+d;if(n<-32768||n>32767)throw Error::unsupported;return std::int16_t(n);};
    const Bounds b{shifted(child->bounds.x_min,c.x),shifted(child->bounds.y_min,c.y),shifted(child->bounds.x_max,c.x),shifted(child->bounds.y_max,c.y)};
    if(!has_bounds){actual=b;has_bounds=true;}else{actual.x_min=std::min(actual.x_min,b.x_min);actual.y_min=std::min(actual.y_min,b.y_min);actual.x_max=std::max(actual.x_max,b.x_max);actual.y_max=std::max(actual.y_max,b.y_max);}
    if(child->segments.size()>max_segments-out->segments.size()||child->contours.size()>max_contours-out->contours.size())throw Error::limit_exceeded;
    const auto base=out->segments.size();
    // Precharge moves performed internally by reserve; the allocator itself is
    // not interruptible and is not represented as CPU-time work units.
    if(base+child->segments.size()>out->segments.capacity())for(std::size_t n=0;n<base;++n)run.tick();
    run.check();out->segments.reserve(base+child->segments.size());
    if(out->contours.size()+child->contours.size()>out->contours.capacity())for(std::size_t n=0;n<out->contours.size();++n)run.tick();
    run.check();out->contours.reserve(out->contours.size()+child->contours.size());
    for(auto seg:child->segments){run.tick();seg.from.x+=c.x;seg.from.y+=c.y;seg.to.x+=c.x;seg.to.y+=c.y;if(seg.kind==SegmentKind::quadratic){seg.control.x+=c.x;seg.control.y+=c.y;}out->segments.push_back(seg);}
    for(auto contour:child->contours){run.tick();contour.first_segment+=base;out->contours.push_back(contour);}
   }
   if(has_bounds){
    if(bounds.x_min>bounds.x_max||bounds.y_min>bounds.y_max)throw Error::malformed;
    if(actual.x_min!=bounds.x_min||actual.y_min!=bounds.y_min||actual.x_max!=bounds.x_max||actual.y_max!=bounds.y_max)throw Error::unsupported;
    metrics.pp1=std::int32_t(actual.x_min)-metrics.lsb;
    if(has_provider&&(provider_metrics.advance!=metrics.advance||provider_metrics.pp1!=metrics.pp1))throw Error::unsupported;
   }else if(metrics.lsb)throw Error::unsupported;
   out->bounds=actual;out->metrics=metrics;run.check();return out;
  }
  if(!count){
   if(length>10){const auto instructions=g.u16(10);g.range(12,instructions);}
   if(metrics.lsb!=0)throw Error::unsupported;
   run.check();auto out=S::make_outline(face.source_,face.units_,std::uint16_t(glyph_id));out->metrics=metrics;
   run.check();return out;
  }
  std::array<std::uint16_t,max_contours> ends{};std::size_t cursor=10;
  for(std::size_t i=0;i<std::size_t(count);++i){ends[i]=g.u16(cursor);cursor+=2;if(i&&ends[i]<=ends[i-1])throw Error::malformed;}
  const std::size_t points=std::size_t(ends[count-1])+1;if(points>max_points-work.points_decoded||std::size_t(count)>max_contours-work.contours_retained)throw Error::limit_exceeded;
  const auto instructions=g.u16(cursor);cursor+=2;g.range(cursor,instructions);cursor+=instructions;
  run.check();auto flags=S::template vector<std::uint8_t>(face.source_);flags.reserve(points);
  while(flags.size()<points){const auto flag=g.u8(cursor++);if(flag&0x80)throw Error::malformed;std::size_t repeat=1;
   if(flag&8)repeat+=g.u8(cursor++);
   if(repeat>points-flags.size())throw Error::malformed;
   for(std::size_t i=0;i<repeat;++i){run.tick();flags.push_back(flag);}
  }
  run.check();auto decoded=S::template vector<Point>(face.source_);decoded.reserve(points);
  for(std::size_t i=0;i<points;++i){run.tick();decoded.push_back({});}
  for(unsigned axis=0;axis<2;++axis){std::int32_t coordinate=0;const unsigned short_bit=axis?4:2,same_bit=axis?32:16;
   for(std::size_t i=0;i<points;++i){run.tick();std::int32_t delta=0;if(flags[i]&short_bit){delta=g.u8(cursor++);if(!(flags[i]&same_bit))delta=-delta;}
    else if(!(flags[i]&same_bit)){delta=g.s16(cursor);cursor+=2;}
    // Delta and previous coordinate are each int16-bounded; sum is int32 safe.
    coordinate+=delta;if(coordinate<-32768||coordinate>32767)throw Error::malformed;
    if(axis)decoded[i].y=coordinate;else decoded[i].x=coordinate;
   }
  }
  work.points_decoded+=points;
  Bounds actual{std::int16_t(decoded[0].x),std::int16_t(decoded[0].y),std::int16_t(decoded[0].x),std::int16_t(decoded[0].y)};
  for(const auto& p:decoded){run.tick();actual.x_min=std::min(actual.x_min,std::int16_t(p.x));actual.y_min=std::min(actual.y_min,std::int16_t(p.y));actual.x_max=std::max(actual.x_max,std::int16_t(p.x));actual.y_max=std::max(actual.y_max,std::int16_t(p.y));}
  if(actual.x_min!=bounds.x_min||actual.y_min!=bounds.y_min||actual.x_max!=bounds.x_max||actual.y_max!=bounds.y_max)throw Error::unsupported;
  metrics.pp1=std::int32_t(actual.x_min)-metrics.lsb;
  run.check();auto out=S::make_outline(face.source_,face.units_,std::uint16_t(glyph_id));out->metrics=metrics;out->bounds=actual;
  run.check();out->segments.reserve(std::min(max_segments,points*2));run.check();out->contours.reserve(count);
  auto append=[&](SegmentKind kind,Point from,Point control,Point to){run.tick();if(work.segments_retained>=max_segments)throw Error::limit_exceeded;out->segments.push_back({kind,from,control,to});++work.segments_retained;};
  std::size_t first=0;
  for(std::size_t contour=0;contour<std::size_t(count);++contour){run.tick();const std::size_t last=ends[contour],base=out->segments.size();
   Point start;std::size_t begin,remaining;
   if(flags[first]&1){start=decoded[first];begin=first+1;remaining=last-first;}
   else if(flags[last]&1){start=decoded[last];begin=first;remaining=last-first;}
   else{start=midpoint(decoded[last],decoded[first]);begin=first;remaining=last-first+1;}
   Point current=start;
   while(remaining){run.tick();const auto p=decoded[begin];const bool on=flags[begin]&1;++begin;--remaining;
    if(on){append(SegmentKind::line,current,{},p);current=p;}
    else {Point end=start;if(remaining){if(flags[begin]&1){end=decoded[begin++];--remaining;}else end=midpoint(p,decoded[begin]);}
     append(SegmentKind::quadratic,current,p,end);current=end;
    }
   }
   if(current!=start)append(SegmentKind::line,current,{},start);
   out->contours.push_back({base,out->segments.size()-base});++work.contours_retained;first=last+1;
  }
  run.check();return out;
  };
  auto out=decode(decode,glyph_id,0);run.check();return S::freeze_outline(std::move(out));
 }catch(Error e){return e;}catch(const std::bad_alloc&){return Error::out_of_memory;}catch(...){return S::unexpected_outline();}
 }
 template<class S> static std::variant<std::shared_ptr<const typename S::Map>,CmapError> map(std::shared_ptr<const typename S::Face> face,Cancellation* cancel,CmapWork* diagnostic) {

 using namespace cmap_detail;
 CmapWork local{};auto& w=diagnostic?*diagnostic:local;w={};Run run{cancel,w};
 try {
  run.check();if(!face)throw CmapError::invalid_input;if(!face->has_cmap_)throw CmapError::unsupported;
  const auto table=face->cmap_;if(table.length>max_cmap_bytes)throw CmapError::limit_exceeded;
  Reader r{face->source()->byte_view().subspan(table.offset,table.length),run};
  if(r.u16(0)!=0)throw CmapError::malformed;
   const auto count=r.u16(2);
  if(count>max_cmap_records)throw CmapError::limit_exceeded;
  const std::size_t directory=4+8*std::size_t(count);r.bounds(0,directory);
  struct Record{std::uint16_t p,e,format;std::size_t offset,length;int priority;};
  std::array<Record,max_cmap_records> records{};int selected=-1;
  for(std::size_t i=0;i<count;++i){auto& v=records[i];const auto a=4+8*i;
   v.p=r.u16(a);v.e=r.u16(a+2);v.offset=r.u32(a+4);v.priority=rank(v.p,v.e);
   if(v.offset<directory)throw CmapError::malformed;
   r.bounds(v.offset,2);
   if(v.offset%2)throw CmapError::unsupported;
   v.format=r.u16(v.offset);++w.records;
   if(v.priority==4)continue;
   if(v.format!=(v.priority<2?12:4))throw CmapError::unsupported;
   v.length=v.format==12?r.u32(v.offset+4):r.u16(v.offset+2);r.bounds(v.offset,v.length);
   if(v.length<16)throw CmapError::malformed;
   for(std::size_t j=0;j<i;++j){run.tick();const auto& p=records[j];if(p.priority==4)continue;
    if(p.p==v.p&&p.e==v.e&&p.offset!=v.offset)throw CmapError::unsupported;
    if(p.offset!=v.offset&&p.offset<v.offset+v.length&&v.offset<p.offset+p.length)throw CmapError::unsupported;
   }
   if(selected<0||v.priority<records[std::size_t(selected)].priority)selected=int(i);
  }
  if(selected<0)throw CmapError::unsupported;
  const auto v=records[std::size_t(selected)];Reader s{r.b.subspan(v.offset,v.length),run};
  run.tick();run.check();
  auto result=S::make_map(std::move(face));result->format_=v.format;result->offset_=table.offset+v.offset;
  const auto glyphs=result->face_->glyph_count();
  if(v.format==12){
   if(s.u16(2)!=0||s.u32(8)!=0)throw CmapError::malformed;
   const auto n=s.u32(12);if(n>max_cmap_groups)throw CmapError::limit_exceeded;
   if(v.length!=16+12*std::size_t(n))throw CmapError::malformed;
   run.tick();run.check();result->ranges_.reserve(n);
   for(std::size_t i=0;i<n;++i){const auto a=16+12*i;auto first=s.u32(a),last=s.u32(a+4),gid=s.u32(a+8);
    if(first>last||last>0x10ffff||(i&&first<=result->ranges_.back().last))throw CmapError::malformed;
    // Subtraction-based bound also rejects uint32 startGlyph+span overflow.
    if(gid>=glyphs||last-first>=std::uint32_t(glyphs)-gid)throw CmapError::malformed;
    result->ranges_.push_back({first,last,gid,0,0});++w.ranges;
   }
  }else{
   if(s.u16(4)!=0)throw CmapError::malformed;
   const auto twice=s.u16(6);
   if(!twice||twice%2)throw CmapError::malformed;
   const std::size_t n=twice/2;
   if(n>max_cmap_segments)throw CmapError::limit_exceeded;
   const std::size_t array=16+8*n;s.bounds(0,array);if(s.u16(14+2*n)!=0)throw CmapError::malformed;
   run.tick();run.check();result->ranges_.reserve(n);
   for(std::size_t i=0;i<n;++i){const auto last=s.u16(14+2*i),first=s.u16(16+2*n+2*i),delta=s.u16(16+4*n+2*i);
    const std::size_t word=16+6*n+2*i;const auto ro=s.u16(word);
    if(first>last||(i&&first<=result->ranges_.back().last))throw CmapError::malformed;
    if(i+1==n&&(first!=0xffff||last!=0xffff))throw CmapError::malformed;
    std::size_t target=0;if(ro){if(ro%2)throw CmapError::unsupported;target=word+ro;if(target<array)throw CmapError::malformed;s.bounds(target,2*(std::size_t(last)-first+1));}
    for(std::uint32_t cp=first;cp<=last;++cp){run.tick();++w.codepoints;std::uint16_t gid;
     if(!ro)gid=std::uint16_t(cp+delta);else{const auto raw=s.u16(target+2*(cp-first));gid=raw?std::uint16_t(raw+delta):0;}
     if(gid>=glyphs)throw CmapError::malformed;
    }
    result->ranges_.push_back({first,last,0,delta,target});++w.ranges;
   }
  }
  run.check();return std::shared_ptr<const typename S::Map>(std::move(result));
 }catch(CmapError e){return e;}catch(const std::bad_alloc&){return CmapError::out_of_memory;}catch(...){return S::unexpected_cmap();}

 }
 template<class Map> static LookupResult lookup(const Map& map,uint32_t scalar,CmapWork* diagnostic) noexcept {

 CmapWork local{};auto& w=diagnostic?*diagnostic:local;w={};
 if(scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff))return CmapError::invalid_input;
 std::size_t lo=0,hi=map.ranges_.size();while(lo<hi){++w.comparisons;const auto mid=lo+(hi-lo)/2;if(map.ranges_[mid].last<scalar)lo=mid+1;else hi=mid;}
 if(lo==map.ranges_.size()||scalar<map.ranges_[lo].first)return Missing{};
 const auto& r=map.ranges_[lo];std::uint16_t gid;
 if(map.format_==12)gid=std::uint16_t(r.glyph+scalar-r.first);
 else if(!r.array)gid=std::uint16_t(scalar+r.delta);
 else{const auto at=map.offset_+r.array+2*(scalar-r.first);const auto b=map.source()->byte_view();const auto raw=std::uint16_t((std::uint16_t(b[at])<<8)|b[at+1]);gid=raw?std::uint16_t(raw+r.delta):0;}
 if(!gid)return Missing{};
 return Mapped{gid};
 }
};
struct LegacyFontStorage {
 using Source=FontSource;using Face=ValidatedFace;using Map=UnicodeMap;using Outline=::aether::rendering::text::truetype::Outline;
 using OutlineMutable=std::unique_ptr<Outline>;
 static Error unexpected_outline()noexcept{return Error::malformed;}
 static CmapError unexpected_cmap()noexcept{return CmapError::malformed;}
 static auto make_face(std::shared_ptr<const Source> s){return std::unique_ptr<Face>(new Face(std::move(s)));}
 static auto share_face(std::unique_ptr<Face> p){return std::shared_ptr<Face>(std::move(p));}
 static auto make_map(std::shared_ptr<const Face> f){return std::shared_ptr<Map>(new Map(std::move(f)));}
 static auto make_outline(std::shared_ptr<const Source> s,uint16_t u,uint16_t id){return std::unique_ptr<Outline>(new Outline(std::move(s),u,id));}
 static auto freeze_outline(std::unique_ptr<Outline> p){return std::shared_ptr<const Outline>(std::move(p));}
 template<class T> static auto vector(const std::shared_ptr<const Source>&){return std::vector<T>{};}
};
}
