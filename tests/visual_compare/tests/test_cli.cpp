#include "test_support.hpp"
#include "aether_visual_compare/cli.hpp"
#include "aether_visual_compare/artifact_store.hpp"
#include <sstream>
using namespace avc;
using namespace avc::test;
int main(){
 auto good=arguments();auto p=parse_cli(views(good));CHECK(p);CHECK(p->viewport.width==101);CHECK(p->viewport.pixel_width==126);CHECK(p->viewport.pixel_height==124);
 for(const auto& scale:std::vector<std::string>{"1","1.25","1.5","2"}){auto a=good;a[12]=scale;CHECK(parse_cli(views(a)));}
 for(const auto& value:std::vector<std::string>{"0x1","1x0","16385x1","1x16385","-1x2","+1x2","1X2","1x2x3","999999999999999999999x2","1.0x2","1x"}){auto a=good;a[10]=value;CHECK(!parse_cli(views(a)));}
 for(const auto& value:std::vector<std::string>{"0","1.0","1,25","NaN","inf","2.5","-1"}){auto a=good;a[12]=value;CHECK(!parse_cli(views(a)));}
 {auto a=good;a[10]="16384x16384";a[12]="2";auto q=parse_cli(views(a));CHECK(q&&q->viewport.pixel_width==32768);}
 {auto a=good;a[10]="1x1";a[12]="1.5";auto q=parse_cli(views(a));CHECK(q&&q->viewport.pixel_width==2);}
 std::vector<std::vector<std::string>> invalid{{},{"unknown"},{"--help","inspect"},{"--version","--html"}};
 {auto a=good;a.push_back("--html");a.push_back(a[2]);invalid.push_back(a);}
 {auto a=good;a[1]="--htm";invalid.push_back(a);}
 {auto a=good;a.pop_back();invalid.push_back(a);}
 {auto a=good;a[2]="https://invalid.example/input.html";invalid.push_back(a);}
 {auto a=good;a.push_back("--allow-network");invalid.push_back(a);}
 for(const auto& a:invalid){FakeFileSystem f;std::ostringstream out,err;int ids=0;CHECK(run_cli(views(a),f,{},[&]{++ids;return "0123456789abcdef0123456789abcdef";},out,err)==2);CHECK(f.events.empty());CHECK(ids==0);CHECK(!err.str().empty());CHECK(err.str().find("invalid.example")==std::string::npos);}
 for(const std::string flag:{"--help","--version"}){FakeFileSystem f;std::ostringstream out,err;std::vector<std::string> a{flag};int ids=0;CHECK(run_cli(views(a),f,{},[&]{++ids;return std::string{};},out,err)==0);CHECK(f.events.empty());CHECK(ids==0);CHECK(!out.str().empty());}
 {FakeFileSystem f;auto a=arguments(Command::capture);std::ostringstream out,err;int ids=0;CHECK(run_cli(views(a),f,{},[&]{++ids;return "0123456789abcdef0123456789abcdef";},out,err)==11);CHECK(ids==1);CHECK(f.count("canonical:")==0);
  for(const auto& event:f.events){CHECK(event.find("input.html")==std::string::npos);CHECK(event.find("resources.json")==std::string::npos);}
  auto dir=fs::path(request().out);auto json=f.bytes(dir/"report.json");CHECK(verify_committed(f.bytes(dir/"manifest.json"),json,f.bytes(dir/"report.log")));CHECK(json.find("NOT_IMPLEMENTED")!=std::string::npos);CHECK(json.find("\"sha256\":null")!=std::string::npos);CHECK(json.find("\"size_bytes\":null")!=std::string::npos);CHECK(json.find("\"manifest_sha256\":null")!=std::string::npos);for(const auto& [name,node]:f.nodes){(void)node;CHECK(name.find(".png")==std::string::npos);}}
 {FakeFileSystem f;inputs(f);std::ostringstream out,err;auto a=arguments();CHECK(run_cli(views(a),f,{},[]{return "0123456789abcdef0123456789abcdef";},out,err)==0);auto d=fs::path(request().out);CHECK(verify_committed(f.bytes(d/"manifest.json"),f.bytes(d/"report.json"),f.bytes(d/"report.log")));}
 {FakeFileSystem f;f.directory(request().out);std::ostringstream out,err;auto a=arguments(Command::capture);CHECK(run_cli(views(a),f,{},[]{return "0123456789abcdef0123456789abcdef";},out,err)==13);CHECK(f.count("open_read:")==0);CHECK(f.count("open_write:")==0);}
 {FakeFileSystem f;std::ostringstream out,err;auto a=arguments();CHECK(run_cli(views(a),f,{},[]{throw std::runtime_error("untrusted detail");return std::string{};},out,err)==14);CHECK(f.events.empty());CHECK(err.str().find("untrusted detail")==std::string::npos);}
 {FakeFileSystem f;std::ostringstream out,err;auto a=arguments();CHECK(run_cli(views(a),f,{},[]{return std::string("invalid-id");},out,err)==14);CHECK(f.events.empty());}

}
