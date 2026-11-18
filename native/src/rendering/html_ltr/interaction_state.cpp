#include "interaction_state.h"
#include "interaction_frame.h"
#include <cmath>
#include <limits>

namespace aether::rendering::html_ltr {
namespace {
[[noreturn]] void fail(aether_status_t status){throw Error{status};}
// Complete decoding precedes repertoire/count decisions. This helper has no
// logging or diagnostics containing input bytes.
std::size_t scalars(std::string_view text){
 for(std::size_t i=0;i<text.size();)(void)decode_scalar_utf8(text,i);
 std::size_t count=0;bool supported=true;
 for(std::size_t i=0;i<text.size();){supported=repertoire_scalar(decode_scalar_utf8(text,i))&&supported;++count;}
 if(!supported)fail(AETHER_ERROR_UNSUPPORTED);
 return count;
}
std::size_t event_shape(const InteractionEvent& event){
 const auto kind=static_cast<InteractionEventKind>(event.kind);
 if(event.kind<1||event.kind>7)fail(AETHER_ERROR_UNSUPPORTED);
 if(kind==InteractionEventKind::pointer_down||kind==InteractionEventKind::pointer_up){
  if(event.key||event.modifiers||event.text||event.text_length||!std::isfinite(event.x)||!std::isfinite(event.y))fail(AETHER_ERROR_INVALID_ARGUMENT);
 }else if(kind==InteractionEventKind::key_down||kind==InteractionEventKind::key_up){
  if(event.x!=0||event.y!=0||event.text||event.text_length)fail(AETHER_ERROR_INVALID_ARGUMENT);
  if(event.key<1||event.key>4||event.modifiers&~interaction_shift)fail(AETHER_ERROR_UNSUPPORTED);
  if(event.modifiers&&event.key!=static_cast<std::uint32_t>(InteractionKey::tab))fail(AETHER_ERROR_INVALID_ARGUMENT);
 }else if(kind==InteractionEventKind::text){
  if(event.key||event.modifiers||event.x!=0||event.y!=0||(!event.text&&event.text_length))fail(AETHER_ERROR_INVALID_ARGUMENT);
  // Byte-limit rejection bounds decoding work. Within this envelope decode the
  // entire payload before scalar count/repertoire and before looking at focus.
  if(event.text_length>1024)fail(AETHER_ERROR_LIMIT_EXCEEDED);
  const auto n=scalars(event.text_length?std::string_view(reinterpret_cast<const char*>(event.text),event.text_length):std::string_view{});
  if(n>256)fail(AETHER_ERROR_LIMIT_EXCEEDED);
  return n;
 }else if(event.key||event.modifiers||event.x!=0||event.y!=0||event.text||event.text_length)fail(AETHER_ERROR_INVALID_ARGUMENT);
 return 0;
}
struct Busy {bool& value;explicit Busy(bool& b):value(b){if(value)fail(AETHER_ERROR_INVALID_STATE);value=true;}~Busy(){value=false;}};
void clear_arm(InteractionMetadata& m)noexcept{m.arm=InteractionArm::none;m.arm_id=0;}
}
std::size_t validate_interaction_event(const InteractionEvent& event){return event_shape(event);}
InteractionPresentation::InteractionPresentation(OwnedBudget::Handle d,InteractionControlKind k):kind(k),text(OwnedAllocator<char>(d,OwnedCategory::values)){}
InteractionStateMachine::Definition::Definition(OwnedBudget::Handle d,InteractionControlKind k,std::string_view p):kind(k),placeholder(p.begin(),p.end(),OwnedAllocator<char>(d,OwnedCategory::values)){}
InteractionStateMachine::Data::Data(OwnedBudget::Handle d):values(OwnedAllocator<OwnedString>(d,OwnedCategory::values)),scalar_counts(OwnedAllocator<std::uint16_t>(d,OwnedCategory::values)),presentations(OwnedAllocator<InteractionPresentation>(d,OwnedCategory::values)){}
void InteractionStateMachine::Data::swap(Data& other)noexcept{std::swap(metadata,other.metadata);values.swap(other.values);scalar_counts.swap(other.scalar_counts);presentations.swap(other.presentations);std::swap(value_scalars,other.value_scalars);}
void InteractionStateMachine::owner()const{if(owner_!=std::this_thread::get_id())fail(AETHER_ERROR_INVALID_STATE);}
bool InteractionStateMachine::input(std::uint32_t id)const noexcept{return id&&id<=definitions_.size()&&definitions_[id-1].kind!=InteractionControlKind::button;}
bool InteractionStateMachine::button(std::uint32_t id)const noexcept{return id&&id<=definitions_.size()&&definitions_[id-1].kind==InteractionControlKind::button;}
void InteractionStateMachine::present(Data& data,std::size_t i)const{
 auto& p=data.presentations[i];p.text.clear();
 p.shows_placeholder=input(static_cast<std::uint32_t>(i+1))&&data.values[i].empty();
 if(p.shows_placeholder)p.text=definitions_[i].placeholder;
 else if(p.kind==InteractionControlKind::text)p.text=data.values[i];
 else if(p.kind==InteractionControlKind::password){p.text.reserve(std::size_t(data.scalar_counts[i])*3);for(std::size_t n=0;n<data.scalar_counts[i];++n)p.text.append("\xe2\x80\xa2",3);}
}
InteractionStateMachine::InteractionStateMachine(OwnedBudget& budget,std::span<const InteractionControlSeed> seeds,std::size_t immutable,
 std::int32_t width,std::int32_t height,NativeStateValidator& validator)
 :domain_(budget.handle()),definitions_(OwnedAllocator<Definition>(domain_,OwnedCategory::values)),immutable_scalars_(immutable),data_(domain_){
 (void)InteractionFrame::byte_count(width,height);
 if(seeds.size()>32||immutable>4096)fail(AETHER_ERROR_LIMIT_EXCEEDED);
 // Validate all seeds and quotas before copying any authored values.
 std::size_t initial_values=0,placeholder_scalars=0;
 for(const auto& seed:seeds){
  if(seed.kind!=InteractionControlKind::text&&seed.kind!=InteractionControlKind::password&&seed.kind!=InteractionControlKind::button)fail(AETHER_ERROR_INVALID_ARGUMENT);
  if(seed.kind==InteractionControlKind::button&&(!seed.initial_value.empty()||!seed.placeholder.empty()))fail(AETHER_ERROR_INVALID_ARGUMENT);
  const auto n=scalars(seed.initial_value),p=scalars(seed.placeholder);if(n>256||p>256)fail(AETHER_ERROR_LIMIT_EXCEEDED);
  initial_values+=n;placeholder_scalars+=p;
 }
 if(placeholder_scalars>immutable)fail(AETHER_ERROR_INVALID_ARGUMENT);
 if(initial_values>4096-immutable)fail(AETHER_ERROR_LIMIT_EXCEEDED);
 definitions_.reserve(seeds.size());data_.values.reserve(seeds.size());data_.scalar_counts.reserve(seeds.size());data_.presentations.reserve(seeds.size());
 for(const auto& seed:seeds){definitions_.emplace_back(domain_,seed.kind,seed.placeholder);data_.values.emplace_back(seed.initial_value.begin(),seed.initial_value.end(),OwnedAllocator<char>(domain_,OwnedCategory::values));data_.scalar_counts.push_back(static_cast<std::uint16_t>(scalars(seed.initial_value)));data_.presentations.emplace_back(domain_,seed.kind);present(data_,data_.values.size()-1);}
 data_.value_scalars=initial_values;data_.metadata.width=width;data_.metadata.height=height;
 Busy guard(busy_);validator.validate({data_.metadata,data_.presentations});
}
InteractionMetadata InteractionStateMachine::metadata()const{owner();return data_.metadata;}
std::uint32_t InteractionStateMachine::control_count()const{owner();return static_cast<std::uint32_t>(definitions_.size());}
InteractionControlKind InteractionStateMachine::control_kind(std::uint32_t index)const{owner();if(index>=definitions_.size())fail(AETHER_ERROR_INVALID_ARGUMENT);return definitions_[index].kind;}
InteractionCandidateView InteractionStateMachine::candidate_view()const{owner();return {data_.metadata,data_.presentations};}
InteractionStateResult InteractionStateMachine::commit(Data& candidate,std::uint32_t activation,NativeStateValidator& validator){
 validator.validate({candidate.metadata,candidate.presentations});
 data_.swap(candidate);
 return {true,data_.metadata.state_version,activation};
}
InteractionStateResult InteractionStateMachine::dispatch(const InteractionEvent& event,InteractionPointerContext pointer,NativeStateValidator& validator){
 owner();Busy guard(busy_);
 const auto payload_scalars=validate_interaction_event(event);const auto kind=static_cast<InteractionEventKind>(event.kind);const auto key=static_cast<InteractionKey>(event.key);
 auto metadata=data_.metadata;std::uint32_t activation=0;std::uint32_t edited=0;bool remove=false;
 const bool pointer_event=kind==InteractionEventKind::pointer_down||kind==InteractionEventKind::pointer_up;
 if(pointer_event){
  if(!pointer.frame_current||!metadata.width||!metadata.height)fail(AETHER_ERROR_INVALID_STATE);
  if(pointer.hit_id>definitions_.size())fail(AETHER_ERROR_INTERNAL);
  if(event.x<0||event.y<0||event.x>=metadata.width||event.y>=metadata.height)pointer.hit_id=0;
 }
 switch(kind){
 case InteractionEventKind::pointer_down:
  if(!metadata.pointer_primary_down){metadata.pointer_primary_down=true;clear_arm(metadata);metadata.focus_id=pointer.hit_id;if(button(pointer.hit_id)){metadata.arm=InteractionArm::pointer;metadata.arm_id=pointer.hit_id;}}break;
 case InteractionEventKind::pointer_up:
  if(metadata.pointer_primary_down){metadata.pointer_primary_down=false;if(metadata.arm==InteractionArm::pointer){if(metadata.arm_id==pointer.hit_id&&metadata.focus_id==metadata.arm_id)activation=metadata.arm_id;clear_arm(metadata);}}break;
 case InteractionEventKind::pointer_cancel:metadata.pointer_primary_down=false;if(metadata.arm==InteractionArm::pointer)clear_arm(metadata);break;
 case InteractionEventKind::key_down:
  switch(key){
  case InteractionKey::tab:{const auto n=static_cast<std::uint32_t>(definitions_.size());if(n){if(event.modifiers&interaction_shift)metadata.focus_id=metadata.focus_id<=1?n:metadata.focus_id-1;else metadata.focus_id=metadata.focus_id==n?1:metadata.focus_id+1;}clear_arm(metadata);break;}
  case InteractionKey::enter:if(!(metadata.keys_down&interaction_enter_down)){metadata.keys_down|=interaction_enter_down;if(button(metadata.focus_id))activation=metadata.focus_id;}break;
  case InteractionKey::space:if(!(metadata.keys_down&interaction_space_down)){metadata.keys_down|=interaction_space_down;if(button(metadata.focus_id)&&metadata.arm!=InteractionArm::pointer){metadata.arm=InteractionArm::space;metadata.arm_id=metadata.focus_id;}}break;
  case InteractionKey::backspace:if(input(metadata.focus_id)&&data_.scalar_counts[metadata.focus_id-1]){edited=metadata.focus_id;remove=true;}break;
  default:break;
  }break;
 case InteractionEventKind::key_up:
  if(key==InteractionKey::enter)metadata.keys_down&=~interaction_enter_down;
  else if(key==InteractionKey::space){metadata.keys_down&=~interaction_space_down;if(metadata.arm==InteractionArm::space){if(metadata.focus_id==metadata.arm_id)activation=metadata.arm_id;clear_arm(metadata);}}
  break;
 case InteractionEventKind::text:
  if(input(metadata.focus_id)&&event.text_length){edited=metadata.focus_id;if(payload_scalars>std::size_t(256-data_.scalar_counts[edited-1])||payload_scalars>4096-immutable_scalars_-data_.value_scalars)fail(AETHER_ERROR_LIMIT_EXCEEDED);}break;
 case InteractionEventKind::blur:metadata.focus_id=0;clear_arm(metadata);metadata.pointer_primary_down=false;metadata.keys_down=0;break;
 }
 if(metadata==data_.metadata&&!edited)return {false,data_.metadata.state_version,0};
 if(data_.metadata.state_version==std::numeric_limits<std::uint64_t>::max())fail(AETHER_ERROR_EXHAUSTED);
 ++metadata.state_version;
 Data candidate=data_;candidate.metadata=metadata;
 if(edited){const auto i=edited-1;auto& value=candidate.values[i];if(remove){std::size_t start=value.size()-1;while(start&&(static_cast<unsigned char>(value[start])&0xc0)==0x80)--start;value.erase(start);--candidate.scalar_counts[i];--candidate.value_scalars;}else{value.append(reinterpret_cast<const char*>(event.text),event.text_length);candidate.scalar_counts[i]=static_cast<std::uint16_t>(candidate.scalar_counts[i]+payload_scalars);candidate.value_scalars+=payload_scalars;}present(candidate,i);}
 return commit(candidate,activation,validator);
}
InteractionStateResult InteractionStateMachine::resize(std::int32_t width,std::int32_t height,NativeStateValidator& validator){
 owner();Busy guard(busy_);(void)InteractionFrame::byte_count(width,height);
 if(width==data_.metadata.width&&height==data_.metadata.height)return {false,data_.metadata.state_version,0};
 if(data_.metadata.state_version==std::numeric_limits<std::uint64_t>::max())fail(AETHER_ERROR_EXHAUSTED);
 Data candidate=data_;candidate.metadata.width=width;candidate.metadata.height=height;candidate.metadata.pointer_primary_down=false;if(candidate.metadata.arm==InteractionArm::pointer)clear_arm(candidate.metadata);++candidate.metadata.state_version;
 return commit(candidate,0,validator);
}
}
