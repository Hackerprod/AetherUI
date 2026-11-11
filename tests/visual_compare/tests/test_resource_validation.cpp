#include "test_support.hpp"
#include "aether_visual_compare/resource_manifest.hpp"
#include "sha256.hpp"
using namespace avc;
using namespace avc::test;
namespace {
std::string manifest(std::string path,std::uint64_t size,std::string hash){return "{\"schema_version\":\"aether.resources/1\",\"entries\":[{\"path\":\""+path+"\",\"mime\":\"text/css\",\"size_bytes\":"+std::to_string(size)+",\"sha256\":\""+hash+"\",\"role\":\"stylesheet\"}]}";}
Report inspect(FakeFileSystem& f,const Request& q=request(),const CancellationToken& token={}){auto r=report();r.input_sha256.reset();r.manifest_sha256.reset();r.input_size.reset();for(std::size_t i=2;i<5;++i){r.stages[i].status="not_run";r.stages[i].reason_code="NOT_REQUESTED";}inspect_resources(f,q,r,token);return r;}
void invalid_before_open(const fs::path& p,FileKind kind){FakeFileSystem f;inputs(f);f.node(p,kind);auto r=inspect(f);CHECK(exit_code(r)==4);CHECK(f.count("open_read:")==0);CHECK(r.stages[3].status=="not_run");CHECK(r.stages[4].status=="not_run");}
}
int main(){
 for(const auto& p:std::vector<std::string>{"../file","a/../b","a/./b","/file","//host/file","C:/file","a\\b","a//b","a/","CON","con.txt","PRN.css","AUX","NUL","COM1.bin","LPT9.txt","x.","x ","x%2fsecret","https://invalid.example/a"})CHECK(!valid_resource_path(p));
 CHECK(valid_resource_path("nested/style-1.css"));CHECK(valid_local_path((base()/"input.html").generic_string()));
 for(const auto& p:std::vector<std::string>{"relative/file","//host/share/file","\\\\host\\share","file:///tmp/file","C:\\file","/a/../file"})CHECK(!valid_local_path(p));
 {FakeFileSystem f;inputs(f);auto r=inspect(f);CHECK(exit_code(r)==0);CHECK(r.input_size==f.bytes(base()/"input.html").size());CHECK(r.input_sha256==sha256(f.bytes(base()/"input.html")));CHECK(r.manifest_sha256==sha256(f.bytes(base()/"resources.json")));CHECK(r.stages[4].status=="passed");CHECK(f.count("open_read:")==2);}
 // A special node represents FIFO/socket/device: none may be opened.
 for(auto kind:{FileKind::symlink,FileKind::special,FileKind::directory,FileKind::missing,FileKind::error}){
  invalid_before_open(base()/"input.html",kind);invalid_before_open(base()/"resources.json",kind);
 }
 for(auto kind:{FileKind::symlink,FileKind::special,FileKind::regular,FileKind::error}){FakeFileSystem f;inputs(f);f.node(base(),kind);auto r=inspect(f);CHECK(exit_code(r)==4);CHECK(f.count("open_read:")==0);}
 {FakeFileSystem f;inputs(f);f.node(base().root_path(),FileKind::symlink);CHECK(exit_code(inspect(f))==4);CHECK(f.count("open_read:")==0);}
 {FakeFileSystem f;inputs(f);f.directory(base()/"nested");f.file(base()/"nested"/"input.html","x");auto q=request();q.html=(base()/"nested"/"input.html").generic_string();f.node(base()/"nested",FileKind::symlink);CHECK(exit_code(inspect(f,q))==4);CHECK(f.count("open_read:")==0);}
 for(const auto& name:{"input.html","resources.json"}){FakeFileSystem f;inputs(f);f.canonical_overrides[(base()/name).generic_string()]=base().parent_path()/"outside"/name;CHECK(exit_code(inspect(f))==4);CHECK(f.count("open_read:")==0);}
 {FakeFileSystem f;inputs(f);auto q=request();q.html=(base().parent_path()/"outside"/"input.html").generic_string();f.directory(base().parent_path()/"outside");f.file(q.html,"x");CHECK(exit_code(inspect(f,q))==4);CHECK(f.count("open_read:")==0);}
 // Both advertised size and actual streamed size are bounded independently.
 for(const auto& item:std::vector<std::pair<std::string,std::size_t>>{{"input.html",2*1024*1024},{"resources.json",1024*1024}}){
  FakeFileSystem f;inputs(f);f.nodes[(base()/item.first).generic_string()].advertised_size=item.second+1;CHECK(exit_code(inspect(f))==4);CHECK(f.count("open_read:")==0);
  FakeFileSystem growth;inputs(growth);growth.file(base()/item.first,std::string(item.second+1,' '));growth.nodes[(base()/item.first).generic_string()].advertised_size=1;CHECK(exit_code(inspect(growth))==4);
 }
 {FakeFileSystem f;inputs(f,std::string("\xc0\xaf",2));auto r=inspect(f);CHECK(exit_code(r)==4);CHECK(r.errors.front().detail_code=="INVALID_UTF8");CHECK(f.count("open_read:")==1);}
 {FakeFileSystem f;inputs(f,"text","{not json}");auto r=inspect(f);CHECK(exit_code(r)==4);CHECK(r.errors.front().detail_code=="INVALID_MANIFEST");CHECK(r.stages[4].status=="not_run");}
 {FakeFileSystem f;inputs(f,"text",manifest("style.css",3,sha256("abc")));f.file(base()/"style.css","abc");auto r=inspect(f);CHECK(exit_code(r)==0);CHECK(f.count("open_read:")==3);}
 for(auto kind:{FileKind::missing,FileKind::symlink,FileKind::special,FileKind::directory}){FakeFileSystem f;inputs(f,"text",manifest("style.css",3,sha256("abc")));f.node(base()/"style.css",kind);auto r=inspect(f);CHECK(exit_code(r)==4);CHECK(r.errors.back().resource_label=="r0001");CHECK(f.count("open_read:")==2);}
 {FakeFileSystem f;inputs(f,"text",manifest("style.css",3,sha256("abc")));f.file(base()/"style.css","abd");auto r=inspect(f);CHECK(exit_code(r)==4);CHECK(r.errors.back().detail_code=="RESOURCE_MISMATCH");}
 {FakeFileSystem f;inputs(f,"text",manifest("style.css",3,sha256("abc")));f.file(base()/"style.css","abcd");auto r=inspect(f);CHECK(exit_code(r)==4);CHECK(f.count("open_read:")==2);}
 {FakeFileSystem f;inputs(f,"text",manifest("style.css",0,sha256("")));f.file(base()/"style.css",std::string(16*1024*1024+1,'x'));f.nodes[(base()/"style.css").generic_string()].advertised_size=0;CHECK(exit_code(inspect(f))==4);}
 // Explicitly list oversized resources: inventory limits never imply decoding.
 {FakeFileSystem f;inputs(f,"text",manifest("style.css",16*1024*1024+1,sha256("")));auto r=inspect(f);CHECK(exit_code(r)==4);CHECK(r.stages[3].status=="failed");CHECK(f.count("open_read:")==2);}
 {FakeFileSystem f;inputs(f,"text",manifest("style.css",0,sha256("")));f.file(base()/"style.css","");f.canonical_overrides[(base()/"style.css").generic_string()]=base().parent_path()/"elsewhere"/"style.css";CHECK(exit_code(inspect(f))==4);CHECK(f.count("open_read:")==2);}
 {FakeFileSystem f;inputs(f,"text",manifest("nested/style.css",0,sha256("")));f.node(base()/"nested",FileKind::symlink);f.file(base()/"nested"/"style.css","");CHECK(exit_code(inspect(f))==4);CHECK(f.count("open_read:")==2);}
 {FakeFileSystem f;const std::string bytes(16*1024*1024,'a');const auto hash=sha256(bytes);std::string entries;
  for(int i=0;i<5;++i){const auto name="chunk"+std::to_string(i)+".bin";if(i<4)f.file(base()/name,bytes);if(i)entries+=',';entries+="{\"path\":\""+name+"\",\"mime\":\"application/octet-stream\",\"size_bytes\":"+std::to_string(i<4?16*1024*1024:1)+",\"sha256\":\""+hash+"\",\"role\":\"other\"}";}
  inputs(f,"text","{\"schema_version\":\"aether.resources/1\",\"entries\":["+entries+"]}");auto r=inspect(f);CHECK(exit_code(r)==4);CHECK(r.errors.back().detail_code=="RESOURCE_LIMIT");CHECK(r.errors.back().resource_label=="r0005");CHECK(f.count("open_read:")==6);
 }
 // The successful trace gives a failure point for every stat/canonical/open/read.
 {FakeFileSystem baseline;inputs(baseline);CHECK(exit_code(inspect(baseline))==0);for(std::size_t n=1;n<=baseline.events.size();++n){FakeFileSystem f;inputs(f);f.fail_at=n;auto r=inspect(f);CHECK(exit_code(r)==4);CHECK(r.stages[4].status!="passed");}}
 {FakeFileSystem f;inputs(f);auto r=inspect(f,request(),{[]{return true;}});CHECK(exit_code(r)==12);CHECK(f.events.empty());}
 {FakeFileSystem f;inputs(f,std::string(50000,'a'));bool cancel=false;f.on_operation=[&](std::string_view op){if(op.starts_with("read:"))cancel=true;};auto r=inspect(f,request(),{[&]{return cancel;}});CHECK(exit_code(r)==12);CHECK(f.count("read:")==1);CHECK(r.stages[3].status=="not_run");}
 // No HTML/CSS URL extraction, font resolution or resource decoding is claimed.
 {FakeFileSystem f;inputs(f,"<img src='missing.png'><style>@import 'missing.css';</style>");auto r=inspect(f);CHECK(exit_code(r)==0);CHECK(f.count("open_read:")==2);}
}
