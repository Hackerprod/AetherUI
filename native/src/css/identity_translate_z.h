#pragma once
// Bounded 3D identity syntax. A lexical zero is required: conversion to float
// must never turn an underflowed nonzero Z into a supported identity.
#include "../style_runtime/css_component_values.h"
#include "../primitives/ascii_text.h"
#include "css_numeric_conversion_internal.h"
#include <string>
namespace aether::css {
inline bool identity_z_argument(const std::string& source) {
 css_variable_lexer lexer(source.data(),source.size()); const auto token=lexer.next();
 if(token.begin!=0||token.end!=source.size() ||
    (token.serialization!=css_variable_serialization::number&&token.serialization!=css_variable_serialization::dimension))return false;
 const auto end=token.serialization==css_variable_serialization::number?source.size():token.numeric_end;
 // Every digit in the significand must be zero (exponent digits don't matter).
 for(size_t i=0;i<end&&source[i]!='e'&&source[i]!='E';++i)if(source[i]>='1'&&source[i]<='9')return false;
 c_numeric_locale locale;const auto parsed=locale.parse_double(source.substr(0,end));
 if(!parsed.valid||parsed.range_error||parsed.value!=0)return false;
 if(token.serialization==css_variable_serialization::number)return true;
 std::string unit;css_variable_lexer units(source.data()+end,source.size()-end);
 if(!units.decode_identifier([](void*p,const char*b,size_t n){static_cast<std::string*>(p)->append(b,n);return true;},&unit))return false;
 return aether::ascii_iequals(unit,"px"); // other length units deliberately deferred
}
inline bool single_identity_translate_z(const std::string& source) {
 css_component_cursor cursor(source);css_component_value function,extra;
 if(!cursor.next(function)||function.token.kind!=css_variable_token_kind::function)return false;
 std::string name;const auto start=function.begin,end=function.token.end-1;
 css_variable_lexer id(source.data()+start,end-start);
 if(!id.decode_identifier([](void*p,const char*b,size_t n){static_cast<std::string*>(p)->append(b,n);return true;},&name)||!aether::ascii_iequals(name,"translatez"))return false;
 const auto args=source.substr(function.token.end,function.end-function.token.end-1);
 bool valid=false;auto parts=css_split_components(args,&valid);
 return valid&&parts.size()==1&&identity_z_argument(parts[0])&&!cursor.next(extra)&&cursor.valid();
}
}
