#pragma once
#include "h4_profile.h"
#include "h8_profile.h"
#include "../../native/src/style_runtime/value_audit_internal.h"
#include "../../native/src/style_runtime/frozen_runtime_metadata.h"
#include <stdexcept>
#include <cmath>
namespace h14 {
// All calls in these values must be var(), including unused fallbacks.
inline bool variable_source(const std::string& property,const std::string& source) {
 if(property!="flex"&&property!="flex-basis"&&!property.starts_with("--"))return false;
 if(source.size()>4096)return false;
 css_variable_budget budget{32768,false};css_variable_lexer lexer(source.data(),source.size(),0,&budget);
 size_t functions=0,depth=0;
 for(;;){auto t=lexer.next();if(t.kind==css_variable_token_kind::end)break;
  if(t.kind==css_variable_token_kind::error||budget.exhausted)return false;
  if(t.serialization==css_variable_serialization::url||t.serialization==css_variable_serialization::bad_url)return false;
  if(t.kind==css_variable_token_kind::function){if(!t.is_var||++functions>32||++depth>16)return false;}
  else if(t.kind==css_variable_token_kind::open)return false;
  else if(t.kind==css_variable_token_kind::close){if(!depth||t.delimiter!=')')return false;--depth;}
 }
 return functions&&!budget.exhausted&&!depth&&validate_pending_declaration_value(source);
}
inline bool number(const std::string& s){return h4::number(s)&&std::isfinite(static_cast<float>(std::strtod(s.c_str(),nullptr)));}
inline bool basis(const std::string& source) {
 if(source=="auto"||h8::intrinsic(source))return true;
 css_component_cursor cursor(source);css_component_value c,next;
 if(!cursor.next(c)||cursor.next(next)||!cursor.valid())return false;
 const auto t=c.token;
 if(t.serialization==css_variable_serialization::number)return number(source)&&std::strtod(source.c_str(),nullptr)==0;
 if(t.serialization!=css_variable_serialization::dimension&&t.serialization!=css_variable_serialization::percentage)return false;
 const auto suffix=source.substr(t.numeric_end);
 return (suffix=="px"||suffix=="%")&&number(source.substr(0,t.numeric_end));
}
inline bool resolved(const std::string& property,const std::string& source) {
 bool ok=false;const auto parts=css_split_components(source,&ok);if(!ok||parts.empty())return false;
 if(parts.size()==1&&(parts[0]=="initial"||parts[0]=="inherit"||parts[0]=="unset"||parts[0]=="revert"))return true;
 if(property=="flex-basis")return parts.size()==1&&basis(parts[0]);
 if(property!="flex")return false;
 if(parts.size()==1)return parts[0]=="none"||parts[0]=="auto"||number(parts[0])||basis(parts[0]);
 if(parts.size()==2)return number(parts[0])&&(number(parts[1])||basis(parts[1]));
 return parts.size()==3&&number(parts[0])&&number(parts[1])&&basis(parts[2]);
}
struct Audit {
 css_variable_budget work{1048576,false};size_t observed=0,scopes=0;bool rejected=false;
 void reject(){rejected=true;throw std::invalid_argument("H14 variable profile or work limit");}
 static void scope(void* p,const aether::style::FrozenCustomPropertyScope* s) {
  auto& a=*static_cast<Audit*>(p);if(a.work.exhausted||++a.scopes>4096)a.reject();
  // Invalid and depth/expansion failure share the native status. Conservatively
  // reject both so a fallback cannot hide an exhausted computation.
  if(s)for(const auto& entry:s->local_entries())if(entry.status()!=custom_property_status::valid)a.reject();
 }
 static void value(void* p,aether_node_t,const std::string& property,const std::string& source,const std::string* result) {
  auto& a=*static_cast<Audit*>(p);if(a.work.exhausted||++a.observed>4096)a.reject();
  if(!variable_has_reference(source))return;
  if((property=="flex"||property=="flex-basis")&&(!result||!resolved(property,*result)))a.reject();
 }
 style_value_audit_s hook(){return {&work,this,scope,value};}
};
}
