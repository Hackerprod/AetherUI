#pragma once
#include "identity_translate_z.h"
#include <cmath>
namespace aether::css {
// Source profile, not a general CSS parser. Retain lexical restrictions even
// when unsupported syntax would evaluate to an identity matrix.
inline bool bounded_translate_argument(const std::string& source) {
 css_variable_lexer lexer(source.data(),source.size());const auto token=lexer.next();
 if(token.begin!=0||token.end!=source.size())return false;
 if(token.serialization==css_variable_serialization::number)return identity_z_argument(source);
 if(token.serialization!=css_variable_serialization::dimension)return false;
 std::string unit;css_variable_lexer units(source.data()+token.numeric_end,source.size()-token.numeric_end);
 if(!units.decode_identifier([](void*p,const char*b,size_t n){static_cast<std::string*>(p)->append(b,n);return true;},&unit)||!aether::ascii_iequals(unit,"px"))return false;
 c_numeric_locale locale;const auto spelling=source.substr(0,token.numeric_end);const auto n=locale.parse_float(spelling);const auto d=locale.parse_double(spelling);
 return d.valid&&!d.range_error&&std::isfinite(d.value)&&std::abs(d.value)<=65536.0&&n.valid&&!n.range_error&&std::isfinite(n.value);
}
inline bool single_bounded_translate(const std::string& source) {
 css_component_cursor cursor(source);css_component_value function,extra;
 if(!cursor.next(function)||function.token.kind!=css_variable_token_kind::function)return false;
 std::string name;css_variable_lexer id(source.data()+function.begin,function.token.end-function.begin-1);
 if(!id.decode_identifier([](void*p,const char*b,size_t n){static_cast<std::string*>(p)->append(b,n);return true;},&name))return false;
 name=aether::ascii_lowercase(name);
 if(name!="translate"&&name!="translatex"&&name!="translatey")return false;
 const auto args=source.substr(function.token.end,function.end-function.token.end-1);
 css_component_cursor arguments(args);css_component_value arg;size_t count=0;bool expect_argument=true;
 while(arguments.next(arg)) {
  if(css_component_is_delimiter(arg,',')){if(expect_argument)return false;expect_argument=true;continue;}
  if(!expect_argument||!bounded_translate_argument(args.substr(arg.begin,arg.end-arg.begin)))return false;
  ++count;expect_argument=false;
 }
 return arguments.valid()&&!expect_argument&&count>=1&&count<=(name=="translate"?2u:1u)&&!cursor.next(extra)&&cursor.valid();
}
}
