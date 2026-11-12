#pragma once
#include "h4_profile.h"
namespace h8 {
inline bool intrinsic(const std::string& value) {
 css_component_cursor cursor(value);css_component_value c,next;
 if(!cursor.next(c)||c.token.kind!=css_variable_token_kind::ident||cursor.next(next)||!cursor.valid())return false;
 const auto id=h4::identifier(value,c.token.begin,c.token.end);
 return id=="min-content"||id=="max-content"||id=="fit-content";
}
// Source checks are deliberately conservative, including inactive declarations.
// Core does not retain writing-mode/direction. Never infer horizontal authorship
// from its computed fallback. This is CLI admission, not general CSS support.
inline bool declaration(const std::string& property,const std::string& value) {
 if(property=="direction"||property=="writing-mode")return false;
 const bool sized=property=="width"||property=="min-width"||property=="max-width"||property=="flex-basis"||property=="flex";
 if(!sized) {
  if(property=="height"||property=="min-height"||property=="max-height") {
   bool valid=false;for(const auto& part:css_split_components(value,&valid))if(intrinsic(part))return false;
  }
  return true;
 }
 bool valid=false;auto parts=css_split_components(value,&valid);if(!valid)return false;
 bool keyword=false;
 css_variable_lexer lex(value.data(),value.size());
 for(;;){auto t=lex.next();if(t.kind==css_variable_token_kind::end)break;if(t.kind==css_variable_token_kind::error)return false;
  if(t.kind==css_variable_token_kind::ident&&intrinsic(value.substr(t.begin,t.end-t.begin)))keyword=true;
  if(t.kind==css_variable_token_kind::function){auto name=h4::identifier(value,t.begin,t.end-1);if(name=="fit-content"||name=="calc-size")return false;}
 }
 if(!keyword)return true; // Existing parsers/gates own non-H8 syntax.
 if(parts.size()==1&&intrinsic(parts[0]))return true;
 return property=="flex"&&parts.size()==3&&h4::number(parts[0])&&h4::number(parts[1])&&intrinsic(parts[2]);
}
}
