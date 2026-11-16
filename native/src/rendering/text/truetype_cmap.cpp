#include "truetype_cmap.h"
#include <array>
#include <span>
#include <new>
namespace aether::rendering::text::truetype {
namespace {
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
int rank(std::uint16_t p,std::uint16_t e){if(p==3&&e==10)return 0;if(p==0&&e==4)return 1;if(p==3&&e==1)return 2;if(p==0&&e==3)return 3;return 4;}
}
CmapResult UnicodeMap::create(std::shared_ptr<const ValidatedFace> face,Cancellation* cancel,CmapWork* diagnostic)noexcept {
 CmapWork local{};auto& w=diagnostic?*diagnostic:local;w={};Run run{cancel,w};
 try {
  run.check();if(!face)throw CmapError::invalid_input;if(!face->has_cmap_)throw CmapError::unsupported;
  const auto table=face->cmap_;if(table.length>max_cmap_bytes)throw CmapError::limit_exceeded;
  Reader r{std::span<const std::uint8_t>(face->source()->bytes).subspan(table.offset,table.length),run};
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
  auto result=std::shared_ptr<UnicodeMap>(new UnicodeMap(std::move(face)));result->format_=v.format;result->offset_=table.offset+v.offset;
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
  run.check();return std::shared_ptr<const UnicodeMap>(std::move(result));
 }catch(CmapError e){return e;}catch(const std::bad_alloc&){return CmapError::out_of_memory;}catch(...){return CmapError::malformed;}
}
LookupResult UnicodeMap::lookup(std::uint32_t scalar,CmapWork* diagnostic)const noexcept {
 CmapWork local{};auto& w=diagnostic?*diagnostic:local;w={};
 if(scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff))return CmapError::invalid_input;
 std::size_t lo=0,hi=ranges_.size();while(lo<hi){++w.comparisons;const auto mid=lo+(hi-lo)/2;if(ranges_[mid].last<scalar)lo=mid+1;else hi=mid;}
 if(lo==ranges_.size()||scalar<ranges_[lo].first)return Missing{};
 const auto& r=ranges_[lo];std::uint16_t gid;
 if(format_==12)gid=std::uint16_t(r.glyph+scalar-r.first);
 else if(!r.array)gid=std::uint16_t(scalar+r.delta);
 else{const auto at=offset_+r.array+2*(scalar-r.first);const auto& b=source()->bytes;const auto raw=std::uint16_t((std::uint16_t(b[at])<<8)|b[at+1]);gid=raw?std::uint16_t(raw+r.delta):0;}
 if(!gid)return Missing{};
 return Mapped{gid};
}
}
