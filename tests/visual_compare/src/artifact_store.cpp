#include "aether_visual_compare/artifact_store.hpp"
#include "aether_visual_compare/report.hpp"
#include "aether_visual_compare/resource_manifest.hpp"
#include "sha256.hpp"
#include <charconv>
#include <array>
#include <cmath>
#include <stdexcept>
namespace avc {
namespace {
bool write_closed(FileSystem& f,const fs::path& p,const std::string& bytes) {
 auto w=f.open_write(p);return w&&w->write(bytes)&&w->flush()&&w->close();
}
std::optional<std::string> read_closed(FileSystem& f,const fs::path& p,std::size_t expected) {
 auto info=f.stat(p);if(info.kind!=FileKind::regular||info.size!=expected)return {};
 auto reader=f.open_read(p);if(!reader)return {};
 std::string bytes;std::array<char,16384> buffer{};
 for(;;){std::size_t n=0;if(!reader->read(buffer.data(),buffer.size(),n)||n>buffer.size()||n>expected-bytes.size())return {};
  bytes.append(buffer.data(),n);if(n==0)break;}
 if(bytes.size()!=expected)return {};
 return bytes;
}
std::string marker_bytes(const Report& r,std::string_view log,std::string_view json) {
 return "{\"schema_version\":\"aether.visual-compare.artifacts/1\",\"commit_state\":\"complete\",\"run_id\":\""+r.run_id+
 "\",\"requested_scope\":\""+requested_scope(r.request)+"\",\"overall_status\":\""+overall_status(r)+"\",\"exit_code\":"+std::to_string(exit_code(r))+
 ",\"artifacts\":[{\"name\":\"report.log\",\"size_bytes\":"+std::to_string(log.size())+",\"sha256\":\""+sha256(log)+
 "\"},{\"name\":\"report.json\",\"size_bytes\":"+std::to_string(json.size())+",\"sha256\":\""+sha256(json)+"\"}]}\n";
}
const Json& get(const Json& o,std::string_view key) {auto p=o.get(key);if(!p)throw std::invalid_argument("INVALID_MANIFEST");return *p;}
std::string string(const Json& o) {if(o.kind!=Json::Kind::string)throw std::invalid_argument("INVALID_MANIFEST");return o.scalar;}
template<class T> T number(const Json& o) {if(o.kind!=Json::Kind::number)throw std::invalid_argument("INVALID_MANIFEST");T n{};auto [p,e]=std::from_chars(o.scalar.data(),o.scalar.data()+o.scalar.size(),n);if(e!=std::errc{}||p!=o.scalar.data()+o.scalar.size())throw std::invalid_argument("INVALID_MANIFEST");return n;}
std::optional<std::string> nullable_string(const Json& o) {if(o.kind==Json::Kind::null_value)return {};return string(o);}
Report restore_report(const Json& j) {
 Report r;r.run_id=string(get(j,"run_id"));
 auto scope=string(get(j,"requested_scope"));if(scope=="contract_inspection")r.request.command=Command::inspect;else if(scope=="visual_capture")r.request.command=Command::capture;else throw std::invalid_argument("INVALID_MANIFEST");
 auto& input=get(j,"input");r.input_sha256=nullable_string(get(input,"sha256"));r.manifest_sha256=nullable_string(get(input,"manifest_sha256"));
 auto& size=get(input,"size_bytes");if(size.kind!=Json::Kind::null_value)r.input_size=number<std::uint64_t>(size);
 auto& v=get(j,"viewport");r.request.viewport.width=number<unsigned>(get(v,"css_width"));r.request.viewport.height=number<unsigned>(get(v,"css_height"));
 const double scale=number<double>(get(v,"scale"));if(scale!=1&&scale!=1.25&&scale!=1.5&&scale!=2)throw std::invalid_argument("INVALID_MANIFEST");r.request.viewport.scale_quarters=static_cast<unsigned>(scale*4);
 r.request.viewport.pixel_width=number<unsigned>(get(v,"requested_pixel_width"));r.request.viewport.pixel_height=number<unsigned>(get(v,"requested_pixel_height"));
 auto& stages=get(j,"stages");if(stages.kind!=Json::Kind::array||stages.array.size()!=r.stages.size())throw std::invalid_argument("INVALID_MANIFEST");
 for(std::size_t i=0;i<r.stages.size();++i){const auto& s=stages.array[i];r.stages[i]={string(get(s,"name")),string(get(s,"status")),string(get(s,"reason_code"))};}
 const auto& time=get(get(j,"timings"),"host_inspection_wall_ms");if(time.kind!=Json::Kind::null_value)r.host_inspection_wall_ms=number<double>(time);
 const auto& errors=get(j,"errors");if(errors.kind!=Json::Kind::array)throw std::invalid_argument("INVALID_MANIFEST");
 for(const auto& e:errors.array)r.errors.push_back({number<int>(get(e,"code")),string(get(e,"stage")),string(get(e,"detail_code")),nullable_string(get(e,"resource_label"))});
 return r;
}
}
bool prepare_output(FileSystem& f,const Request& q) {
 try {if(!valid_local_path(q.out))return false;fs::path p(q.out);
  if(p.filename().empty()||!directory_chain(f,p.parent_path()))return false;
  if(f.stat(p).kind!=FileKind::missing)return false;
  return f.create_directory(p);
 }catch(...){return false;}
}
int commit_report(FileSystem& f,Report& r,const CancellationToken& token) {
 // This is the only final cancellation sample. Publication has no later samples.
 if(token.cancelled()){
  add_error(r,{12,"output_commit","CANCELLED",{}});r.stages[9].status="cancelled";r.stages[9].reason_code="CANCELLED";
 }else{r.stages[9].status="passed";r.stages[9].reason_code="OK";}
 std::string log,json,marker;
 try {log=serialize_log(r);json=serialize_report(r);}
 catch(...){return 14;}
 try {
  const fs::path dir(r.request.out);
  if(!write_closed(f,dir/"report.log.tmp",log)||!write_closed(f,dir/"report.json.tmp",json))return 13;
  if(!f.rename(dir/"report.log.tmp",dir/"report.log"))return 13;
  if(!f.rename(dir/"report.json.tmp",dir/"report.json"))return 13;
  auto closed_log=read_closed(f,dir/"report.log",log.size());
  if(!closed_log||*closed_log!=log)return 13;
  auto closed_json=read_closed(f,dir/"report.json",json.size());
  if(!closed_json||*closed_json!=json)return 13;
  marker=marker_bytes(r,*closed_log,*closed_json);
  if(!write_closed(f,dir/"manifest.json.tmp",marker))return 13;
  if(!f.rename(dir/"manifest.json.tmp",dir/"manifest.json"))return 13;
  // Marker rename is the last filesystem call. Do not inspect, repair or clean up.
  return exit_code(r);
 }catch(...){return 13;}
}
bool verify_committed(std::string_view marker,std::string_view json,std::string_view log) {
 try {
  auto parsed=parse_json(json),m=parse_json(marker);if(!parsed||!m)return false;
  auto r=restore_report(*parsed);if(!valid_report(r))return false;
  // Closed v1 canonical serialization validates every field, including unknown
  // keys, fixed engine nulls and scope semantics. Noncanonical encodings reject.
  return serialize_report(r)==json&&serialize_log(r)==log&&marker_bytes(r,log,json)==marker;
 }catch(...){return false;}
}
}
