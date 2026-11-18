#include <aether/aether.h>
#include "../rendering/html_ltr/interaction_native.h"
#include "../rendering/html_ltr/interaction_frame.h"
#include "../rendering/html_ltr/owned_font_transport.h"
#include "../rendering/html_ltr/owned_software_renderer.h"
#include "../rendering/html_ltr/raster_coordinate.h"
#include <cstring>
#include <limits>
#include <thread>

namespace h=aether::rendering::html_ltr;
namespace r=aether::rendering;
struct aether_interaction_s {
 h::OwnedBudget budget;
 h::OwnedString html;
 std::shared_ptr<h::InteractionNativeDocument> document;
 std::shared_ptr<h::InteractionStateMachine> state;
 std::shared_ptr<h::InteractionNativeScene> scene;
 h::InteractionFrame frame;
 std::thread::id owner=std::this_thread::get_id();
 std::uint64_t sequence{};
 bool busy{};
 aether_interaction_s(h::OwnedBudget::Handle domain,const char* input,size_t length,
                      const aether_interaction_options_v1& options,int32_t width,int32_t height)
  :budget(domain),html(input,input+length,h::OwnedAllocator<char>(budget,h::OwnedCategory::inputs)) {
  const auto source=[&](const uint8_t* bytes,size_t count){
   auto owned=std::allocate_shared<const h::owned_text::Source>(h::OwnedAllocator<const h::owned_text::Source>(budget,h::OwnedCategory::inputs),budget.handle(),std::span<const uint8_t>(bytes,count));
   return h::owned_text::legacy_source(std::move(owned));
  };
  auto regular=source(options.regular_font,options.regular_font_size);
  std::shared_ptr<const r::text::FontSource> bold;
  if(options.bold_font_size)bold=source(options.bold_font,options.bold_font_size);
  document=h::InteractionNativeDocument::create(budget,std::string_view(html.data(),html.size()),std::move(regular),std::move(bold),width,height);
  state=std::allocate_shared<h::InteractionStateMachine>(h::OwnedAllocator<h::InteractionStateMachine>(budget,h::OwnedCategory::values),budget,document->seeds(),document->immutable_scalar_count(),width,height,*document);
 }
};
namespace {
[[noreturn]] void fail(aether_status_t status){throw h::Error{status};}
template<class T> bool valid(const T* p)noexcept{if(!p||p->struct_size!=sizeof(T))return false;for(auto n:p->reserved)if(n)return false;return true;}
template<class T> T zero()noexcept{T out;std::memset(&out,0,sizeof(out));out.struct_size=sizeof(out);return out;}
struct Guard {
 aether_interaction_s& session;
 explicit Guard(aether_interaction_t p):session(checked(p)) {if(session.busy)fail(AETHER_ERROR_INVALID_STATE);session.busy=true;}
 static aether_interaction_s& checked(aether_interaction_t p){if(!p)fail(AETHER_ERROR_INVALID_ARGUMENT);if(p->owner!=std::this_thread::get_id())fail(AETHER_ERROR_WRONG_THREAD);return *p;}
 ~Guard(){session.busy=false;}
};
aether_interaction_result_v1 result(h::InteractionStateResult state)noexcept{auto out=zero<aether_interaction_result_v1>();out.changed=state.changed?1u:0u;out.state_version=state.state_version;out.activated_control_id=state.activated_control_id;return out;}
aether_status_t render_status(r::software::Result v)noexcept{
 using S=r::software::Status;
 switch(v.status){
 case S::ok:return AETHER_OK;
 case S::limit_exceeded:return AETHER_ERROR_LIMIT_EXCEEDED;
 case S::allocation_failed:return AETHER_ERROR_OUT_OF_MEMORY;
 case S::unsupported:return AETHER_ERROR_UNSUPPORTED;
 case S::numeric_error:return AETHER_ERROR_INVALID_ARGUMENT;
 case S::frame_failed:
  switch(v.frame_status){case r::FrameStatus::suspended:return AETHER_ERROR_SUSPENDED;case r::FrameStatus::invalid_viewport:return AETHER_ERROR_INVALID_ARGUMENT;case r::FrameStatus::allocation_failed:return AETHER_ERROR_OUT_OF_MEMORY;case r::FrameStatus::exhausted:return AETHER_ERROR_EXHAUSTED;default:return AETHER_ERROR_INTERNAL;}
 default:return AETHER_ERROR_INTERNAL;
 }
}
}
#define INTERACTION_CATCH catch(const h::Error& e){return e.status;} catch(const h::BoundaryError& e){return e.status;} catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;} catch(...){return AETHER_ERROR_INTERNAL;}
extern "C" {
AETHER_API size_t aether_interaction_options_v1_size(void){return sizeof(aether_interaction_options_v1);}
AETHER_API size_t aether_interaction_control_info_v1_size(void){return sizeof(aether_interaction_control_info_v1);}
AETHER_API size_t aether_interaction_event_v1_size(void){return sizeof(aether_interaction_event_v1);}
AETHER_API size_t aether_interaction_result_v1_size(void){return sizeof(aether_interaction_result_v1);}
AETHER_API size_t aether_interaction_state_v1_size(void){return sizeof(aether_interaction_state_v1);}
AETHER_API size_t aether_interaction_frame_info_v1_size(void){return sizeof(aether_interaction_frame_info_v1);}
AETHER_API aether_status_t aether_interaction_create(int32_t width,int32_t height,const char* html,size_t length,const aether_interaction_options_v1* options,aether_interaction_t* out){
 if(!out)return AETHER_ERROR_INVALID_ARGUMENT;
 *out=nullptr;
 try{
  if(!html||!length||!valid(options)||options->flags||!options->regular_font||!options->regular_font_size||bool(options->bold_font)!=bool(options->bold_font_size))return AETHER_ERROR_INVALID_ARGUMENT;
  (void)h::InteractionFrame::byte_count(width,height);
  constexpr size_t limit=16u*1024u*1024u;
  if(length>65536||options->regular_font_size>limit||options->bold_font_size>limit-options->regular_font_size)return AETHER_ERROR_LIMIT_EXCEEDED;
  h::OwnedBudget initial;
  h::OwnedAllocator<aether_interaction_s> allocator(initial,h::OwnedCategory::temporary);
  auto* session=allocator.allocate(1);
  try{std::construct_at(session,initial.handle(),html,length,*options,width,height);}catch(...){allocator.deallocate(session,1);throw;}
  *out=session;return AETHER_OK;
 }INTERACTION_CATCH
}
AETHER_API aether_status_t aether_interaction_destroy(aether_interaction_t session){
 if(!session)return AETHER_OK;
 try{
  Guard::checked(session);if(session->busy)return AETHER_ERROR_INVALID_STATE;
  h::OwnedAllocator<aether_interaction_s> allocator(session->budget,h::OwnedCategory::temporary);
  std::destroy_at(session);allocator.deallocate(session,1);return AETHER_OK;
 }INTERACTION_CATCH
}
AETHER_API aether_status_t aether_interaction_control_count(aether_interaction_t session,uint32_t* out){
 try{if(!out)return AETHER_ERROR_INVALID_ARGUMENT;
  Guard guard(session);const auto count=session->state->control_count();*out=count;return AETHER_OK;}INTERACTION_CATCH
}
AETHER_API aether_status_t aether_interaction_control_info(aether_interaction_t session,uint32_t index,aether_interaction_control_info_v1* out){
 try{if(!valid(out))return AETHER_ERROR_INVALID_ARGUMENT;
  Guard guard(session);auto info=zero<aether_interaction_control_info_v1>();info.kind=static_cast<uint32_t>(session->state->control_kind(index));info.control_id=index+1;*out=info;return AETHER_OK;}INTERACTION_CATCH
}
AETHER_API aether_status_t aether_interaction_dispatch(aether_interaction_t session,const aether_interaction_event_v1* event,aether_interaction_result_v1* out){
 try{
  if(!valid(event)||!valid(out))return AETHER_ERROR_INVALID_ARGUMENT;
  Guard guard(session);
  const h::InteractionEvent input{event->kind,event->key,event->modifiers,event->x,event->y,event->text,event->text_length};
  (void)h::validate_interaction_event(input);
  const auto before=session->state->metadata();
  h::InteractionPointerContext pointer;
  pointer.frame_current=session->scene&&session->frame.bytes()&&session->frame.version()==before.state_version&&session->frame.width()==before.width&&session->frame.height()==before.height&&before.width&&before.height;
  if(pointer.frame_current&&(event->kind==AETHER_INTERACTION_POINTER_DOWN||event->kind==AETHER_INTERACTION_POINTER_UP))pointer.hit_id=session->scene->hit(event->x,event->y);
  const auto next=session->state->dispatch(input,pointer,*session->document);
  *out=result(next);return AETHER_OK;
 }INTERACTION_CATCH
}
AETHER_API aether_status_t aether_interaction_resize(aether_interaction_t session,int32_t width,int32_t height,aether_interaction_result_v1* out){
 try{if(!valid(out))return AETHER_ERROR_INVALID_ARGUMENT;
  Guard guard(session);const auto next=session->state->resize(width,height,*session->document);*out=result(next);return AETHER_OK;}INTERACTION_CATCH
}
AETHER_API aether_status_t aether_interaction_render(aether_interaction_t session,aether_interaction_result_v1* out){
 try{
  if(!valid(out))return AETHER_ERROR_INVALID_ARGUMENT;
  Guard guard(session);
  const auto state=session->state->metadata();
  if(!state.width||!state.height)return AETHER_ERROR_SUSPENDED;
  if(session->scene&&session->frame.bytes()&&session->frame.version()==state.state_version&&session->frame.width()==state.width&&session->frame.height()==state.height){*out=result({false,state.state_version,0});return AETHER_OK;}
  if(session->sequence==std::numeric_limits<uint64_t>::max())return AETHER_ERROR_EXHAUSTED;
  auto candidate=session->document->prepare(session->state->candidate_view());
  const auto rendered=h::render_interaction_fractional_rect_accounted(session->budget,session->frame,{state.width,state.height,1},state.state_version,session->sequence+1,candidate->prepared(),candidate->masks(),nullptr);
  const auto status=render_status(rendered);if(status!=AETHER_OK)return status;
  session->scene=std::move(candidate);++session->sequence;
  *out=result({true,state.state_version,0});return AETHER_OK;
 }INTERACTION_CATCH
}
AETHER_API aether_status_t aether_interaction_copy_frame(aether_interaction_t session,aether_interaction_frame_info_v1* out,uint8_t* destination,size_t capacity){
 try{
  if(!valid(out)||(!destination&&capacity))return AETHER_ERROR_INVALID_ARGUMENT;
  Guard guard(session);
  if(!session->frame.bytes())return AETHER_ERROR_NO_FRAME;
  auto info=zero<aether_interaction_frame_info_v1>();info.width=session->frame.width();info.height=session->frame.height();info.format=AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED;info.stride=uint64_t(info.width)*4;info.byte_count=session->frame.bytes();info.sequence=session->frame.sequence();info.state_version=session->frame.version();
  if(!destination){*out=info;return AETHER_OK;}
  if(capacity<info.byte_count){*out=info;return AETHER_ERROR_BUFFER_TOO_SMALL;}
  std::memcpy(destination,session->frame.data(),session->frame.bytes());*out=info;return AETHER_OK;
 }INTERACTION_CATCH
}
AETHER_API aether_status_t aether_interaction_state(aether_interaction_t session,aether_interaction_state_v1* out){
 try{
  if(!valid(out))return AETHER_ERROR_INVALID_ARGUMENT;
  Guard guard(session);const auto s=session->state->metadata();
  auto info=zero<aether_interaction_state_v1>();info.width=s.width;info.height=s.height;info.focus_id=s.focus_id;info.arm_kind=static_cast<uint32_t>(s.arm);info.arm_id=s.arm_id;info.keys_down=s.keys_down;info.pointer_primary_down=s.pointer_primary_down?1u:0u;info.state_version=s.state_version;info.frame_version=session->frame.version();info.sequence=session->sequence;*out=info;return AETHER_OK;
 }INTERACTION_CATCH
}
}
#ifdef AETHER_INTERACTION_TESTING
#include "interaction_test_hooks.h"
namespace aether::rendering::html_ltr {
struct OwnedBudgetTestAccess {static void limit(OwnedBudget& b,std::size_t n){if(n<b.state_->total.current||n>OwnedBudget::limit_bytes)throw Error{AETHER_ERROR_INVALID_ARGUMENT};b.state_->limit=n;}};
struct InteractionStateTestAccess {static void version(InteractionStateMachine& s,std::uint64_t n){s.data_.metadata.state_version=n;}};
struct InteractionFrameTestAccess {static void sequence(InteractionFrame& f,std::uint64_t n){f.sequence_=n;}};
}
namespace aether::interaction_test {
StorageStats storage(aether_interaction_t session){
 Guard::checked(session);StorageStats stats;const auto total=session->budget.total();stats.current=total.current;stats.peak=total.peak;stats.limit=session->budget.limit();
 for(std::size_t i=0;i<stats.categories.size();++i) { stats.categories[i]=session->budget.usage(static_cast<h::OwnedCategory>(i)); }
 return stats;
}
void storage_limit(aether_interaction_t s,std::size_t n){Guard::checked(s);h::OwnedBudgetTestAccess::limit(s->budget,n);}
void state_version(aether_interaction_t s,std::uint64_t n){Guard::checked(s);h::InteractionStateTestAccess::version(*s->state,n);}
void sequence(aether_interaction_t s,std::uint64_t n){Guard::checked(s);s->sequence=n;h::InteractionFrameTestAccess::sequence(s->frame,n);}
}
#endif
