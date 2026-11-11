#include "aether_visual_compare/resource_validation.hpp"
#include "aether_visual_compare/resource_manifest.hpp"
#include "sha256.hpp"
#include <fstream>
#include <array>
namespace avc {
namespace {
class NativeReader final : public Reader {
 std::ifstream stream_;
public:
 explicit NativeReader(const fs::path& p):stream_(p,std::ios::binary){}
 bool good() const {return stream_.is_open();}
 bool read(char* p,std::size_t n,std::size_t& got) override {
  stream_.read(p,static_cast<std::streamsize>(n)); got=static_cast<std::size_t>(stream_.gcount());
  return !stream_.bad() && (!stream_.fail() || stream_.eof());
 }
};
class NativeWriter final : public Writer {
 std::ofstream stream_;
public:
 explicit NativeWriter(const fs::path& p):stream_(p,std::ios::binary|std::ios::trunc){}
 bool good() const{return stream_.is_open();}
 bool write(std::string_view s) override {stream_.write(s.data(),static_cast<std::streamsize>(s.size()));return bool(stream_);}
 bool flush() override {stream_.flush();return bool(stream_);}
 bool close() override {stream_.close();return !stream_.fail();}
};
class NativeFileSystem final : public FileSystem {
public:
 FileInfo stat(const fs::path& p) override {
  std::error_code e; auto s=fs::symlink_status(p,e);
  if(e==std::errc::no_such_file_or_directory) return {FileKind::missing,0};
  if(e) return {};
  if(fs::is_symlink(s)) return {FileKind::symlink,0};
  if(fs::is_directory(s)) return {FileKind::directory,0};
  if(!fs::exists(s)) return {FileKind::missing,0};
  if(!fs::is_regular_file(s)) return {FileKind::special,0};
  auto size=fs::file_size(p,e); return e?FileInfo{}:FileInfo{FileKind::regular,size};
 }
 std::optional<fs::path> canonical(const fs::path& p) override {std::error_code e;auto r=fs::canonical(p,e);if(e)return {};return r;}
 std::unique_ptr<Reader> open_read(const fs::path& p) override {auto r=std::make_unique<NativeReader>(p);if(!r->good())return {};return r;}
 bool create_directory(const fs::path& p) override {std::error_code e;return fs::create_directory(p,e)&&!e;}
 std::unique_ptr<Writer> open_write(const fs::path& p) override {auto w=std::make_unique<NativeWriter>(p);if(!w->good())return {};return w;}
 bool rename(const fs::path& a,const fs::path& b) override {std::error_code e;fs::rename(a,b,e);return !e;}
};
bool contained(const fs::path& root,const fs::path& p) {
 auto a=root.begin(),b=p.begin(); for(;a!=root.end();++a,++b) if(b==p.end()||*a!=*b)return false;
 return b!=p.end();
}
void fail(Report& r,std::size_t stage,int code,const char* detail,std::optional<std::string> label={}) {
 r.stages[stage].status=code==12?"cancelled":"failed"; r.stages[stage].reason_code=detail;
 add_error(r,{code,r.stages[stage].name,detail,std::move(label)});
}
bool regular_contained(FileSystem& f,const fs::path& root,const fs::path& p,std::uint64_t limit,FileInfo& info) {
 if(!directory_chain(f,p.parent_path()))return false;
 info=f.stat(p); if(info.kind!=FileKind::regular||info.size>limit)return false;
 auto cp=f.canonical(p);return cp && contained(root,*cp);
}
std::optional<std::string> read_bounded(FileSystem& f,const fs::path& p,std::uint64_t limit,const CancellationToken& token) {
 auto in=f.open_read(p);if(!in)return {};
 std::string bytes;std::array<char,16384> block{};
 for(;;){if(token.cancelled())return {};std::size_t n=0;if(!in->read(block.data(),block.size(),n)||n>block.size())return {};
  if(bytes.size()>limit||n>limit-bytes.size())return {};
  bytes.append(block.data(),n);
  if(n==0)break;}
 return bytes;
}
}
std::unique_ptr<FileSystem> native_filesystem(){return std::make_unique<NativeFileSystem>();}
bool valid_local_path(std::string_view s) {
 // A portable absolute grammar: /segments or X:/segments. No implicit cwd.
 if(s.empty()||s.find('\\')!=s.npos)return false;
 std::size_t start=0;
 if(s[0]=='/') {if(s.size()>1&&s[1]=='/')return false;start=1;}
 else if(s.size()>=3&&((s[0]>='A'&&s[0]<='Z')||(s[0]>='a'&&s[0]<='z'))&&s[1]==':'&&s[2]=='/')start=3;
 else return false;
 return start==s.size() || valid_resource_path(s.substr(start));
}
bool directory_chain(FileSystem& f,const fs::path& p) {
 if(!p.is_absolute())return false;
 fs::path current=p.root_path(); if(f.stat(current).kind!=FileKind::directory)return false;
 for(const auto& part:p.relative_path()) {if(part=="."||part==".."||part.empty())return false;current/=part;if(f.stat(current).kind!=FileKind::directory)return false;}
 return true;
}
void inspect_resources(FileSystem& f,const Request& q,Report& r,const CancellationToken& token) {
 auto cancelled=[&](std::size_t s){if(!token.cancelled())return false;fail(r,s,12,"CANCELLED");return true;};
 if(cancelled(2))return;
 fs::path root(q.resource_root),html(q.html),manifest(q.manifest);
 if(!directory_chain(f,root)){fail(r,2,4,"INPUT_UNREADABLE");return;}
 auto cr=f.canonical(root);if(!cr){fail(r,2,4,"INPUT_UNREADABLE");return;}
 FileInfo hi,mi;
 if(!regular_contained(f,*cr,html,2*1024*1024,hi)||!regular_contained(f,*cr,manifest,1024*1024,mi)){fail(r,2,4,"INPUT_UNREADABLE");return;}
 auto hb=read_bounded(f,html,2*1024*1024,token);if(cancelled(2))return;
 if(!hb||hb->size()!=hi.size){fail(r,2,4,"INPUT_UNREADABLE");return;}
 if(!valid_utf8(*hb)){fail(r,2,4,"INVALID_UTF8");return;}
 r.input_sha256=sha256(*hb);r.input_size=hb->size();
 auto mb=read_bounded(f,manifest,1024*1024,token);if(cancelled(2))return;
 if(!mb||mb->size()!=mi.size){fail(r,2,4,"INPUT_UNREADABLE");return;}
 r.manifest_sha256=sha256(*mb);r.stages[2].status="passed";r.stages[2].reason_code="OK";
 if(cancelled(3))return;
 auto m=parse_manifest(*mb);if(!m){fail(r,3,4,"INVALID_MANIFEST");return;}
 r.stages[3].status="passed";r.stages[3].reason_code="OK";
 std::uint64_t total=0;
 for(std::size_t i=0;i<m->entries.size();++i){
  if(cancelled(4))return;
  const auto& e=m->entries[i];auto number=std::to_string(i+1);std::string label="r"+std::string(4-number.size(),'0')+number;
  if(e.size_bytes>64*1024*1024-total){fail(r,4,4,"RESOURCE_LIMIT",label);return;}total+=e.size_bytes;
  FileInfo ri;auto p=root/fs::path(e.path);
  if(!regular_contained(f,*cr,p,16*1024*1024,ri)){fail(r,4,4,"RESOURCE_UNREADABLE",label);return;}
  if(ri.size!=e.size_bytes){fail(r,4,4,"RESOURCE_MISMATCH",label);return;}
  auto bytes=read_bounded(f,p,16*1024*1024,token);if(cancelled(4))return;
  if(!bytes){fail(r,4,4,"RESOURCE_UNREADABLE",label);return;}
  if(bytes->size()!=e.size_bytes||sha256(*bytes)!=e.sha256){fail(r,4,4,"RESOURCE_MISMATCH",label);return;}
 }
 r.stages[4].status="passed";r.stages[4].reason_code="OK";
}
}
