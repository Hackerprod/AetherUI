#pragma once
// A narrow source-context gate, not a CSS coverage certificate. Only tested
// declaration/value combinations can admit function-token offsets.
#include "../../native/src/css/contextual_math.h"
#include "../../native/src/style_runtime/css_component_values.h"
#include <set>
namespace h4 {
inline std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
inline std::string identifier(const std::string& source,size_t begin,size_t end){
 std::string result;css_variable_lexer l(source.data()+begin,end-begin);
 if(!l.decode_identifier([](void* p,const char* b,size_t n){static_cast<std::string*>(p)->append(b,n);return true;},&result))return {};
 return lower(result);
}
inline bool length(const std::string& s){
 aether::css::math_tree t;if(!aether::css::parse_contextual_math(s,t))return false;
 // Nonnegative literal restrictions are property syntax; negative intermediate
 // branches remain valid math, clamped at the final property used-value stage.
 css_component_cursor cursor(s);css_component_value c;if(!cursor.next(c)||!cursor.valid())return false;
 return c.token.kind==css_variable_token_kind::function || t->value>=0;
}
inline bool number(const std::string& s){
 css_variable_lexer l(s.data(),s.size());auto t=l.next();
 if(t.serialization!=css_variable_serialization::number||t.begin!=0||t.end!=s.size())return false;
 aether::css::c_numeric_locale locale;auto n=locale.parse_double(s);return n.valid&&!n.range_error&&n.value>=0;
}
inline bool value(const std::string& property,const std::string& source){
 bool valid=false;auto parts=css_split_components(source,&valid);if(!valid||parts.empty())return false;
 if(property=="padding") {if(parts.size()>4)return false;for(const auto& p:parts)if(!length(p))return false;return true;}
 if(property=="padding-top"||property=="padding-right"||property=="padding-bottom"||property=="padding-left"||property=="flex-basis")return parts.size()==1&&length(parts[0]);
 // Deliberately admit only explicit grow shrink basis, the tested H4 profile.
 if(property=="flex")return parts.size()==3&&number(parts[0])&&number(parts[1])&&length(parts[2]);
 return false;
}
inline std::set<size_t> admitted_functions(const std::string& source,bool sheet){
 std::vector<css_variable_token> ts;css_variable_budget budget{source.size()*8+1024,false};css_variable_lexer l(source.data(),source.size(),0,&budget);
 for(;;){auto t=l.next();if(t.kind==css_variable_token_kind::end)break;if(t.kind==css_variable_token_kind::error||budget.exhausted)return {};if(t.kind!=css_variable_token_kind::comment&&t.kind!=css_variable_token_kind::whitespace)ts.push_back(t);}
 auto delim=[&](size_t i,char c){return i<ts.size()&&ts[i].delimiter==c&&ts[i].serialization!=css_variable_serialization::string;};
 std::set<size_t> admitted;bool declarations=!sheet;size_t i=0;
 while(i<ts.size()){
  if(!declarations){while(i<ts.size()&&!delim(i,'{')) {if(delim(i,'}')||delim(i,';'))return {};++i;}if(i==ts.size())return {};++i;declarations=true;continue;}
  if(delim(i,';')){++i;continue;}
  if(delim(i,'}')){if(!sheet)return {};++i;declarations=false;continue;}
  if(ts[i].kind!=css_variable_token_kind::ident)return {};
  auto property=identifier(source,ts[i].begin,ts[i].end);++i;if(!delim(i,':'))return {};++i;
  const size_t start=i;std::vector<char> stack;
  while(i<ts.size()){
   const auto& t=ts[i];if(stack.empty()&&(delim(i,';')||delim(i,'}')))break;
   if(delim(i,'{'))return {};
   if(t.kind==css_variable_token_kind::function)stack.push_back(')');
   else if(t.kind==css_variable_token_kind::open)stack.push_back(t.delimiter=='('?')':t.delimiter=='['?']':'}');
   else if(t.kind==css_variable_token_kind::close){if(stack.empty()||stack.back()!=t.delimiter)return {};stack.pop_back();}
   ++i;
  }
  if(!stack.empty()||i==start)return {};
  size_t end=i;
  if(end>=start+2&&delim(end-2,'!')&&ts[end-1].kind==css_variable_token_kind::ident&&identifier(source,ts[end-1].begin,ts[end-1].end)=="important")end-=2;
  if(end==start)return {};
  if(value(property,source.substr(ts[start].begin,ts[end-1].end-ts[start].begin)))for(size_t j=start;j<end;++j)if(ts[j].kind==css_variable_token_kind::function)admitted.insert(ts[j].begin);
 }
 if(sheet&&declarations)return {};
 return admitted;
}
}
