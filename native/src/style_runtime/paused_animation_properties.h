#pragma once
#include "style_runtime_types.h"
#include "style_property_registry_internal.h"
#include "css_component_tokens.h"
#include <array>
#include <cmath>
#include <cstdio>

// Single-animation, deterministic initial paused sample. These parsers never
// publish an animation clock or reinterpret a decoded identifier as CSS.
namespace paused_animation {
inline constexpr std::array<const char*,8> names = {
 "animation-name", "animation-duration", "animation-delay", "animation-timing-function",
 "animation-iteration-count", "animation-direction", "animation-fill-mode", "animation-play-state"};
inline constexpr std::array<const char*,8> defaults = {"none","0s","0s","ease","1","normal","none","running"};
inline bool tokens(const std::string& s,std::vector<css_variable_token>& out) {
 css_component_lexer l(s.data(),s.size()); std::vector<css_variable_token> staged;
 for(;;){auto t=l.next();if(t.kind==css_variable_token_kind::end)break;
  if(t.kind==css_variable_token_kind::whitespace||t.kind==css_variable_token_kind::comment)continue;
  if(staged.size()==16 || t.kind==css_variable_token_kind::error || t.kind==css_variable_token_kind::comma || t.kind==css_variable_token_kind::function || t.kind==css_variable_token_kind::open || t.kind==css_variable_token_kind::close)return false;
  staged.push_back(t);
 }
 out=std::move(staged);return !out.empty();
}
inline bool ident(const std::string& s,std::string& out) {
 std::vector<css_variable_token> ts;if(!tokens(s,ts)||ts.size()!=1||ts[0].kind!=css_variable_token_kind::ident)return false;
 std::string staged;auto t=ts[0];css_variable_lexer decoder(s.data()+t.begin,t.end-t.begin);
 if(!decoder.decode_identifier([](void*p,const char*b,size_t n){static_cast<std::string*>(p)->append(b,n);return true;},&staged))return false;
 out=std::move(staged);return true;
}
inline std::string number_text(double v){char b[64];std::snprintf(b,sizeof b,"%.17g",v);return b;}
inline bool number(const std::string& s,double& out,bool time) {
 std::vector<css_variable_token> ts;if(!tokens(s,ts)||ts.size()!=1)return false;auto t=ts[0];
 if(t.serialization!=(time?css_variable_serialization::dimension:css_variable_serialization::number))return false;
 std::string numeric=s.substr(t.begin,t.numeric_end-t.begin);char* e=nullptr;double n=std::strtod(numeric.c_str(),&e);
 if(e!=numeric.c_str()+numeric.size()||!std::isfinite(n))return false;
 if(time){std::string unit;if(!ident(s.substr(t.numeric_end,t.end-t.numeric_end),unit))return false;
  if(str_iequals(unit,"ms"))n/=1000.;else if(!str_iequals(unit,"s"))return false;}
 if(!std::isfinite(n))return false;
 out=n;return true;
}
inline bool parse(size_t i,const std::string& s,std::string& out){
 std::string v;
 if(i==1||i==2){double n;if(!number(s,n,true)||(i==1&&n<0))return false;out=number_text(n);return true;}
 if(i==4){double n;if(number(s,n,false)){if(n<0)return false;out=number_text(n);return true;}}
 if(!ident(s,v))return false;
 const auto lower=str_to_lower(v);
 if(i==0){if(lower=="initial"||lower=="inherit"||lower=="unset"||lower=="revert"||lower=="revert-layer"||lower=="default")return false;
  out=lower=="none"?"none":v;return true;}
 bool valid=false;
 if(i==3)valid=lower=="linear"||lower=="ease"||lower=="ease-in"||lower=="ease-out"||lower=="ease-in-out"||lower=="step-start"||lower=="step-end";
 if(i==4)valid=lower=="infinite";
 if(i==5)valid=lower=="normal"||lower=="reverse"||lower=="alternate"||lower=="alternate-reverse";
 if(i==6)valid=lower=="none"||lower=="forwards"||lower=="backwards"||lower=="both";
 if(i==7)valid=lower=="running"||lower=="paused";
 if(!valid)return false;
 out=lower;return true;
}
inline void publish(const std::array<std::string,8>& raw,style_shorthand_writer_s& writer){
 style_shorthand_writer_s staged;
 for(size_t i=0;i<8;++i)staged.set(names[i],raw[i].c_str());
 writer.items.reserve(writer.items.size()+staged.items.size());
 for(auto& item:staged.items)writer.items.push_back(std::move(item));
}
inline bool expand(const char* source,style_shorthand_writer_s& writer){
 if(!source)return false;
 std::string s(source);std::string id;
 std::array<std::string,8> raw;for(size_t i=0;i<8;++i)raw[i]=defaults[i];
 if(ident(s,id)&&(str_iequals(id,"initial")||str_iequals(id,"inherit")||str_iequals(id,"unset")||str_iequals(id,"revert"))){for(auto& v:raw)v=str_to_lower(id);publish(raw,writer);return true;}
 std::vector<css_variable_token> ts;if(!tokens(s,ts))return false;
 std::array<bool,8> seen{};
 for(auto t:ts){std::string part=s.substr(t.begin,t.end-t.begin),parsed;double n;
  if(number(part,n,true)){size_t i=seen[1]?2:1;if(seen[i]||!parse(i,part,parsed))return false;seen[i]=true;raw[i]=part;continue;}
  bool assigned=false;
  // CSS shorthand prioritizes keywords before the custom animation name.
  for(size_t i:{size_t(3),size_t(4),size_t(5),size_t(6),size_t(7)})if(!seen[i]&&parse(i,part,parsed)){seen[i]=true;raw[i]=part;assigned=true;break;}
  if(assigned)continue;
  if(seen[0]||!parse(0,part,parsed))return false;
  seen[0]=true;raw[0]=part;
 }
 // Stage all items so a syntax error never partially expands a shorthand.
 publish(raw,writer);return true;
}
}
inline void paused_animation_register_longhands(style_property_registry_s* reg){
 for(size_t i=0;i<8;++i)reg->register_property(paused_animation::names[i],false,STYLE_IMPACT_LAYOUT|STYLE_IMPACT_PAINT,paused_animation::defaults[i],
 [i](const char*s,std::string&o){return s&&paused_animation::parse(i,s,o);},[](aether_computed_style_t*,const std::string&){});
}
inline void paused_animation_register_shorthands(style_property_registry_s* reg){
 reg->register_shorthand("animation",paused_animation::expand,std::vector<std::string>(paused_animation::names.begin(),paused_animation::names.end()));
}
struct paused_animation_parameters {
 std::string name="none";
 double duration=0,delay=0;
 bool profile_supported=true;
 bool active=false;
 double progress=0;
};
inline paused_animation_parameters paused_animation_read(const std::vector<cascade_entry_s>& cascade,const style_property_registry_s* registry){
 paused_animation_parameters p;std::array<std::string,8> values;
 for(size_t i=0;i<8;++i){if(!paused_animation::parse(i,paused_animation::defaults[i],values[i])){p.profile_supported=false;return p;}
  const auto* d=registry->try_get(paused_animation::names[i]);if(!d||size_t(d->id)>=cascade.size()){p.profile_supported=false;return p;}
  const auto& c=cascade[size_t(d->id)];if(!c.is_set)continue;
  if(c.value.mode==compiled_value_mode::value)values[i]=c.value.typed_value;
  else if(c.value.mode!=compiled_value_mode::initial&&c.value.mode!=compiled_value_mode::unset){p.profile_supported=false;return p;}
 }
 p.name=values[0];p.duration=std::strtod(values[1].c_str(),nullptr);p.delay=std::strtod(values[2].c_str(),nullptr);
 if(p.name=="none")return p;
 p.profile_supported=std::isfinite(p.duration)&&std::isfinite(p.delay)&&p.duration>0&&values[3]=="linear"&&values[4]=="1"&&values[5]=="normal"&&values[6]=="none"&&values[7]=="paused";
 p.active=p.profile_supported&&p.delay<=0&&-p.delay<p.duration;
 if(p.active)p.progress=-p.delay/p.duration;
 return p;
}
// Capture admission is deliberately stricter than CSS syntactic recognition.
// This checks authored declarations, not a substitute for effective cascade.
inline bool paused_animation_declaration_supported(const std::string& property,const std::string& source){
 const auto prop=str_to_lower(property);
 if(prop=="animation"){
  style_shorthand_writer_s w;if(!paused_animation::expand(source.c_str(),w))return false;
  std::array<std::string,8> v;
  if(w.items.size()!=8)return false;
  for(size_t i=0;i<8;++i)if(!paused_animation::parse(i,w.items[i].value,v[i]))return false;
  if(v[0]=="none")return true;
  return std::strtod(v[1].c_str(),nullptr)>0&&v[3]=="linear"&&v[4]=="1"&&v[5]=="normal"&&v[6]=="none"&&v[7]=="paused";
 }
 for(size_t i=0;i<8;++i)if(prop==paused_animation::names[i]){
  std::string v;if(!paused_animation::parse(i,source,v))return false;
  if(i==0||i==2)return true;
  if(i==1)return std::strtod(v.c_str(),nullptr)>0;
  return v==(i==3?"linear":i==4?"1":i==5?"normal":i==6?"none":"paused");
 }
 return false;
}
