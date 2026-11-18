#include "test_support.hpp"
#include "aether_visual_compare/artifact_store.hpp"
#include "aether_visual_compare/report.hpp"
#include "aether_visual_compare/native_capture.hpp"
using namespace avc;
using namespace avc::test;
Report sample(){
 auto r=report();r.request.command=Command::capture;r.request.backend="native-b0-source";r.request.profile=0;r.request.viewport={2,2,4,2,2};
 r.stages[5]={"aether_capture","passed","OK"};
 B0CaptureMetadata n;n.abi_version=1;n.runtime_version="0.1.0";n.public_status=0;n.capabilities=7;n.width=2;n.height=2;n.stride=8;n.sequence=1;n.regular_sha256=std::string(64,'a');n.bold_sha256=std::string(64,'b');r.b0=n;
 std::vector<std::uint8_t> pixels(16,255);r.png_bytes=encode_native_png(2,2,pixels);return r;
}
int main(){
 auto r=sample();CHECK(valid_report(r));CHECK(exit_code(r)==0);
 FakeFileSystem f;CHECK(prepare_output(f,r.request));f.events.clear();CHECK(commit_report(f,r,{})==0);const auto d=fs::path(r.request.out);
 auto m=f.bytes(d/"manifest.json"),j=f.bytes(d/"report.json"),l=f.bytes(d/"report.log"),p=f.bytes(d/"aether.png");
 CHECK(j.find("report/3")!=j.npos);CHECK(m.find("artifacts/3")!=m.npos);CHECK(j.find("native_b0_source_capture")!=j.npos);CHECK(j.find("limitations")==j.npos);CHECK(verify_committed(m,j,l,p));
 // Exercise every filesystem publication fault with real v3 PNG/report shape.
 for(std::size_t n=1;n<=f.events.size();++n) {
  FakeFileSystem broken;auto failed=sample();CHECK(prepare_output(broken,failed.request));broken.events.clear();broken.fail_at=n;
  const auto code=commit_report(broken,failed,{});
  CHECK(code==13);CHECK(!broken.exists(d/"manifest.json"));
  CHECK(!verify_committed(broken.bytes(d/"manifest.json"),broken.bytes(d/"report.json"),broken.bytes(d/"report.log"),broken.bytes(d/"aether.png")));
 }
 {FakeFileSystem cancelled;auto failed=sample();CHECK(prepare_output(cancelled,failed.request));CHECK(commit_report(cancelled,failed,{[]{return true;}})==12);CHECK(!cancelled.exists(d/"aether.png"));CHECK(failed.png_bytes.empty());CHECK(verify_committed(cancelled.bytes(d/"manifest.json"),cancelled.bytes(d/"report.json"),cancelled.bytes(d/"report.log")));}
 CHECK(!verify_committed(m,j,l));CHECK(!verify_committed(m,j+" ",l,p));CHECK(!verify_committed(m,j,l+"x",p));CHECK(!verify_committed(m,j,l,p+"x"));
 for(auto pair:{std::pair{"report/3","report/2"},std::pair{"native_b0_source_capture","native_profile_capture"},std::pair{"0123456789abcdef0123456789abcdef","fedcba9876543210fedcba9876543210"}}){auto bad=j;auto pos=bad.find(pair.first);CHECK(pos!=bad.npos);bad.replace(pos,std::strlen(pair.first),pair.second);CHECK(!verify_committed(m,bad,l,p));}
 {auto bad=m;bad.replace(bad.find("artifacts/3"),11,"artifacts/2");CHECK(!verify_committed(bad,j,l,p));}
 {auto bad=r;bad.request.backend="native-html-ltr";bad.request.profile=1;CHECK(!valid_report(bad));}
 {auto bad=r;bad.request.backend="";CHECK(!valid_report(bad));}
 {auto bad=r;bad.native=NativeCaptureMetadata{};CHECK(!valid_report(bad));}
 {auto bad=r;bad.b0->public_status.reset();CHECK(!valid_report(bad));}
 {auto bad=r;bad.b0->capabilities=3;CHECK(!valid_report(bad));}
 {auto good=r;good.b0->capabilities=15;CHECK(valid_report(good));}
 {auto bad=r;bad.b0->diagnostic=B0Diagnostic{};bad.b0->diagnostic->node_ordinal=UINT64_MAX;CHECK(!valid_report(bad));}
 {auto bad=r;bad.b0->diagnostic=B0Diagnostic{};bad.b0->diagnostic->stage=10;CHECK(!valid_report(bad));}
 {auto bad=r;bad.b0->diagnostic=B0Diagnostic{};bad.b0->diagnostic->status=-1;CHECK(!valid_report(bad));}
 {auto good=r;good.b0->diagnostic=B0Diagnostic{};CHECK(valid_report(good));auto json=serialize_report(good);CHECK(json.find("\"node_ordinal\":null")!=json.npos);}
 // Reject malformed PNG even if callers attempt to rehash/recommit it.
 for(std::size_t i=0;i<p.size();++i){auto bad=r;bad.png_bytes[i]^=1;CHECK(!valid_report(bad));}
 {auto bad=r;bad.png_bytes="fake PNG";CHECK(!valid_report(bad));}
 {auto bad=r;bad.png_bytes+=p;CHECK(!valid_report(bad));}
 // Failure before ABI has no synthetic status/diagnostic.
 {auto failed=r;failed.b0.reset();failed.png_bytes.clear();failed.stages[5]={"aether_capture","blocked","NATIVE_UNSUPPORTED"};add_error(failed,{10,"aether_capture","NATIVE_UNSUPPORTED",{}});CHECK(valid_report(failed));CHECK(serialize_report(failed).find("\"metadata\":null")!=std::string::npos);}
}
