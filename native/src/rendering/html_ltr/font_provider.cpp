#include "font_provider.h"
#include "raster_coordinate.h"
#include <cmath>
#include <limits>

namespace aether::rendering::html_ltr {
namespace {
[[noreturn]] void fail(aether_status_t status){throw Error{status};}
void check(tt::Error e){
 switch(e){
 case tt::Error::malformed:fail(AETHER_ERROR_INVALID_ARGUMENT);
 case tt::Error::unsupported:fail(AETHER_ERROR_UNSUPPORTED);
 case tt::Error::limit_exceeded:fail(AETHER_ERROR_LIMIT_EXCEEDED);
 case tt::Error::out_of_memory:fail(AETHER_ERROR_OUT_OF_MEMORY);
 case tt::Error::cancelled:fail(AETHER_ERROR_INTERNAL);
 } fail(AETHER_ERROR_INTERNAL);
}
void check(tt::CmapError e){
 switch(e){
 case tt::CmapError::invalid_input:case tt::CmapError::malformed:fail(AETHER_ERROR_INVALID_ARGUMENT);
 case tt::CmapError::unsupported:fail(AETHER_ERROR_UNSUPPORTED);
 case tt::CmapError::limit_exceeded:fail(AETHER_ERROR_LIMIT_EXCEEDED);
 case tt::CmapError::out_of_memory:fail(AETHER_ERROR_OUT_OF_MEMORY);
 case tt::CmapError::cancelled:fail(AETHER_ERROR_INTERNAL);
 } fail(AETHER_ERROR_INTERNAL);
}
void check(tt::BindingError e){
 switch(e){
 case tt::BindingError::invalid_input:case tt::BindingError::malformed:fail(AETHER_ERROR_INVALID_ARGUMENT);
 case tt::BindingError::unsupported:fail(AETHER_ERROR_UNSUPPORTED);
 case tt::BindingError::limit_exceeded:fail(AETHER_ERROR_LIMIT_EXCEEDED);
 case tt::BindingError::out_of_memory:fail(AETHER_ERROR_OUT_OF_MEMORY);
 case tt::BindingError::cancelled:fail(AETHER_ERROR_INTERNAL);
 } fail(AETHER_ERROR_INTERNAL);
}
// The decoder's public limits bound every active Outline, with <=17 active
// levels. Vector capacity/allocator overhead/RSS are not logical payload.
constexpr std::size_t outline_retained_max=sizeof(tt::Outline)+tt::max_segments*sizeof(tt::Segment)+tt::max_contours*sizeof(tt::Contour);
constexpr std::size_t outline_temporary_max=17*outline_retained_max;
// face loca <=65536*4; cmap <=65536 ranges (24 bytes in F1); table/owner overhead.
constexpr std::size_t binding_temporary_max=2u*1024u*1024u;
constexpr std::size_t map_entry_overhead=64;
std::uint32_t scalar(std::string_view source,std::size_t& at,std::uint32_t profile){
 if(at>=source.size())fail(AETHER_ERROR_INTERNAL);
 const auto first=static_cast<unsigned char>(source[at++]);std::uint32_t cp=first;
 if(profile==2&&first==0xe2&&source.size()-at>=2&&static_cast<unsigned char>(source[at])==0x80&&static_cast<unsigned char>(source[at+1])==0xa2){at+=2;return 0x2022;}
 if(first>=128){
  if(first<0xc2||first>0xc3||at==source.size())fail(AETHER_ERROR_INVALID_ARGUMENT);
  const auto next=static_cast<unsigned char>(source[at++]);if((next&0xc0)!=0x80)fail(AETHER_ERROR_INVALID_ARGUMENT);
  cp=((first&31u)<<6)|(next&63u);
 }
 if(!((cp>=32&&cp<=126)||(cp>=160&&cp<=255&&cp!=173)))fail(AETHER_ERROR_UNSUPPORTED);
 return cp;
}
}
void FontProvider::reserve(std::size_t n,std::size_t& used,std::size_t limit){
 if(used>limit||n>limit-used)fail(AETHER_ERROR_LIMIT_EXCEEDED);
 used+=n;
}
void FontProvider::reserve_payload(std::size_t n){reserve(n,usage_.payload,payload_limit);}
void FontProvider::validate_source(const std::shared_ptr<const text::FontSource>& source){
 reserve_payload(binding_temporary_max);
 auto face_result=tt::ValidatedFace::create(source);if(auto e=std::get_if<tt::Error>(&face_result))check(*e);
 auto face=std::get<std::shared_ptr<const tt::ValidatedFace>>(std::move(face_result));
 auto map_result=tt::UnicodeMap::create(face);if(auto e=std::get_if<tt::CmapError>(&map_result))check(*e);
 // Only successful temporary validation releases its reservation. A failing
 // operation never refunds; callers abort the whole one-shot session.
 map_result=tt::CmapError::cancelled;face.reset();usage_.payload-=binding_temporary_max;
}
FontProvider::FontProvider(std::vector<std::uint8_t> regular,std::vector<std::uint8_t> bold,std::uint32_t profile):profile_(profile){
 if(profile_!=1&&profile_!=2)fail(AETHER_ERROR_INVALID_ARGUMENT);
 if(regular.empty())fail(AETHER_ERROR_INVALID_ARGUMENT);
 if(regular.size()>16u*1024u*1024u||bold.size()>16u*1024u*1024u-regular.size())fail(AETHER_ERROR_LIMIT_EXCEEDED);
 reserve_payload(regular.size()+sizeof(text::FontSource)+128);
 regular_=std::make_shared<const text::FontSource>(std::move(regular),"HTML LTR regular");validate_source(regular_);
 if(!bold.empty()){reserve_payload(bold.size()+sizeof(text::FontSource)+128);bold_=std::make_shared<const text::FontSource>(std::move(bold),"HTML LTR bold");validate_source(bold_);}
}
std::shared_ptr<const tt::RealFontBinding> FontProvider::binding(int size,int weight){
 if(size<8||size>48||(weight!=400&&weight!=700))fail(AETHER_ERROR_INTERNAL);
 const auto source=weight==700?bold_:regular_;if(!source)fail(AETHER_ERROR_UNSUPPORTED);
 const auto key=std::pair{size,weight};if(auto it=bindings_.find(key);it!=bindings_.end())return it->second;
 reserve(1,usage_.instances,16);reserve_payload(binding_temporary_max+map_entry_overhead);
 auto result=tt::RealFontBinding::create(source,size*64);if(auto e=std::get_if<tt::BindingError>(&result))check(*e);
 auto b=std::get<std::shared_ptr<const tt::RealFontBinding>>(std::move(result));
 const auto retained=b->face()->logical_payload_bytes()+b->map()->logical_payload_bytes()+sizeof(text::FontInstance)+sizeof(tt::RealFontBinding);
 if(retained>binding_temporary_max)fail(AETHER_ERROR_INTERNAL);
 bindings_.emplace(key,b);usage_.payload-=binding_temporary_max-retained;return b;
}
std::shared_ptr<const tt::Outline> FontProvider::outline(const std::shared_ptr<const tt::RealFontBinding>& b,std::uint32_t glyph){
 if(!b||(b->source()!=regular_&&b->source()!=bold_))fail(AETHER_ERROR_INTERNAL);
 bool owned=false;for(const auto& [_,candidate]:bindings_)if(candidate==b){owned=true;break;}if(!owned)fail(AETHER_ERROR_INTERNAL);
 const auto key=std::pair{b->source().get(),glyph};if(auto it=outlines_.find(key);it!=outlines_.end())return it->second;
 reserve(1,usage_.outlines,256);
 constexpr std::uint64_t work=1000000;
 if(work>outline_work_limit-usage_.outline_work_reserved)fail(AETHER_ERROR_LIMIT_EXCEEDED);
 usage_.outline_work_reserved+=work;
 reserve_payload(outline_temporary_max+map_entry_overhead);
 tt::Work observed;auto result=b->face()->outline(glyph,nullptr,&observed);if(observed.units>work)fail(AETHER_ERROR_INTERNAL);
 if(auto e=std::get_if<tt::Error>(&result))check(*e);
 auto ol=std::get<std::shared_ptr<const tt::Outline>>(std::move(result));
 const auto retained=sizeof(tt::Outline)+ol->segments.size()*sizeof(tt::Segment)+ol->contours.size()*sizeof(tt::Contour);
 if(retained>outline_retained_max)fail(AETHER_ERROR_INTERNAL);
 outlines_.emplace(key,ol);usage_.payload-=outline_temporary_max-retained;return ol;
}
FontProduct FontProvider::product(std::string_view source,int size,int weight,int line_height){
 reserve(1,usage_.requests,32768);
 if(source.size()>text::max_text_bytes)fail(AETHER_ERROR_LIMIT_EXCEEDED);
 if(line_height<8||line_height>96)fail(AETHER_ERROR_INTERNAL);
 if(line_height<size)fail(AETHER_ERROR_UNSUPPORTED);
 // Count both physical decoding passes, reserving BEFORE each visit.
 std::size_t n=0;for(std::size_t at=0;at<source.size();){reserve(1,usage_.scalar_visits,65536);scalar(source,at,profile_);++n;}
 if(n>text::max_glyphs)fail(AETHER_ERROR_LIMIT_EXCEEDED);
 // n is bounded by max_glyphs before products and reservations.
 reserve_payload(sizeof(FontProduct)+source.size()+n*sizeof(text::Glyph)+(n+1)*sizeof(std::uint64_t));
 FontProduct product;product.binding=binding(size,weight);product.source=source;product.glyphs.reserve(n);product.prefixes.reserve(n+1);product.prefixes.push_back(0);
 auto vertical=tt::read_vertical_metrics(product.binding);if(auto e=std::get_if<tt::BindingError>(&vertical))check(*e);
 const auto vm=std::get<tt::FontVerticalMetrics>(vertical);if(!vm.units_per_em)fail(AETHER_ERROR_INTERNAL);
 const double scale=double(size)/vm.units_per_em;
 const double baseline=double(vm.ascent)*scale+(double(line_height)-(double(vm.ascent)+vm.descent)*scale)/2;
 if(!std::isfinite(baseline)||baseline<0||baseline>line_height)fail(AETHER_ERROR_UNSUPPORTED);
 std::uint64_t sum=0;
 for(std::size_t at=0;at<source.size();){reserve(1,usage_.scalar_visits,65536);const auto cluster=at;const auto cp=scalar(source,at,profile_);const auto key=std::pair{product.binding->source().get(),cp};std::uint16_t glyph;
  if(auto it=cmap_.find(key);it!=cmap_.end())glyph=it->second;
  else {reserve(1,usage_.cmap_lookups,4352);reserve_payload(sizeof(decltype(cmap_)::value_type)+map_entry_overhead);auto mapped=product.binding->map()->lookup(cp);if(auto e=std::get_if<tt::CmapError>(&mapped))check(*e);if(std::holds_alternative<tt::Missing>(mapped))fail(AETHER_ERROR_UNSUPPORTED);glyph=std::get<tt::Mapped>(mapped).glyph_id;cmap_.emplace(key,glyph);}
  auto ol=outline(product.binding,glyph);const auto delta=std::uint64_t(ol->metrics.advance);sum=checked_design_add(sum,delta);
  const double width=scaled_design_prefix(sum,scale);
  (void)width;
  // Operands are bounded (uint16 hmtx, size<=48, unitsPerEm validated) before
  // multiplication; validate again before round/cast, never abs(int32_min).
  const auto q=quantize_absolute(scaled_design_prefix(delta,scale));
  product.prefixes.push_back(sum);product.glyphs.push_back({glyph,std::uint32_t(cluster),std::int32_t(q),0,0,0});
 }
 const double width=scaled_design_prefix(sum,scale);
 product.metrics=checked_metrics(width,line_height,baseline);
 if(!source.empty())validate_run_transport(source,product.glyphs,width,line_height,baseline);
 return product;
}
}
