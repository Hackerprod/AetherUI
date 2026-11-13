#pragma once
// Observation only, bounded and noexcept. Coordinates refer to extracted CSS
// bytes, never HTML offsets. Selectors/values are not echoed into the report.
namespace h16 {
inline size_t emitted=0;
inline std::string quote(std::string_view s){std::string out="\"";const char* hex="0123456789abcdef";
 for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c<32||c>=127){out+="\\x";out+=hex[c>>4];out+=hex[c&15];}else out+=c;}out+='"';return out;}

inline void failure(const std::string& source,const std::string& original,Failure f,
                    const h11::SourceMap* map=nullptr,const h5::Analysis* analysis=nullptr) noexcept {
 try {
  if(emitted>=256){if(emitted==256){++emitted;logstream<<"diagnostic_v1_incomplete=RECORD_LIMIT\n";}return;}++emitted;
  const std::string_view reason(f.reason);
  const char* category=reason.find("LIMIT")!=reason.npos||reason.find("WORK")!=reason.npos?"LIMIT_OR_ANALYSIS_FAILURE":
   reason.find("UNKNOWN")!=reason.npos||reason.find("LEXICAL")!=reason.npos?"ANALYSIS_FAILURE_UNCLASSIFIED":"PROFILE_REJECTION";
  if(analysis&&analysis->limit_failure)category="WORK_LIMIT";
  else if(analysis&&analysis->lexical_failure)category="LEXICAL_FAILURE";
  else if(reason=="CSS_CONTEXT_PROFILE_UNSUPPORTED")category="PROFILE_OR_STRUCTURE_FAILURE";
  size_t a=f.begin,z=f.end,sa=f.selector_begin,sz=f.selector_end;
  bool known=a!=SIZE_MAX&&z>=a;
  if(known&&map)known=map->locate(a,z,a,z);
  if(known)known=z<=original.size();
  bool selector=sa!=SIZE_MAX&&sz>=sa;
  if(selector&&map)selector=map->locate(sa,sz,sa,sz);
  if(selector)selector=sz<=original.size();
  if(known&&!selector&&analysis)for(const auto& span:analysis->declarations)if(a>=span.begin&&z<=span.end&&span.selector_begin!=SIZE_MAX){sa=span.selector_begin;sz=span.selector_end;selector=sz<=original.size();break;}
  std::string record="diagnostic_v1 source="+quote(std::string_view(source).substr(0,64))+" category="+category+" reason="+f.reason+" coverage=PARTIAL_FIRST_FAILURE_PER_CHECK offsets=UTF8_CSS_BYTES_HALF_OPEN";
  if(known){record+=" begin="+std::to_string(a)+" end="+std::to_string(z);
   // Only identify a property when the failure starts at its identifier followed
   // by colon. Function-token failures intentionally retain property=UNKNOWN.
   css_variable_budget budget{1024,false};css_variable_lexer lexer(original.data()+a,z-a,0,&budget);auto t=lexer.next();auto colon=lexer.next();
   while(colon.kind==css_variable_token_kind::whitespace||colon.kind==css_variable_token_kind::comment)colon=lexer.next();
   if(!budget.exhausted&&t.kind==css_variable_token_kind::ident&&colon.delimiter==':'){
    auto property=diagnostic::name(original,a+t.begin,a+t.end);
    record+=" property="+quote(property.starts_with("--")?"<custom-property>":property.substr(0,80));
   }else record+=" property=UNKNOWN";
  }else record+=" location=UNKNOWN property=UNKNOWN";
  if(selector)record+=" selector_begin="+std::to_string(sa)+" selector_end="+std::to_string(sz)+" selector_sha256="+avc::sha256(original.substr(sa,sz-sa));
  else record+=" selector=UNKNOWN";
  record+=" engine_support=UNVERIFIED\n";logstream<<record;
 }catch(...){logstream<<"diagnostic_v1_incomplete=ALLOCATION_OR_OUTPUT_FAILURE\n";}
}
}
