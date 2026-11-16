#include "truetype_outline.h"
#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <span>
namespace aether::rendering::text::truetype {
namespace {
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
constexpr std::uint32_t tag(char a,char b,char c,char d){return(std::uint32_t(a)<<24)|(std::uint32_t(b)<<16)|(std::uint32_t(c)<<8)|std::uint32_t(d);}
bool denied(std::uint32_t t){
 constexpr std::array tags{tag('C','F','F',' '),tag('C','F','F','2'),tag('f','v','a','r'),tag('g','v','a','r'),tag('a','v','a','r'),tag('H','V','A','R'),tag('V','V','A','R'),tag('M','V','A','R'),tag('C','O','L','R'),tag('C','P','A','L'),tag('S','V','G',' '),tag('C','B','D','T'),tag('C','B','L','C'),tag('s','b','i','x'),tag('E','B','D','T'),tag('E','B','L','C')};
 return std::find(tags.begin(),tags.end(),t)!=tags.end();
}
Point midpoint(Point a,Point b){return{(a.x+b.x)*.5,(a.y+b.y)*.5};}
}
FaceResult ValidatedFace::create(std::shared_ptr<const FontSource> source,std::uint32_t face_index,Cancellation* cancel,Work* diagnostic) noexcept {
 Work local{};Work& work=diagnostic?*diagnostic:local;work={};Run run{cancel,work};
 try {
  run.check();if(!source)throw Error::malformed;
  if(source->bytes.size()>max_font_bytes)throw Error::limit_exceeded;
  if(face_index!=0)throw Error::unsupported;
  Reader r{source->bytes,run};if(r.u32(0)!=0x10000)throw Error::unsupported;
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
  run.check();auto owned=std::unique_ptr<ValidatedFace>(new ValidatedFace(std::move(source)));
  run.check();auto f=std::shared_ptr<ValidatedFace>(std::move(owned));
  for(std::size_t i=0;i<count;++i){run.tick();if(entries[i].tag==tag('c','m','a','p')){f->cmap_=entries[i].table;f->has_cmap_=true;}}
  f->units_=units;f->glyphs_=glyphs;f->horizontal_metrics_=metrics;f->glyf_=glyf;f->hmtx_=hmtx;
  run.check();f->locations_.reserve(std::size_t(glyphs)+1);
  for(std::size_t i=0;i<=glyphs;++i){const auto offset=format==0?std::uint32_t(lo.u16(i*2))*2:lo.u32(i*4);
   if(offset>glyf.length||(i&&offset<f->locations_.back()))throw Error::malformed;
   f->locations_.push_back(offset);
  }
  run.check();return std::shared_ptr<const ValidatedFace>(std::move(f));
 }catch(Error e){return e;}catch(const std::bad_alloc&){return Error::out_of_memory;}catch(...){return Error::malformed;}
}
OutlineResult ValidatedFace::outline(std::uint32_t glyph_id,Cancellation* cancel,Work* diagnostic)const noexcept {
 Work local{};Work& work=diagnostic?*diagnostic:local;work={};Run run{cancel,work,0,true};
 try{
  std::array<std::uint32_t,17> path{};
  auto decode=[&](auto&& self,std::uint32_t glyph_id,std::size_t depth)->std::unique_ptr<Outline>{
  run.check();if(depth>16)throw Error::limit_exceeded;
  work.max_component_depth=std::max(work.max_component_depth,depth);
  for(std::size_t i=0;i<depth;++i){run.tick();if(path[i]==glyph_id)throw Error::malformed;}path[depth]=glyph_id;
  if(glyph_id>=glyphs_)throw Error::malformed;
  Reader r{source_->bytes,run};auto hm=r.sub(hmtx_.offset,hmtx_.length);
  const std::size_t metric=std::min<std::size_t>(glyph_id,horizontal_metrics_-1);
  Metrics metrics{hm.u16(metric*4),std::int16_t(hm.s16(glyph_id<horizontal_metrics_?glyph_id*4+2:std::size_t(horizontal_metrics_)*4+(glyph_id-horizontal_metrics_)*2)),0};
  const auto offset=locations_[glyph_id],length=locations_[glyph_id+1]-offset;
  auto g=r.sub(glyf_.offset+offset,length);std::int32_t count=0;Bounds bounds{};
  if(length){g.range(0,10);count=g.s16(0);if(count>0&&std::size_t(count)>max_contours)throw Error::limit_exceeded;
   bounds={std::int16_t(g.s16(2)),std::int16_t(g.s16(4)),std::int16_t(g.s16(6)),std::int16_t(g.s16(8))};
   if(count>0&&(bounds.x_min>bounds.x_max||bounds.y_min>bounds.y_max))throw Error::malformed;
  }
  if(count<0){
   run.check();auto out=std::unique_ptr<Outline>(new Outline(source_,units_,std::uint16_t(glyph_id)));
   std::size_t cursor=10;bool more=true,instructions=false,unsupported=false,has_bounds=false,has_provider=false;
   Bounds actual{},provider_bounds{};Metrics provider_metrics{};
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
    if(id>=glyphs_)throw Error::malformed;
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
     has_provider=true;provider_bounds=child->bounds;provider_metrics=child->metrics;
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
    if(has_provider&&(provider_metrics.advance!=metrics.advance||provider_metrics.lsb!=metrics.lsb||provider_metrics.pp1!=metrics.pp1||provider_bounds.x_min!=actual.x_min))throw Error::unsupported;
   }else if(metrics.lsb)throw Error::unsupported;
   out->bounds=actual;out->metrics=metrics;run.check();return out;
  }
  if(!count){
   if(length>10){const auto instructions=g.u16(10);g.range(12,instructions);}
   if(metrics.lsb!=0)throw Error::unsupported;
   run.check();auto out=std::unique_ptr<Outline>(new Outline(source_,units_,std::uint16_t(glyph_id)));out->metrics=metrics;
   run.check();return out;
  }
  std::array<std::uint16_t,max_contours> ends{};std::size_t cursor=10;
  for(std::size_t i=0;i<std::size_t(count);++i){ends[i]=g.u16(cursor);cursor+=2;if(i&&ends[i]<=ends[i-1])throw Error::malformed;}
  const std::size_t points=std::size_t(ends[count-1])+1;if(points>max_points-work.points_decoded||std::size_t(count)>max_contours-work.contours_retained)throw Error::limit_exceeded;
  const auto instructions=g.u16(cursor);cursor+=2;g.range(cursor,instructions);cursor+=instructions;
  run.check();std::vector<std::uint8_t> flags;flags.reserve(points);
  while(flags.size()<points){const auto flag=g.u8(cursor++);if(flag&0x80)throw Error::malformed;std::size_t repeat=1;
   if(flag&8)repeat+=g.u8(cursor++);
   if(repeat>points-flags.size())throw Error::malformed;
   for(std::size_t i=0;i<repeat;++i){run.tick();flags.push_back(flag);}
  }
  run.check();std::vector<Point> decoded;decoded.reserve(points);
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
  run.check();auto out=std::unique_ptr<Outline>(new Outline(source_,units_,std::uint16_t(glyph_id)));out->metrics=metrics;out->bounds=actual;
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
  auto out=decode(decode,glyph_id,0);run.check();return std::shared_ptr<const Outline>(std::move(out));
 }catch(Error e){return e;}catch(const std::bad_alloc&){return Error::out_of_memory;}catch(...){return Error::malformed;}
}
}
