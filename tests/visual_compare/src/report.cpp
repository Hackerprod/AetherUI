#include "aether_visual_compare/report.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
namespace avc {
namespace {
constexpr std::array<std::string_view,10> stage_names{"arguments","output_prepare","input_read","manifest_parse","resource_validation","aether_capture","webview2_capture","comparison","coverage","output_commit"};
constexpr std::array<std::string_view,6> statuses{"not_run","passed","failed","blocked","not_implemented","cancelled"};
constexpr std::array<std::string_view,19> reasons{"NOT_REQUESTED","OK","OUTSIDE_SCOPE","NOT_IMPLEMENTED","INVALID_ARGUMENTS","INPUT_UNREADABLE","INPUT_INVALID","INPUT_TOO_LARGE","INVALID_UTF8","INVALID_MANIFEST","RESOURCE_INVALID","RESOURCE_UNREADABLE","RESOURCE_MISMATCH","RESOURCE_LIMIT","INVALID_PATH","POLICY_BLOCKED","CANCELLED","OUTPUT_IO","INTERNAL_FAILURE"};
constexpr std::array<int,14> priority{13,14,10,12,2,3,4,5,6,7,8,9,11,0};
template<class T> bool contains(const T& values, std::string_view s) { return std::find(values.begin(),values.end(),s)!=values.end(); }
std::size_t rank(int c) { const auto p=std::find(priority.begin(),priority.end(),c); return static_cast<std::size_t>(p-priority.begin()); }
bool hex(std::string_view s,std::size_t n) { return s.size()==n && std::all_of(s.begin(),s.end(),[](char c){ return (c>='0'&&c<='9')||(c>='a'&&c<='f'); }); }
bool label(std::string_view s) { return s.size()==5 && s[0]=='r' && std::all_of(s.begin()+1,s.end(),[](char c){return c>='0'&&c<='9';}) && s!="r0000" && s<="r1024"; }
// All strings admitted here are closed ASCII tokens or hexadecimal digests.
// Escaping is still complete for JSON string controls and metacharacters.
std::string quote(std::string_view s) {
 std::string result="\""; constexpr char h[]="0123456789abcdef";
 for(unsigned char c:s) { if(c=='"'||c=='\\') {result+='\\';result+=static_cast<char>(c);} else if(c<32) {result+="\\u00";result+=h[c>>4];result+=h[c&15];} else result+=static_cast<char>(c); }
 return result+'"';
}
std::string nullable(const std::optional<std::string>& s) {return s?quote(*s):"null";}
struct Result {int code;std::string detail;};
Result result(const Report& r) {
 Result best{0,{}};
 if(r.request.command==Command::capture) best={11,"NOT_IMPLEMENTED"};
 else if(r.request.command!=Command::inspect || std::any_of(r.stages.begin(),r.stages.begin()+5,[](const Stage& s){return s.status!="passed";}) || r.stages[9].status!="passed") best={14,"INTERNAL_FAILURE"};
 // Incomplete inspection is expected on a recorded failure, not a second error.
 if(!r.errors.empty()) best={0,{}};
 for(const auto& e:r.errors) if(rank(e.code)<rank(best.code)) best={e.code,e.detail_code};
 if(r.request.command==Command::capture && rank(11)<rank(best.code)) best={11,"NOT_IMPLEMENTED"};
 return best;
}
}
std::string requested_scope(const Request& r) {return r.command==Command::capture?"visual_capture":"contract_inspection";}
void add_error(Report& r,Error error) {if(std::find(r.errors.begin(),r.errors.end(),error)==r.errors.end()) r.errors.push_back(std::move(error));}
int exit_code(const Report& r) {return result(r).code;}
std::string primary_error(const Report& r) {return result(r).detail;}
std::string overall_status(const Report& r) {
 switch(exit_code(r)) {case 0:return "passed";case 10:return "blocked";case 11:return "not_implemented";case 12:return "cancelled";default:return "failed";}
}
bool valid_report(const Report& r) {
 if(!hex(r.run_id,32) || (r.request.command!=Command::inspect && r.request.command!=Command::capture)) return false;
 const auto& v=r.request.viewport;
 if(v.width<1||v.width>16384||v.height<1||v.height>16384||!(v.scale_quarters==4||v.scale_quarters==5||v.scale_quarters==6||v.scale_quarters==8)) return false;
 if(v.pixel_width!=(v.width*v.scale_quarters+2)/4||v.pixel_height!=(v.height*v.scale_quarters+2)/4||v.pixel_width>32768||v.pixel_height>32768) return false;
 if((r.input_sha256&&!hex(*r.input_sha256,64))||(r.manifest_sha256&&!hex(*r.manifest_sha256,64))||(r.input_size&&*r.input_size>2097152)) return false;
 if(r.input_sha256.has_value()!=r.input_size.has_value()) return false;
 if(exit_code(r)==0 && (!r.input_sha256||!r.input_size||!r.manifest_sha256)) return false;
 if(r.host_inspection_wall_ms&&(!std::isfinite(*r.host_inspection_wall_ms)||*r.host_inspection_wall_ms<0)) return false;
 for(std::size_t i=0;i<r.stages.size();++i) if(r.stages[i].name!=stage_names[i]||!contains(statuses,r.stages[i].status)||!contains(reasons,r.stages[i].reason_code)) return false;
 if(r.stages[0].status!="passed"||r.stages[1].status!="passed") return false;
 if(r.stages[9].status!="passed" && !(r.stages[9].status=="cancelled" && r.stages[9].reason_code=="CANCELLED" && std::any_of(r.errors.begin(),r.errors.end(),[](const Error& e){return e.code==12;}))) return false;
 for(std::size_t i=5;i<9;++i) if(r.stages[i].status!="not_run" && !(i==5 && r.request.command==Command::capture && r.stages[i].status=="not_implemented" && r.stages[i].reason_code=="NOT_IMPLEMENTED")) return false;
 if(r.request.command==Command::capture) {
  if(r.input_sha256||r.manifest_sha256||r.input_size) return false;
  for(std::size_t i=2;i<5;++i) if(r.stages[i].status!="not_run") return false;
 }
 for(std::size_t i=0;i<r.errors.size();++i) {
  const auto& e=r.errors[i];
  if(e.code==0||rank(e.code)==priority.size()||!contains(stage_names,e.stage)||!contains(reasons,e.detail_code)||(e.resource_label&&!label(*e.resource_label))) return false;
  if(std::find(r.errors.begin(),r.errors.begin()+static_cast<std::ptrdiff_t>(i),e)!=r.errors.begin()+static_cast<std::ptrdiff_t>(i)) return false;
 }
 return true;
}
std::string serialize_report(const Report& r) {
 if(!valid_report(r)) throw std::invalid_argument("INVALID_REPORT");
 std::ostringstream o; o.imbue(std::locale::classic()); o<<std::setprecision(17);
 o<<"{\n  \"schema_version\":\"aether.visual-compare.report/1\",\n  \"tool\":{\"name\":\"aether_visual_compare\",\"implementation\":\"contract-only\",\"version\":\"0.1.0-contract\"},\n  \"run_id\":"<<quote(r.run_id)<<",\n  \"requested_scope\":"<<quote(requested_scope(r.request))<<",\n  \"mode\":\"strict\",\n  \"input\":{\"label\":\"input.html\",\"sha256\":"<<nullable(r.input_sha256)<<",\"size_bytes\":";
 if(r.input_size)o<<*r.input_size;else o<<"null";
 const auto& v=r.request.viewport;
 o<<",\"manifest_sha256\":"<<nullable(r.manifest_sha256)<<"},\n  \"viewport\":{\"css_width\":"<<v.width<<",\"css_height\":"<<v.height<<",\"scale\":"<<v.scale_quarters/4.0<<",\"requested_pixel_width\":"<<v.pixel_width<<",\"requested_pixel_height\":"<<v.pixel_height<<",\"effective\":null},\n  \"stages\":[";
 for(std::size_t i=0;i<r.stages.size();++i) {const auto& s=r.stages[i];if(i)o<<',';o<<"{\"name\":"<<quote(s.name)<<",\"status\":"<<quote(s.status)<<",\"reason_code\":"<<quote(s.reason_code)<<'}';}
 constexpr auto engine="{\"implementation_status\":\"not_implemented\",\"runtime_version\":null,\"capture_status\":\"not_run\",\"content_status\":\"not_evaluated\",\"metrics\":null}";
 o<<"],\n  \"engines\":{\"aether\":"<<engine<<",\"webview2\":"<<engine<<"},\n  \"analysis_limits\":{\"references_complete\":\"not_evaluated\",\"resource_decode\":\"not_implemented\",\"font_availability\":\"not_evaluated\",\"filesystem_race_hardening\":\"not_implemented\"},\n  \"comparison\":{\"status\":\"not_run\",\"metrics\":null},\n  \"coverage\":{\"status\":\"not_evaluated\",\"expected_cases\":null,\"evaluated_cases\":null},\n  \"timings\":{\"host_inspection_wall_ms\":";
 if(r.host_inspection_wall_ms)o<<*r.host_inspection_wall_ms;else o<<"null";
 o<<",\"clock\":\"steady_clock\"},\n  \"errors\":[";
 for(std::size_t i=0;i<r.errors.size();++i) {const auto& e=r.errors[i];if(i)o<<',';o<<"{\"code\":"<<e.code<<",\"stage\":"<<quote(e.stage)<<",\"resource_label\":"<<nullable(e.resource_label)<<",\"detail_code\":"<<quote(e.detail_code)<<'}';}
 const auto primary=primary_error(r);
 o<<"],\n  \"overall\":{\"status\":"<<quote(overall_status(r))<<",\"primary_error\":"<<(primary.empty()?"null":quote(primary))<<",\"exit_code\":"<<exit_code(r)<<"},\n  \"artifacts\":[],\n  \"commit\":{\"state\":\"requires_marker\",\"marker\":\"manifest.json\"}\n}\n";
 return o.str();
}
std::string serialize_log(const Report& r) {
 if(!valid_report(r)) throw std::invalid_argument("INVALID_REPORT");
 std::ostringstream o; o.imbue(std::locale::classic());
 o<<"aether_visual_compare 0.1.0-contract (contract-only)\nrun_id="<<r.run_id<<"\nrequested_scope="<<requested_scope(r.request)<<"\nmode=strict\ninput.label=input.html\ninput.sha256="<<(r.input_sha256?*r.input_sha256:"unavailable")<<"\ninput.size_bytes="<<(r.input_size?std::to_string(*r.input_size):"unavailable")<<"\ninput.manifest_sha256="<<(r.manifest_sha256?*r.manifest_sha256:"unavailable")<<"\nviewport.css="<<r.request.viewport.width<<'x'<<r.request.viewport.height<<"\nviewport.scale="<<r.request.viewport.scale_quarters/4.0<<"\nviewport.requested_pixels="<<r.request.viewport.pixel_width<<'x'<<r.request.viewport.pixel_height<<"\nviewport.effective=unavailable\n";
 for(const auto& s:r.stages)o<<"stage."<<s.name<<'='<<s.status<<" reason="<<s.reason_code<<'\n';
 o<<"aether.implementation=not_implemented; capture=not_run; content=not_evaluated; runtime_version=unavailable; metrics=unavailable\nwebview2.implementation=not_implemented; capture=not_run; content=not_evaluated; runtime_version=unavailable; metrics=unavailable\nreferences_complete=not_evaluated\nresource_decode=not_implemented\nfont_availability=not_evaluated\nfilesystem_race_hardening=not_implemented\ncomparison=not_run; metrics=unavailable\ncoverage=not_evaluated; expected_cases=unavailable; evaluated_cases=unavailable\nhost_inspection_wall_ms=";
 if(r.host_inspection_wall_ms)o<<std::setprecision(17)<<*r.host_inspection_wall_ms;else o<<"unavailable";
 o<<"\nclock=steady_clock\n";
 for(const auto& e:r.errors)o<<"error="<<e.code<<" stage="<<e.stage<<" resource="<<(e.resource_label?*e.resource_label:"unavailable")<<" detail="<<e.detail_code<<'\n';
 o<<"overall="<<overall_status(r)<<" primary_error="<<(primary_error(r).empty()?"none":primary_error(r))<<" exit_code="<<exit_code(r)<<"\nartifacts=none\ncommit=requires_marker; marker=manifest.json\nThis report is a candidate until the complete marker and both report hashes are verified.\n";
 return o.str();
}
}
