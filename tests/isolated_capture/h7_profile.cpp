#include "h7_profile.h"
#include "../../native/src/style_runtime/paused_keyframes.h"
#include "../../native/src/style_runtime/paused_animation_properties.h"
namespace h7 {
bool keyframes(const std::string& name,const std::string& body) {
 const auto parsed=paused_keyframes_parse(name,body);
 return parsed.profile_supported&&!parsed.limit_exceeded;
}
bool declaration(const std::string& property,const std::string& value) {
 if(property=="animation"||property.starts_with("animation-"))
  return paused_animation_declaration_supported(property,value);
 return true;
}
}
