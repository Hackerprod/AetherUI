#pragma once
#include "style_runtime_types.h"
#include "style_property_registry_internal.h"
#include "css_single_identifier.h"
#include "css_important.h"
#include "paused_animation_properties.h"
#include <array>
#include <cmath>
#include <map>

// An owned, bounded capture-time subset, not a live CSS animation timeline.
struct paused_keyframe_value_s { std::string property; double from=0, to=0; };
struct paused_keyframes_s {
    std::string name;
    bool profile_supported=false;
    bool limit_exceeded=false;
    std::vector<paused_keyframe_value_s> values;
};
inline bool paused_keyframe_number(const std::string& s, bool px, double& out) {
    css_component_lexer lexer(s.data(),s.size());
    auto t=lexer.next();
    if(t.kind==css_variable_token_kind::whitespace)t=lexer.next();
    if(t.serialization!=css_variable_serialization::number && t.serialization!=css_variable_serialization::dimension)return false;
    std::string raw=s.substr(t.begin,t.end-t.begin); char* end=nullptr;
    out=std::strtod(raw.c_str(),&end);
    if(!std::isfinite(out)||std::abs(out)>3.402823466e38)return false;
    if(px ? !str_iequals(end,"px") : *end!='\0')return false;
    do{t=lexer.next();}while(t.kind==css_variable_token_kind::whitespace||t.kind==css_variable_token_kind::comment);
    return t.kind==css_variable_token_kind::end;
}
inline paused_keyframes_s paused_keyframes_parse(const std::string& name_source,const std::string& body, size_t* shared_budget=nullptr) {
    paused_keyframes_s out;
    // Bounded tokenization occurs before parsing/copying any nested block.
    css_component_lexer scan(body.data(),body.size());size_t tokens=0;int depth=0;
    for(;;){auto t=scan.next();if(t.kind==css_variable_token_kind::end)break;
      if(++tokens>65536 || (shared_budget && *shared_budget==0)){out.limit_exceeded=true;return out;}
      if(shared_budget)--*shared_budget;
      if(t.kind==css_variable_token_kind::error)return out;
      if(t.kind==css_variable_token_kind::function||t.kind==css_variable_token_kind::open){if(++depth>32){out.limit_exceeded=true;return out;}}
      if(t.kind==css_variable_token_kind::close && --depth<0)return out;
    }
    if(depth)return out;
    if(!paused_animation::ident(name_source,out.name) || str_iequals(out.name,"none") ||
       str_iequals(out.name,"initial") || str_iequals(out.name,"inherit") || str_iequals(out.name,"unset") ||
       str_iequals(out.name,"default") || str_iequals(out.name,"revert") || str_iequals(out.name,"revert-layer")) return out;

    std::array<std::map<std::string,double>,2> ends;
    css_component_lexer lexer(body.data(),body.size());
    auto next=[&](){css_variable_token t;do{t=lexer.next();}while(t.kind==css_variable_token_kind::whitespace||t.kind==css_variable_token_kind::comment);return t;};
    for(;;){auto selector=next();if(selector.kind==css_variable_token_kind::end)break;
      int which=-1;std::string text=body.substr(selector.begin,selector.end-selector.begin), ident;
      if(selector.kind==css_variable_token_kind::ident&&css_single_identifier(text,ident)){
          if(str_iequals(ident,"from"))which=0;else if(str_iequals(ident,"to"))which=1;
      } else if(text=="0%")which=0;else if(text=="100%")which=1;
      if(which<0)return out;
      auto open=next();if(open.delimiter!='{')return out;
      for(;;){auto prop=next();if(prop.delimiter=='}')break;if(prop.kind==css_variable_token_kind::end)return out;
        if(prop.delimiter==';')continue;
        std::string name;if(prop.kind!=css_variable_token_kind::ident||!css_single_identifier(body.substr(prop.begin,prop.end-prop.begin),name))return out;
        for(char& c:name)if(c>='A'&&c<='Z')c+=32;
        const auto colon=next();if(colon.delimiter!=':')return out;
        const size_t begin=colon.end;
        size_t end=begin;css_variable_token term;
        for(;;){term=lexer.next();if(term.kind==css_variable_token_kind::end)return out;
          if(term.delimiter==';'||term.delimiter=='}'){end=term.begin;break;}
          if(term.kind==css_variable_token_kind::open||term.kind==css_variable_token_kind::function)return out;
        }
        std::string value=body.substr(begin,end-begin);
        const bool important=css_strip_important(value);
        if(!important){
          std::vector<compiled_property_declaration_s> declarations;std::vector<custom_property_declaration_s> custom;std::vector<compiled_shorthand_declaration_s> shorthands;
          const std::string declaration=name+":"+value;
          if(style_source_compile_inline(declaration.c_str(),declarations,custom,shorthands,nullptr,nullptr)!=AETHER_OK||declarations.empty()||!custom.empty()||!shorthands.empty())return out;
          for(const auto& d:declarations){auto* reg=style_property_registry_get_global();const auto& property=reg->definitions[d.property_id].name;double number=0;
            if(d.value.mode!=compiled_value_mode::value)return out;
            if(property=="flex-direction") {if(!paused_keyframe_number(d.value.typed_value,false,number))return out;}
            else if(property=="order"||property=="flex-grow") {if(!paused_keyframe_number(value,false,number))return out;if(property=="order"&&(std::floor(number)!=number||number<INT32_MIN||number>INT32_MAX))return out;}
            else if(property=="flex-basis"||property=="row-gap"||property=="column-gap") {if(!paused_keyframe_number(d.value.typed_value,true,number)||number<0)return out;}
            else return out;
            ends[which][property]=number;
          }
        }
        if(term.delimiter=='}')break;
      }
    }
    if(ends[0].empty()||ends[0].size()!=ends[1].size())return out;
    for(const auto& entry:ends[0]){auto to=ends[1].find(entry.first);if(to==ends[1].end())return out;out.values.push_back({entry.first,entry.second,to->second});}
    out.profile_supported=true;return out;
}
