#pragma once
#include "interaction_preparation.h"
#include "interaction_frame.h"
#include "../software/render_kernel.h"
namespace aether::rendering::html_ltr {
namespace owned_software_detail {
namespace sw=software;
class Store final {
 OwnedBudget& budget_;
 InteractionFrame& confirmed_;
 InteractionFrame candidate_;
 Viewport viewport_;
 std::uint64_t version_,sequence_;
 bool active_{};
public:
 struct Token{bool valid{};};
 Store(OwnedBudget& b,InteractionFrame& f,Viewport v,std::uint64_t version,std::uint64_t sequence):budget_(b),confirmed_(f),viewport_(v),version_(version),sequence_(sequence){}
 FrameStatus current_viewport(Viewport& out)const noexcept {
  if(viewport_.width<0||viewport_.height<0||viewport_.width>8192||viewport_.height>8192||std::uint64_t(viewport_.width)*std::uint64_t(viewport_.height)*4>64u*1024u*1024u)return FrameStatus::invalid_viewport;
  out=viewport_;return FrameStatus::ok;
 }
 FrameStatus prepare(Token& token){
  candidate_.prepare(budget_,viewport_.width,viewport_.height,version_,sequence_);
  active_=true;token.valid=true;return FrameStatus::ok;
 }
 FrameStatus candidate(Token token,std::span<std::uint8_t>& pixels)noexcept {
  if(!active_||!token.valid)return FrameStatus::stale_token;
  pixels={candidate_.data(),candidate_.bytes()};return FrameStatus::ok;
 }
 FrameStatus discard(Token)noexcept {candidate_=InteractionFrame{};active_=false;return FrameStatus::ok;}
 FrameStatus commit(Token token)noexcept {
  if(!active_||!token.valid)return FrameStatus::stale_token;
  confirmed_.swap(candidate_);active_=false;return FrameStatus::ok;
 }
};
struct Storage {
 using Store=owned_software_detail::Store;using PreparedFrame=InteractionPreparedFrame;using Mask=owned_text::Mask;using Font=owned_text::Font;using Text=InteractionText;using Gradient=InteractionGradient;
 OwnedBudget::Handle domain;
 template<class T> auto vector()const{return OwnedVector<T>(OwnedAllocator<T>(domain,OwnedCategory::temporary));}
 template<class K,class V> auto map()const{return std::map<K,V,std::less<K>,OwnedAllocator<std::pair<const K,V>>>(std::less<K>{},OwnedAllocator<std::pair<const K,V>>(domain,OwnedCategory::temporary));}
 template<class K> auto set()const{return std::set<K,std::less<K>,OwnedAllocator<K>>(std::less<K>{},OwnedAllocator<K>(domain,OwnedCategory::temporary));}
 auto layers(std::size_t n)const {
  auto result=vector<OwnedVector<std::uint8_t>>();result.reserve(n);
  for(std::size_t i=0;i<n;++i)result.emplace_back(OwnedAllocator<std::uint8_t>(domain,OwnedCategory::temporary));
  return result;
 }
 bool valid_mask(const Mask& m)const noexcept {
  return m.font&&m.font->source&&domain.same_domain(m.font->source->domain())&&domain.same_domain(m.bytes.get_allocator().domain())&&m.flags==m.font->raster_flags&&m.phase_x<=63&&m.phase_y<=63&&m.width<=8192&&m.height<=8192&&
   std::abs(std::int64_t(m.bearing_left))<=65536&&std::abs(std::int64_t(m.bearing_top))<=65536&&
   (m.width==0||m.height==0?(m.width==0&&m.height==0&&m.stride==0&&m.bytes.empty()&&m.bearing_left==0&&m.bearing_top==0):
    (m.stride>=m.width&&std::uint64_t(m.stride)*m.height<=text::max_mask_bytes&&m.bytes.size()==std::uint64_t(m.stride)*m.height));
 }
 // Keep the established renderer admission/work estimate; actual ownership is
 // accounted separately by the allocator, including control blocks and capacity.
 std::size_t mask_metadata()const noexcept{return sizeof(text::GlyphMaskA8)+256;}
 std::size_t font_bytes(const Font& f)const noexcept{return f.source->bytes.size();}
 sw::Result unexpected(std::size_t index,std::uint64_t work)const noexcept {
  try{throw;}catch(const Error& e){return {e.status==AETHER_ERROR_LIMIT_EXCEEDED?sw::Status::limit_exceeded:sw::Status::numeric_error,FrameStatus::ok,index,work};}catch(...){return {sw::Status::numeric_error,FrameStatus::ok,index,work};}
 }
};
// G0-M1 bridge: legacy prepared text/masks are borrowed retained inputs, not
// silently converted or included in this allocator's owned-byte accounting.
// New render scratch (including shared-ref map/draw nodes) and candidate/old
// frame storage remain charged to the same interaction domain.
struct LegacyAccountedStorage:Storage {
 using PreparedFrame=preparation::PreparedFrame;using Mask=text::GlyphMaskA8;using Font=text::FontInstance;using Text=preparation::Text;using Gradient=preparation::LinearGradientFill;
 explicit LegacyAccountedStorage(OwnedBudget::Handle d):Storage{std::move(d)}{}
 bool valid_mask(const Mask&)const noexcept{return true;} // validated immutable legacy construction
 std::size_t font_bytes(const Font& f)const noexcept{return f.byte_view().size();}
};
}
// Metadata is supplied by the session only after its version/exhaustion checks.
// All rendering uses the same kernel as the legacy renderer, with private owned
// transport/scratch and a transactional frame adapter; no legacy payload copies.
inline software::Result render_owned_a8(OwnedBudget& budget,InteractionFrame& confirmed,Viewport viewport,std::uint64_t version,std::uint64_t sequence,const InteractionPreparedFrame& prepared,std::span<const std::shared_ptr<const owned_text::Mask>> masks,preparation::Cancellation* cancel=nullptr)noexcept{
 if(!budget.handle().same_domain(prepared.domain()))return {software::Status::unsupported};
 owned_software_detail::Store store(budget,confirmed,viewport,version,sequence);
 return software::render_kernel::render_impl(owned_software_detail::Storage{budget.handle()},store,prepared,&masks,cancel);
}
// Authorized G0-M1 bridge. The legacy PreparedFrame, ShapedRun, source/font and
// mask payloads are explicitly excluded derived storage. This function counts
// its own scratch/copies of references and both BGRA buffers, not their inputs.
inline software::Result render_legacy_accounted(OwnedBudget& budget,InteractionFrame& confirmed,Viewport viewport,std::uint64_t version,std::uint64_t sequence,const preparation::PreparedFrame& prepared,std::span<const std::shared_ptr<const text::GlyphMaskA8>> masks,preparation::Cancellation* cancel=nullptr)noexcept{
 owned_software_detail::Store store(budget,confirmed,viewport,version,sequence);
 return software::render_kernel::render_impl(owned_software_detail::LegacyAccountedStorage{budget.handle()},store,prepared,&masks,cancel);
}
// R3.1 only. Legacy derived products remain explicitly excluded under M1.
inline software::Result render_interaction_fractional_rect_accounted(OwnedBudget& budget,InteractionFrame& confirmed,Viewport viewport,std::uint64_t version,std::uint64_t sequence,const preparation::PreparedFrame& prepared,std::span<const std::shared_ptr<const text::GlyphMaskA8>> masks,preparation::Cancellation* cancel=nullptr)noexcept{
 owned_software_detail::Store store(budget,confirmed,viewport,version,sequence);
 return software::render_kernel::render_impl<software::render_kernel::FractionalRectA8Clip>(owned_software_detail::LegacyAccountedStorage{budget.handle()},store,prepared,&masks,cancel);
}

}
