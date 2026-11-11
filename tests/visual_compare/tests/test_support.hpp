#pragma once
#include "aether_visual_compare/model.hpp"
#include "aether_visual_compare/resource_validation.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace avc::test {
inline void check(bool condition,const char* expression,int line) {
 if(!condition) throw std::runtime_error(std::string("contract assertion at line ")+std::to_string(line)+": "+expression);
}
#define CHECK(expression) ::avc::test::check(bool(expression),#expression,__LINE__)
inline fs::path base() {
#ifdef _WIN32
 return fs::path("C:/fixture");
#else
 return fs::path("/fixture");
#endif
}
inline Request request(Command command=Command::inspect) {
 Request q; q.command=command;q.resource_root=base().generic_string();
 q.html=(base()/"input.html").generic_string();q.manifest=(base()/"resources.json").generic_string();
 q.out=(base()/"result").generic_string();q.viewport={101,99,5,126,124};return q;
}
inline Report report(Command command=Command::inspect) {
 Report r;r.request=request(command);r.run_id="0123456789abcdef0123456789abcdef";
 if(command==Command::inspect){r.input_sha256=std::string(64,'0');r.manifest_sha256=std::string(64,'0');r.input_size=0;}
 for(std::size_t i=0;i<5;++i){r.stages[i].status="passed";r.stages[i].reason_code="OK";}
 for(std::size_t i=5;i<9;++i)r.stages[i].reason_code="OUTSIDE_SCOPE";
 r.stages[9].status="passed";r.stages[9].reason_code="OK"; // Planned commit, never acceptance evidence.
 if(command==Command::capture){for(std::size_t i=2;i<5;++i){r.stages[i].status="not_run";r.stages[i].reason_code="NOT_REQUESTED";}
  r.stages[5].status="not_implemented";r.stages[5].reason_code="NOT_IMPLEMENTED";
  add_error(r,{11,"aether_capture","NOT_IMPLEMENTED",{}});}
 return r;
}
struct Node {FileKind kind=FileKind::regular;std::string bytes;std::optional<std::uint64_t> advertised_size;};
class FakeFileSystem final:public FileSystem {
 class Input final:public Reader {
  FakeFileSystem& f_;std::string key_;std::size_t cursor_=0;
 public:Input(FakeFileSystem& f,std::string p):f_(f),key_(std::move(p)){}
  bool read(char* out,std::size_t capacity,std::size_t& got) override {
   got=0;if(!f_.operation("read",key_))return false;
   const auto& bytes=f_.nodes.at(key_).bytes;got=std::min(capacity,bytes.size()-cursor_);
   std::memcpy(out,bytes.data()+cursor_,got);cursor_+=got;return true;
  }
 };
 class Output final:public Writer {
  FakeFileSystem& f_;std::string key_;
 public:Output(FakeFileSystem& f,std::string p):f_(f),key_(std::move(p)){}
  bool write(std::string_view s) override {
   if(!f_.operation("write",key_)){f_.nodes[key_].bytes.append(s.substr(0,s.size()/2));return false;}
   f_.nodes[key_].bytes.append(s);return true;
  }
  bool flush() override {return f_.operation("flush",key_);}
  bool close() override {return f_.operation("close",key_);}
 };
 static std::string key(const fs::path& p){return p.generic_string();}
public:
 std::map<std::string,Node> nodes;
 std::map<std::string,fs::path> canonical_overrides;
 std::vector<std::string> events;
 std::size_t fail_at=std::numeric_limits<std::size_t>::max();
 std::function<void(std::string_view)> on_operation;
 FakeFileSystem(){directory(base().root_path());directory(base());}
 bool operation(std::string_view op,const std::string& p){events.push_back(std::string(op)+":"+p);if(on_operation)on_operation(events.back());return events.size()!=fail_at;}
 void directory(const fs::path& p){nodes[key(p)]={FileKind::directory,{},{}};}
 void file(const fs::path& p,std::string bytes){nodes[key(p)]={FileKind::regular,std::move(bytes),{}};}
 void node(const fs::path& p,FileKind kind){nodes[key(p)]={kind,{},{}};}
 bool exists(const fs::path& p)const{return nodes.contains(key(p));}
 std::string bytes(const fs::path& p)const{auto it=nodes.find(key(p));return it==nodes.end()?std::string{}:it->second.bytes;}
 std::size_t count(std::string_view prefix)const{return std::count_if(events.begin(),events.end(),[&](const auto& e){return e.starts_with(prefix);});}
 FileInfo stat(const fs::path& p) override {const auto k=key(p);if(!operation("stat",k))return {};auto it=nodes.find(k);if(it==nodes.end())return {FileKind::missing,0};return {it->second.kind,it->second.advertised_size.value_or(it->second.bytes.size())};}
 std::optional<fs::path> canonical(const fs::path& p) override {const auto k=key(p);if(!operation("canonical",k))return {};if(canonical_overrides.contains(k))return canonical_overrides.at(k);if(!exists(p))return {};return p;}
 std::unique_ptr<Reader> open_read(const fs::path& p) override {const auto k=key(p);if(!operation("open_read",k)||!exists(p)||nodes.at(k).kind!=FileKind::regular)return {};return std::make_unique<Input>(*this,k);}
 bool create_directory(const fs::path& p) override {const auto k=key(p);if(!operation("create_directory",k)||exists(p))return false;directory(p);return true;}
 std::unique_ptr<Writer> open_write(const fs::path& p) override {const auto k=key(p);if(!operation("open_write",k))return {};file(p,{});return std::make_unique<Output>(*this,k);}
 bool rename(const fs::path& from,const fs::path& to) override {const auto a=key(from),b=key(to);if(!operation("rename",a+"->"+b)||!exists(from)||exists(to))return false;nodes[b]=std::move(nodes.at(a));nodes.erase(a);return true;}
};
inline void inputs(FakeFileSystem& f,std::string html="<!doctype html><p>Contract bytes</p>",std::string manifest="{\"schema_version\":\"aether.resources/1\",\"entries\":[]}") {
 f.file(base()/"input.html",std::move(html));f.file(base()/"resources.json",std::move(manifest));
}
inline std::vector<std::string> arguments(Command command=Command::inspect) {
 auto q=request(command);return {command==Command::inspect?"inspect":"capture","--html",q.html,"--resource-root",q.resource_root,"--manifest",q.manifest,"--out",q.out,"--viewport","101x99","--scale","1.25"};
}
inline std::vector<std::string_view> views(const std::vector<std::string>& owned){std::vector<std::string_view> out;for(const auto& a:owned)out.push_back(a);return out;}
}
