#include "real_font_metrics.h"
#include <span>

namespace aether::rendering::text::truetype {
namespace {
struct Reader {
 std::span<const std::uint8_t> b;
 void range(std::size_t at,std::size_t size)const {
  if(at>b.size()||size>b.size()-at)throw BindingError::malformed;
 }
 std::uint16_t u16(std::size_t at)const {
  range(at,2);return (std::uint16_t(b[at])<<8)|b[at+1];
 }
 std::int32_t s16(std::size_t at)const {
  const auto v=u16(at);return v<32768?v:std::int32_t(v)-65536;
 }
 std::uint32_t u32(std::size_t at)const {
  range(at,4);return (std::uint32_t(b[at])<<24)|(std::uint32_t(b[at+1])<<16)|
   (std::uint32_t(b[at+2])<<8)|b[at+3];
 }
 Reader sub(std::size_t at,std::size_t n)const {range(at,n);return {b.subspan(at,n)};}
};
}
FontVerticalMetricsResult read_vertical_metrics(
 const std::shared_ptr<const RealFontBinding>& binding) noexcept {
 try {
  if(!binding)return BindingError::invalid_input;
  Reader r{binding->source()->bytes};
  const auto count=r.u16(4);
  if(!count||count>max_tables)return BindingError::malformed;
  r.range(12,std::size_t(count)*16);
  Reader hhea{},os2{};bool found_hhea=false,found_os2=false;
  for(std::size_t i=0;i<count;++i){
   const auto at=12+i*16;const auto tag=r.u32(at);
   if(tag==0x68686561||tag==0x4f532f32){
    auto table=r.sub(r.u32(at+8),r.u32(at+12));
    if(tag==0x68686561){if(found_hhea)return BindingError::malformed;hhea=table;found_hhea=true;}
    else {if(found_os2)return BindingError::malformed;os2=table;found_os2=true;}
   }
  }
  if(!found_hhea)return BindingError::malformed;
  hhea.range(0,36);if(hhea.u32(0)!=0x00010000)return BindingError::malformed;
  FontVerticalMetrics result{};
  result.units_per_em=binding->face()->units_per_em();
  result.os2_present=found_os2;
  Reader selected=hhea;std::size_t offset=4;
  if(found_os2){
   result.os2_version=os2.u16(0);
   constexpr std::size_t lengths[]{78,86,96,96,96,100};
   if(result.os2_version>5)return BindingError::unsupported;
   // Legacy version 0 may end at usLastCharIndex (68 bytes).
   if(!(result.os2_version==0&&os2.b.size()==68))os2.range(0,lengths[result.os2_version]);
   else os2.range(0,68);
   result.use_typo_metrics_bit=(os2.u16(62)&0x80)!=0;
   // Bit 7 was reserved before version 4. Do not interpret it in older tables.
   if(result.os2_version>=4&&result.use_typo_metrics_bit){
    selected=os2;offset=68;result.policy=VerticalMetricPolicy::os2_typo;
   }
  }
  result.ascent=selected.s16(offset);
  result.descent=-selected.s16(offset+2);
  result.line_gap=selected.s16(offset+4);
  // Opposite-sign metrics are outside this horizontal line-box policy, not a
  // proof the font itself is malformed. Zero extents cannot define a line box.
  if(result.ascent<0||result.descent<0||result.ascent+result.descent==0)
   return BindingError::unsupported;
  return result;
 }catch(BindingError error){return error;}
}
}
