#include "test_support.hpp"
#include "aether_visual_compare/artifact_store.hpp"
#include "aether_visual_compare/report.hpp"
using namespace avc;
using namespace avc::test;
namespace {
bool accepted(const FakeFileSystem& f,const fs::path& d){return verify_committed(f.bytes(d/"manifest.json"),f.bytes(d/"report.json"),f.bytes(d/"report.log"));}
void no_images(const FakeFileSystem& f){for(const auto& [name,node]:f.nodes){(void)node;CHECK(name.find(".png")==std::string::npos);}}
}
int main(){
 auto d=fs::path(request().out);
 {FakeFileSystem f;CHECK(prepare_output(f,request()));CHECK(f.exists(d));CHECK(!prepare_output(f,request()));CHECK(f.count("create_directory:")==1);CHECK(f.count("open_write:")==0);}
 for(auto kind:{FileKind::symlink,FileKind::special,FileKind::regular,FileKind::missing,FileKind::error}){FakeFileSystem f;f.node(base(),kind);CHECK(!prepare_output(f,request()));CHECK(f.count("create_directory:")==0);CHECK(f.count("open_write:")==0);}
 {FakeFileSystem f;f.node(base().root_path(),FileKind::symlink);CHECK(!prepare_output(f,request()));CHECK(f.count("create_directory:")==0);}
 {FakeFileSystem f;auto q=request();q.out=(base()/"missing"/"result").generic_string();CHECK(!prepare_output(f,q));CHECK(f.count("create_directory:")==0);CHECK(!f.exists(base()/"missing"));}
 {FakeFileSystem f;auto q=request();q.out="//host/share/result";CHECK(!prepare_output(f,q));CHECK(f.events.empty());}
 {FakeFileSystem baseline;CHECK(prepare_output(baseline,request()));for(std::size_t n=1;n<=baseline.events.size();++n){FakeFileSystem f;f.fail_at=n;CHECK(!prepare_output(f,request()));CHECK(!f.exists(d));}}
 FakeFileSystem baseline;CHECK(prepare_output(baseline,request()));baseline.events.clear();auto good=report();CHECK(commit_report(baseline,good,{})==0);CHECK(accepted(baseline,d));CHECK(!baseline.events.empty());CHECK(baseline.events.back()=="rename:"+(d/"manifest.json.tmp").generic_string()+"->"+(d/"manifest.json").generic_string());no_images(baseline);
 auto json=baseline.bytes(d/"report.json"),log=baseline.bytes(d/"report.log"),marker=baseline.bytes(d/"manifest.json");
 CHECK(json.find("\"state\":\"requires_marker\"")!=std::string::npos);CHECK(json.find("\"status\":\"passed\"")!=std::string::npos);CHECK(!verify_committed("",json,log));CHECK(!verify_committed("{}",json,log));CHECK(!verify_committed(marker,json+" ",log));CHECK(!verify_committed(marker,json,log+"x"));
 {auto bad=marker;auto i=bad.find(good.run_id);CHECK(i!=std::string::npos);bad[i]=bad[i]=='0'?'1':'0';CHECK(!verify_committed(bad,json,log));}
 {auto bad=marker;auto i=bad.find("complete");CHECK(i!=std::string::npos);bad.replace(i,8,"partial");CHECK(!verify_committed(bad,json,log));}
 {auto bad=marker;auto i=bad.find("artifacts/1");CHECK(i!=std::string::npos);bad[i+10]='2';CHECK(!verify_committed(bad,json,log));}
 {auto bad=marker;bad.pop_back();bad.pop_back();CHECK(!verify_committed(bad,json,log));}
 {auto bad=marker;auto i=bad.find("passed");CHECK(i!=std::string::npos);bad.replace(i,6,"failed");CHECK(!verify_committed(bad,json,log));}
 // Fail every I/O point, including short write/flush/close, either report rename,
 // closed-output readback and every marker operation. No candidate certifies 0.
 bool saw_one_promoted=false,saw_both_promoted=false;
 for(std::size_t n=1;n<=baseline.events.size();++n){FakeFileSystem f;CHECK(prepare_output(f,request()));f.events.clear();f.fail_at=n;auto r=report();CHECK(commit_report(f,r,{})==13);CHECK(!accepted(f,d));CHECK(!f.exists(d/"manifest.json"));no_images(f);
  if(f.exists(d/"report.log")&&!f.exists(d/"report.json"))saw_one_promoted=true;
  if(f.exists(d/"report.log")&&f.exists(d/"report.json")){saw_both_promoted=true;CHECK(f.bytes(d/"report.json").find("\"status\":\"passed\"")!=std::string::npos);}
  CHECK(f.events.size()==n); // No automatic retry, repair, deletion or fallback.
 }
 CHECK(saw_one_promoted);CHECK(saw_both_promoted);
 {FakeFileSystem f;CHECK(prepare_output(f,request()));auto r=report();int samples=0;CHECK(commit_report(f,r,{[&]{++samples;return true;}})==12);CHECK(samples==1);CHECK(accepted(f,d));CHECK(f.bytes(d/"manifest.json").find("cancelled")!=std::string::npos);}
 {FakeFileSystem f;CHECK(prepare_output(f,request()));auto r=report();bool cancel=false;int samples=0;f.on_operation=[&](std::string_view op){if(op.starts_with("open_write:"))cancel=true;};CHECK(commit_report(f,r,{[&]{++samples;return cancel;}})==0);CHECK(cancel);CHECK(samples==1);CHECK(accepted(f,d));}
 {FakeFileSystem f;CHECK(prepare_output(f,request()));auto r=report(Command::capture);CHECK(commit_report(f,r,{})==11);CHECK(accepted(f,d));CHECK(f.bytes(d/"manifest.json").find("not_implemented")!=std::string::npos);no_images(f);}
 {FakeFileSystem f;CHECK(prepare_output(f,request()));f.events.clear();f.fail_at=1;auto r=report(Command::capture);CHECK(commit_report(f,r,{[]{return true;}})==13);CHECK(!accepted(f,d));}
 // A stale complete marker does not attest a different run or modified report.
 {auto changed=good;changed.run_id="fedcba9876543210fedcba9876543210";CHECK(!verify_committed(marker,serialize_report(changed),serialize_log(changed)));}
}
