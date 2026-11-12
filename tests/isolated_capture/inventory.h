#pragma once
// Observation only. This scanner never admits content or changes renderer support.
#include <iomanip>
#include <sstream>
#include <map>
namespace diagnostic {
inline std::string quoted(const std::string& s) {
 std::ostringstream o; o << '"';
 for(unsigned char c:s) {if(c=='"'||c=='\\')o << '\\' << c;else if(c<32||c>=127)o << "\\x" << std::hex << std::setw(2)<<std::setfill('0')<<unsigned(c);else o << c;}
 o << '"';return o.str();
}
inline std::string name(const std::string& s,size_t begin,size_t end) {
 std::string result;css_variable_budget b{max_bytes*4,false};
 css_variable_lexer l(s.data()+begin,end-begin,0,&b);
 if(!l.decode_identifier([](void* p,const char* bytes,size_t n){static_cast<std::string*>(p)->append(bytes,n);return true;},&result))return "<undecoded>";
 return result;
}
inline void css(const std::string& s,const std::string& source,std::set<std::string>& blockers) {
 logstream<<"inventory_source id="<<quoted(source)<<" bytes="<<s.size()<<" sha256="<<avc::sha256(s)<<"\n";
 css_variable_budget budget{max_bytes*4,false};css_variable_lexer lexer(s.data(),s.size(),0,&budget);
 size_t cursor=0,line=1,column=1;std::map<std::string,size_t> totals;
 for(;;){auto t=lexer.next();if(t.kind==css_variable_token_kind::end)break;
  if(budget.exhausted||t.kind==css_variable_token_kind::error){blockers.insert("CSS_LEXICAL_OR_WORK_LIMIT");logstream<<"inventory_incomplete="<<quoted(source)<<"\n";break;}
  const char* kind=nullptr;const char* reason=nullptr;size_t a=t.begin,z=t.end;
  if(t.serialization==css_variable_serialization::at_keyword){kind="at_rule";reason="CSS_AT_RULE_UNSUPPORTED";++a;}
  else if(t.kind==css_variable_token_kind::function){kind="function";reason="CSS_FUNCTION_REQUIRES_CONTEXT_GATE";if(z>a)--z;}
  else if(t.serialization==css_variable_serialization::url||t.serialization==css_variable_serialization::bad_url){kind="url";reason="CSS_RESOURCE_UNSUPPORTED";}
  if(!kind)continue;
  while(cursor<t.begin){if(s[cursor++]=='\n'){++line;column=1;}else ++column;}
  const std::string label=std::string(kind)=="url"?"url":name(s,a,z);
  ++totals[std::string(kind)+":"+label];if(t.kind!=css_variable_token_kind::function)blockers.insert(reason);
  logstream<<"inventory_css source="<<quoted(source)<<" offset="<<t.begin<<" length="<<t.end-t.begin<<" line="<<line<<" column="<<column<<" kind="<<kind<<" name="<<quoted(label)<<" reason="<<reason<<" context=LEXICAL_ONLY engine_support=UNVERIFIED\n";
 }
 for(const auto& [key,count]:totals)logstream<<"inventory_css_count source="<<quoted(source)<<" token="<<quoted(key)<<" count="<<count<<"\n";
}
inline void element(aether_node_s* n,size_t ordinal,const char* reason="RESOURCE_OR_SCRIPT_ELEMENT_UNSUPPORTED") {
 logstream<<"inventory_element dom_ordinal="<<ordinal<<" tag="<<quoted(n->tag_name)<<" reason="<<reason<<" source_location=UNAVAILABLE";
 for(const char* key:{"id","class","type","width","height"}){auto i=n->attributes.find(key);if(i!=n->attributes.end())logstream<<" "<<key<<"="<<quoted(i->second);}
 // Do not emit resource URLs or embedded data; preserve their identity via hash.
 auto src=n->attributes.find("src");if(src!=n->attributes.end())logstream<<" src_sha256="<<avc::sha256(src->second)<<" src_bytes="<<src->second.size();
 logstream<<" engine_support=UNVERIFIED\n";
}
}
