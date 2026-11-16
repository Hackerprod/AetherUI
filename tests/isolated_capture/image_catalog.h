#pragma once
// Explicit CLI capabilities only. CSS never supplies a filesystem path.
#include <aether/aether.h>
#include "../../native/src/rendering/native_paint_list.h"
#include "../visual_compare/src/sha256.hpp"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>
namespace capture_images {
constexpr size_t max_entries=64, max_file=16u*1024u*1024u,
 max_encoded=32u*1024u*1024u, max_retained=64u*1024u*1024u;
struct Error {const char* reason;};
inline void require(bool b,const char* reason){if(!b)throw Error{reason};}
struct Entry {std::string alias, file;};
struct Config {std::string root;std::vector<Entry> entries;std::set<std::string> aliases;};
inline bool alias_valid(const std::string& s){
 if(s.empty()||s.size()>64)return false;
 for(unsigned char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
 return true;
}
inline bool component_valid(const std::string& s){
 if(s.empty()||s=="."||s==".."||s.size()>255)return false;
 for(unsigned char c:s)if(c=='/'||c=='\\'||c<32||c==127)return false;
 return true;
}
inline std::vector<std::string> root_components(const std::string& path){
 require(!path.empty()&&path.size()<=4096,"IMAGE_ROOT_INVALID");
 std::vector<std::string> parts;
 if(path=="/"||path==".")return parts;
 size_t start=path[0]=='/'?1:0;
 while(start<path.size()){
  auto end=path.find('/',start);if(end==std::string::npos)end=path.size();
  auto c=path.substr(start,end-start);require(component_valid(c),"IMAGE_ROOT_INVALID");parts.push_back(std::move(c));
  start=end+1;
 }
 require(path.back()!='/',"IMAGE_ROOT_INVALID");return parts;
}
inline void validate(Config& c){
 require(c.entries.size()<=max_entries,"IMAGE_COUNT_LIMIT");
 require(c.root.empty()==c.entries.empty(),"IMAGE_ROOT_REQUIRED_WITH_ENTRIES");
 c.aliases.clear();
 if(c.entries.empty())return;
 (void)root_components(c.root);
 for(const auto& e:c.entries){
  require(alias_valid(e.alias),"IMAGE_ALIAS_INVALID");
  require(component_valid(e.file),"IMAGE_FILENAME_INVALID");
  require(c.aliases.insert(e.alias).second,"IMAGE_ALIAS_DUPLICATE");
 }
}
// Parse all options before opening any asset. Legacy six positional args unchanged.
inline Config options(int argc,char**argv){
 Config c;bool root=false;
 for(int i=7;i<argc;){
  std::string flag=argv[i++];
  if(flag=="--image-root") {require(!root&&i<argc,"IMAGE_OPTIONS_INVALID");root=true;c.root=argv[i++];require(!c.root.empty(),"IMAGE_ROOT_INVALID");}
  else if(flag=="--image") {require(i+1<argc,"IMAGE_OPTIONS_INVALID");c.entries.push_back({argv[i],argv[i+1]});i+=2;require(c.entries.size()<=max_entries,"IMAGE_COUNT_LIMIT");}
  else throw Error{"IMAGE_OPTIONS_INVALID"};
 }
 validate(c);return c;
}
struct Fd {
 int fd=-1;explicit Fd(int f=-1):fd(f){}~Fd(){if(fd>=0)close(fd);}
 Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;
 void reset(int f){if(fd>=0)close(fd);fd=f;}
};
inline int open_root(const std::string& root){
 auto parts=root_components(root);
 Fd current(open(root[0]=='/'?"/":".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
 require(current.fd>=0,"IMAGE_ROOT_OPEN_FAILED");
 for(const auto& part:parts){int next=openat(current.fd,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(next>=0,"IMAGE_ROOT_OPEN_FAILED");current.reset(next);}
 int result=current.fd;current.fd=-1;return result;
}
using Clock=std::chrono::steady_clock;
inline double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
inline aether::rendering::NativeImageCatalog load(const Config& config,std::ostream& log){
 Config checked=config;validate(checked); // Even direct callers validate entire configuration first.
 aether::rendering::NativeImageCatalog catalog;
 log<<"image_loading="<<(checked.entries.empty()?"DISABLED":"EXPLICIT_CATALOG_ONLY")<<"\n";
 if(checked.entries.empty())return catalog;
 Fd root(open_root(checked.root));size_t encoded_total=0,retained_total=0;
 double read_ms=0,decode_ms=0,copy_ms=0;
 for(const auto& entry:checked.entries){
  auto started=Clock::now();
  Fd file(openat(root.fd,entry.file.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));require(file.fd>=0,"IMAGE_OPEN_FAILED");
  struct stat st{};require(fstat(file.fd,&st)==0&&S_ISREG(st.st_mode),"IMAGE_NOT_REGULAR");
  require(st.st_size>=0&&static_cast<unsigned long long>(st.st_size)<=max_file&&static_cast<unsigned long long>(st.st_size)<=max_encoded-encoded_total,"IMAGE_ENCODED_LIMIT");
  std::vector<uint8_t> bytes;char buffer[16384];
  for(;;){ssize_t n=read(file.fd,buffer,sizeof buffer);if(n<0&&errno==EINTR)continue;require(n>=0,"IMAGE_READ_FAILED");if(!n)break;
   require(size_t(n)<=max_file-bytes.size()&&size_t(n)<=max_encoded-encoded_total-bytes.size(),"IMAGE_ENCODED_LIMIT");
   bytes.insert(bytes.end(),buffer,buffer+n);
  }
  require(!bytes.empty(),"IMAGE_EMPTY");read_ms+=ms(started);encoded_total+=bytes.size();
  auto hash=avc::sha256(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
  started=Clock::now();int32_t w=0,h=0;uint8_t* decoded=nullptr;
  auto status=aether_image_decode(bytes.data(),bytes.size(),&w,&h,&decoded);
  std::unique_ptr<uint8_t,decltype(&aether_image_pixels_free)> owner(decoded,aether_image_pixels_free);
  decode_ms+=ms(started);require(status==AETHER_OK&&decoded&&w>0&&h>0,"IMAGE_DECODE_FAILED");
  require(size_t(w)<=(max_retained-retained_total)/4u/size_t(h),"IMAGE_RETAINED_LIMIT");
  size_t count=size_t(w)*size_t(h)*4u;
  started=Clock::now();auto copied=aether::rendering::preparation::ImageResource::copy(w,h,{decoded,count});copy_ms+=ms(started);
  require(std::holds_alternative<aether::rendering::preparation::ImageRef>(copied),"IMAGE_COPY_FAILED");
  catalog.push_back({entry.alias,std::get<aether::rendering::preparation::ImageRef>(std::move(copied))});retained_total+=count;
  log<<"image alias_sha256="<<avc::sha256(entry.alias)<<" sha256="<<hash<<" encoded_bytes="<<bytes.size()<<" retained_bgra_bytes="<<count<<" width="<<w<<" height="<<h<<"\n";
 }
 log<<"image_entries="<<catalog.size()<<"\nimage_encoded_bytes="<<encoded_total<<"\nimage_retained_bgra_bytes="<<retained_total<<"\nimage_read_wall_ms="<<read_ms<<"\nimage_decode_wall_ms="<<decode_ms<<"\nimage_copy_wall_ms="<<copy_ms<<"\nimage_memory_scope=RETAINED_BGRA_ONLY_EXCLUDES_DECODER_SCRATCH_AND_TEMPORARIES\n";
 return catalog;
}
}
