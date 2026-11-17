#include "real_font_binding.h"
#include <limits>
#include <new>
namespace aether::rendering::text::truetype {
namespace {
BindingError convert(Error error) noexcept {
 switch(error) {
 case Error::malformed:return BindingError::malformed;
 case Error::unsupported:return BindingError::unsupported;
 case Error::limit_exceeded:return BindingError::limit_exceeded;
 case Error::cancelled:return BindingError::cancelled;
 case Error::out_of_memory:return BindingError::out_of_memory;
 }
 return BindingError::malformed;
}
BindingError convert(CmapError error) noexcept {
 switch(error) {
 case CmapError::invalid_input:return BindingError::invalid_input;
 case CmapError::malformed:return BindingError::malformed;
 case CmapError::unsupported:return BindingError::unsupported;
 case CmapError::limit_exceeded:return BindingError::limit_exceeded;
 case CmapError::cancelled:return BindingError::cancelled;
 case CmapError::out_of_memory:return BindingError::out_of_memory;
 }
 return BindingError::malformed;
}
bool sum(BindingWork& work) noexcept {
 if(work.cmap.units>std::numeric_limits<std::size_t>::max()-work.face.units)return false;
 work.units=work.face.units+work.cmap.units;
 return true;
}
}
BindingResult RealFontBinding::create(std::shared_ptr<const FontSource> source,
 std::int32_t size,std::uint32_t flags,Cancellation* cancel,BindingWork* diagnostic) noexcept {
 BindingWork local{};BindingWork& work=diagnostic?*diagnostic:local;work={};
 const auto cancelled=[&]() noexcept {++work.cancel_checks;return cancel&&cancel->cancelled();};
 try {
  if(cancelled())return BindingError::cancelled;
  if(!source||size<1||size>8192)return BindingError::invalid_input;
  if(flags!=q1_unhinted)return BindingError::unsupported;
  work.face_started=true;
  auto face_result=ValidatedFace::create(source,0,cancel,&work.face);
  if(!sum(work))return BindingError::limit_exceeded;
  if(auto error=std::get_if<Error>(&face_result))return convert(*error);
  if(cancelled())return BindingError::cancelled;
  auto face=std::get<std::shared_ptr<const ValidatedFace>>(std::move(face_result));
  work.cmap_started=true;
  auto map_result=UnicodeMap::create(face,cancel,&work.cmap);
  if(!sum(work))return BindingError::limit_exceeded;
  if(auto error=std::get_if<CmapError>(&map_result))return convert(*error);
  if(cancelled())return BindingError::cancelled;
  auto map=std::get<std::shared_ptr<const UnicodeMap>>(std::move(map_result));
  work.instance_started=true;
  auto font=std::make_shared<const FontInstance>(std::move(source),0,size,flags);
  if(cancelled())return BindingError::cancelled;
  // All intermediate state is local. A failure/cancellation cannot replace a
  // caller's previous binding or expose an incompletely constructed object.
  auto binding=std::shared_ptr<const RealFontBinding>(
    new RealFontBinding(std::move(face),std::move(map),std::move(font)));
  work.binding_created=true;
  if(cancelled())return BindingError::cancelled;
  return binding;
 } catch(const std::bad_alloc&) {return BindingError::out_of_memory;}
}
}
