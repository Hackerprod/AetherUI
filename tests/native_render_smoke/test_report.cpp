#ifdef NDEBUG
#error "Assertions required"
#endif
#include "report.h"
#include <cassert>
#include <limits>
int main() {
 namespace sm=aether::smoke;
 sm::Report r;r.run_id="contract-test-1";r.provenance=sm::Provenance::synthetic_test;
 assert(sm::coherent(r)&&sm::overall(r)==sm::Status::skip&&!sm::accepts_real_smoke(r));
 for(auto& p:r.phases) {p={sm::Status::pass,"",0};}
 r.phases[0].duration_ns.reset();
 r.text_width=20;r.text_height=10;r.text_baseline=-1;
 assert(sm::coherent(r)&&sm::overall(r)==sm::Status::pass&&!sm::accepts_real_smoke(r));
 auto output=sm::serialize(r);assert(output.find("metrics.provenance=synthetic_test")!=output.npos);
 assert(output.find("acceptance.real_smoke=NOT_ACCEPTED")!=output.npos);
 // A model of measured state exercises acceptance logic, not evidence of rendering.
 auto measured=r;measured.provenance=sm::Provenance::measured;
 assert(sm::accepts_real_smoke(measured));
 auto invalid=measured;invalid.provenance=sm::Provenance::not_measured;assert(!sm::coherent(invalid));
 invalid=measured;invalid.phases[0].duration_ns=1;assert(!sm::coherent(invalid));
 invalid=measured;invalid.phases[2].status=sm::Status::skip;invalid.phases[2].reason="not_run";
 assert(!sm::coherent(invalid));invalid.phases[2].duration_ns.reset();assert(!sm::coherent(invalid));
 invalid.phases[3]={sm::Status::skip,"dependency_failed",{}};
 assert(sm::coherent(invalid)&&!sm::accepts_real_smoke(invalid));
 invalid=measured;invalid.phases[1].duration_ns=-1;assert(!sm::coherent(invalid));
 invalid=measured;invalid.phases[1].duration_ns.reset();assert(!sm::coherent(invalid));
 invalid.phases[1].reason="clock_unavailable";assert(sm::coherent(invalid));
 invalid=measured;invalid.phases[7]={sm::Status::fail,"decode_failed\ninjected=PASS",4};
 assert(sm::overall(invalid)==sm::Status::fail&&!sm::accepts_real_smoke(invalid));
 output=sm::serialize(invalid);assert(output.find("\ninjected=PASS")==output.npos);
 assert(output.find("decode_failed\\ninjected\\=PASS")!=output.npos);
 invalid=measured;invalid.text_width=std::numeric_limits<double>::quiet_NaN();assert(!sm::coherent(invalid));
 invalid=measured;invalid.text_width.reset();assert(!sm::coherent(invalid));
 assert(!sm::valid_run_id("../old")&&!sm::valid_run_id("")&&!sm::valid_run_id(std::string(65,'a')));
 assert(sm::valid_run_id("abc_123-X"));
 // No files are written: in particular, no synthetic report.log is produced.
}
