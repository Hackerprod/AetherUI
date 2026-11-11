#include "aether_visual_compare/report.hpp"
#include "test_support.hpp"
#include <limits>
#include <stdexcept>
using namespace avc;
namespace {
Report completed() {
 Report r; r.run_id=std::string(32,'a');r.request.command=Command::inspect;
 r.input_sha256=std::string(64,'0');r.manifest_sha256=std::string(64,'0');r.input_size=0;
 for(std::size_t i=0;i<5;++i)r.stages[i]={r.stages[i].name,"passed","OK"};
 for(std::size_t i=5;i<9;++i)r.stages[i]={r.stages[i].name,"not_run","OUTSIDE_SCOPE"};
 r.stages[9]={"output_commit","passed","OK"};return r;
}
void rejected(const Report& r) {
 CHECK(!valid_report(r));bool threw=false;
 try{(void)serialize_report(r);}catch(const std::invalid_argument&){threw=true;}CHECK(threw);
}
}
int main() {
 auto r=completed(); CHECK(valid_report(r));CHECK(exit_code(r)==0);CHECK(primary_error(r).empty());
 r.request.html="sensitive-input-path";r.request.manifest="sensitive-manifest-path";
 const auto json=serialize_report(r),log=serialize_log(r);
 CHECK(json.find("sensitive")==std::string::npos&&log.find("sensitive")==std::string::npos);
 CHECK(json.ends_with("\n")&&json.find('\r')==std::string::npos);
 CHECK(json.find("\"metrics\":null")!=std::string::npos);
 CHECK(json.find("\"runtime_version\":null")!=std::string::npos);
 CHECK(json.find("\"state\":\"requires_marker\"")!=std::string::npos);
 CHECK(json.find("\"artifacts\":[]")!=std::string::npos);
 CHECK(json.find("403")==std::string::npos&&json.find("layout_ms")==std::string::npos);
 CHECK(log.find("unavailable")!=std::string::npos);
 // This candidate's passed status does not constitute a committed result.
 CHECK(json.find("\"status\":\"passed\"")!=std::string::npos);
 CHECK(json.find("\"commit_state\":\"complete\"")==std::string::npos);
 auto invalid=r;invalid.run_id="path/private";rejected(invalid);
 invalid=r;invalid.host_inspection_wall_ms=std::numeric_limits<double>::infinity();rejected(invalid);
 invalid=r;invalid.host_inspection_wall_ms=std::numeric_limits<double>::quiet_NaN();rejected(invalid);
 invalid=r;invalid.host_inspection_wall_ms=-1;rejected(invalid);
 invalid=r;invalid.request.viewport.scale_quarters=7;rejected(invalid);
 invalid=r;invalid.request.viewport.pixel_width=2;rejected(invalid);
 invalid=r;invalid.input_sha256="private";rejected(invalid);
 invalid=r;invalid.stages[0].reason_code="private text";rejected(invalid);
 invalid=r;invalid.stages[0].name="private path";rejected(invalid);
 invalid=r;invalid.stages[5].status="passed";rejected(invalid);
 invalid=r;add_error(invalid,{4,"input_read","INPUT_UNREADABLE",std::string("private")});rejected(invalid);
 invalid=r;add_error(invalid,{4,"input_read","INPUT_UNREADABLE",std::string("r1025")});rejected(invalid);
 invalid=r;add_error(invalid,{4,"input_read","INPUT_UNREADABLE",std::string("r0000")});rejected(invalid);
 auto failed=r;failed.stages[2]={"input_read","failed","INPUT_UNREADABLE"};
 failed.stages[3]={"manifest_parse","not_run","NOT_REQUESTED"};failed.stages[4]={"resource_validation","not_run","NOT_REQUESTED"};
 add_error(failed,{4,"input_read","INPUT_UNREADABLE",{}});CHECK(exit_code(failed)==4);
 add_error(failed,{4,"input_read","INPUT_UNREADABLE",{}});CHECK(failed.errors.size()==1);
 add_error(failed,{12,"output_commit","CANCELLED",{}});CHECK(exit_code(failed)==12);
 add_error(failed,{10,"input_read","POLICY_BLOCKED",{}});CHECK(exit_code(failed)==10);
 add_error(failed,{14,"output_commit","INTERNAL_FAILURE",{}});CHECK(exit_code(failed)==14);
 add_error(failed,{13,"output_commit","OUTPUT_IO",{}});CHECK(exit_code(failed)==13);
 CHECK(primary_error(failed)=="OUTPUT_IO"&&overall_status(failed)=="failed");
 CHECK(failed.errors.front().code==4);CHECK(valid_report(failed));
 // Reserved codes retain their deterministic ordering without implying engines ran.
 constexpr int order[]{13,14,10,12,2,3,4,5,6,7,8,9,11};
 for(std::size_t i=0;i<13;++i)for(std::size_t j=i;j<13;++j){auto q=r;add_error(q,{order[j],"output_commit","INTERNAL_FAILURE",{}});add_error(q,{order[i],"input_read","INPUT_INVALID",{}});CHECK(exit_code(q)==order[i]);}
 invalid=r;invalid.input_sha256.reset();rejected(invalid);
 invalid=r;invalid.manifest_sha256.reset();rejected(invalid);
 auto incomplete=completed();incomplete.stages[3].status="not_run";CHECK(exit_code(incomplete)==14);
 auto c=completed();c.request.command=Command::capture;c.input_sha256.reset();c.manifest_sha256.reset();c.input_size.reset();
 for(std::size_t i=2;i<5;++i)c.stages[i]={c.stages[i].name,"not_run","NOT_REQUESTED"};
 c.stages[5]={"aether_capture","not_implemented","NOT_IMPLEMENTED"};
 add_error(c,{11,"aether_capture","NOT_IMPLEMENTED",{}});
 CHECK(valid_report(c)&&exit_code(c)==11&&overall_status(c)=="not_implemented");
 CHECK(serialize_report(c).find("\"sha256\":null")!=std::string::npos);
 c.stages[9]={"output_commit","cancelled","CANCELLED"};add_error(c,{12,"output_commit","CANCELLED",{}});
 CHECK(valid_report(c)&&exit_code(c)==12&&overall_status(c)=="cancelled");
 invalid=c;invalid.input_size=0;rejected(invalid);
 auto tied=r;add_error(tied,{4,"input_read","INPUT_INVALID",{}});add_error(tied,{4,"manifest_parse","INVALID_MANIFEST",{}});CHECK(primary_error(tied)=="INPUT_INVALID");
 return 0;
}
