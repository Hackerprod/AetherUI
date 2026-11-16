#include "document_scroll_requests.h"
#include <aether/rendering/preparation/detail/utf8.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
namespace aether::rendering {
DocumentScrollRequests DocumentScrollRequests::prepare(std::span<const DocumentScrollRequestInput> input,aether::layout::DocumentTextBudget& budget){
 if(input.size()>256)throw std::length_error("SCROLL_REQUEST_LIMIT");
 std::size_t bytes=0;
 // Validate the full borrowed sequence before any request/string copying.
 for(const auto& request:input){
  if(request.id.size()>1024||request.id.size()>65536-bytes)throw std::length_error("SCROLL_ID_LIMIT");
  if(request.x>65536||request.y>65536)throw std::invalid_argument("SCROLL_OFFSET_INVALID");
  budget.normalized(1+2*request.id.size());
  if(request.id.empty()||request.id.find('\0')!=std::string_view::npos||!preparation::detail::valid_utf8(request.id))throw std::invalid_argument("SCROLL_ID_INVALID");
  bytes+=request.id.size();
 }
 struct Less {aether::layout::DocumentTextBudget* budget;bool operator()(std::string_view a,std::string_view b)const{
  budget->normalized(1+std::min(a.size(),b.size()));return a<b;
 }};
 // Ordered index retains borrowed keys only during this callback-free call.
 // Charge tree-node bookkeeping conservatively, not allocator capacity/RSS.
 budget.bytes(input.size(),sizeof(std::string_view)+4*sizeof(void*));
 std::set<std::string_view,Less> ids{Less{&budget}};
 for(const auto& request:input)if(!ids.insert(request.id).second)throw std::invalid_argument("SCROLL_REQUEST_DUPLICATE");
 const std::size_t wire=16+bytes+input.size()*12; // bounded above before arithmetic
 budget.bytes(1,sizeof(DocumentScrollRequests));
 budget.bytes(input.size(),sizeof(DocumentScrollRequest));budget.bytes(bytes+input.size());
 budget.bytes(wire);budget.normalized(bytes+wire+input.size());
 DocumentScrollRequests out;out.entries_.reserve(input.size());out.canonical_.reserve(wire);
 const auto u32=[&](std::uint32_t value){for(unsigned shift=0;shift<32;shift+=8)out.canonical_.push_back(static_cast<std::uint8_t>(value>>shift));};
 constexpr char domain[]="AETHSCRL";
 out.canonical_.insert(out.canonical_.end(),domain,domain+8);u32(1);u32(static_cast<std::uint32_t>(input.size()));
 for(const auto& request:input){
  out.entries_.push_back({std::string(request.id),request.x,request.y});
  u32(static_cast<std::uint32_t>(request.id.size()));
  out.canonical_.insert(out.canonical_.end(),request.id.begin(),request.id.end());
  u32(request.x);u32(request.y);
 }
 if(out.canonical_.size()!=wire)throw std::logic_error("SCROLL_REQUEST_ENCODING");
 return out;
}
}
