#pragma once
#include "owned_budget.h"
#include <algorithm>
#include <cstring>

namespace aether::rendering::html_ltr {
// Private candidate/confirmed storage for the new API. Not FrameStore or the
// legacy surface path. Caller publishes state/version separately at commit.
class InteractionFrame final {
 friend struct InteractionFrameTestAccess;
 OwnedBuffer<std::uint8_t> pixels_;
 std::int32_t width_{},height_{};
 std::uint64_t version_{},sequence_{};
public:
 InteractionFrame()noexcept=default;
 InteractionFrame(const InteractionFrame&)=delete;
 InteractionFrame& operator=(const InteractionFrame&)=delete;
 InteractionFrame(InteractionFrame&&)=default;
 InteractionFrame& operator=(InteractionFrame&&)=default;
 static std::size_t byte_count(std::int32_t width,std::int32_t height){
  if(width<0||height<0||width>8192||height>8192)throw Error{AETHER_ERROR_INVALID_ARGUMENT};
  const auto n=OwnedBudget::multiply(OwnedBudget::multiply(std::size_t(width),std::size_t(height)),4);
  if(n>64u*1024u*1024u)throw Error{AETHER_ERROR_INVALID_ARGUMENT};
  return n;
 }
 void prepare(OwnedBudget& budget,std::int32_t width,std::int32_t height,
              std::uint64_t version,std::uint64_t sequence,OwnedBuffer<std::uint8_t>::AllocationHook hook=nullptr){
  const auto n=byte_count(width,height);
  if(!n)throw Error{AETHER_ERROR_SUSPENDED};
  InteractionFrame candidate;
  candidate.pixels_.allocate(budget,OwnedCategory::frame,n,hook);
  candidate.width_=width;candidate.height_=height;candidate.version_=version;candidate.sequence_=sequence;
  swap(candidate);
 }
 void swap(InteractionFrame& other)noexcept{
  pixels_.swap(other.pixels_);std::swap(width_,other.width_);std::swap(height_,other.height_);std::swap(version_,other.version_);std::swap(sequence_,other.sequence_);
 }
 std::uint8_t* data()noexcept{return pixels_.data();}
 const std::uint8_t* data()const noexcept{return pixels_.data();}
 std::size_t bytes()const noexcept{return pixels_.capacity();}
 std::int32_t width()const noexcept{return width_;}
 std::int32_t height()const noexcept{return height_;}
 std::uint64_t version()const noexcept{return version_;}
 std::uint64_t sequence()const noexcept{return sequence_;}
};
}
