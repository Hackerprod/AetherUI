#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include "../../../native/src/rendering/native_frame.h"
#include "../../software_render_smoke/png_writer.h"
#include "../../../native/src/rendering/text/font_source.h"
namespace q1b_atlas {
namespace fs=std::filesystem;
using Bytes=std::vector<std::uint8_t>;
inline constexpr std::size_t max_png_bytes=16*1024*1024, max_report_bytes=128*1024;
inline void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
inline std::string hash(std::span<const std::uint8_t> b){return aether::rendering::text::detail::sha256({reinterpret_cast<const char*>(b.data()),b.size()});}
inline void write(const fs::path& p,std::span<const std::uint8_t> bytes){std::ofstream f(p,std::ios::binary|std::ios::trunc);require(bool(f),"write_open_failed");f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));f.flush();require(bool(f),"write_failed");f.close();require(bool(f),"close_failed");}
inline Bytes read(const fs::path& p,std::size_t max){require(fs::is_regular_file(p),"not_regular_file");auto n=fs::file_size(p);require(n<=max,"read_size_limit");std::ifstream f(p,std::ios::binary);Bytes bytes(n);f.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(n));require(f.gcount()==std::streamsize(n)&&f.peek()==std::char_traits<char>::eof(),"read_failed");return bytes;}
inline std::uint32_t be(std::span<const std::uint8_t> b,std::size_t at){require(at<=b.size()&&4<=b.size()-at,"png_truncated");return std::uint32_t(b[at])<<24|std::uint32_t(b[at+1])<<16|std::uint32_t(b[at+2])<<8|b[at+3];}
// Verifies the bounded RGBA8/stored-deflate output of our existing PNG encoder.
// Not a general-purpose decoder and not used for untrusted arbitrary PNGs.
inline void verify_png(std::span<const std::uint8_t> b,std::span<const std::uint8_t> expected_rgba={}){
 require(expected_rgba.empty()||expected_rgba.size()==1024*1024*4,"expected_pixel_size");
 constexpr std::array<std::uint8_t,8> sig{137,80,78,71,13,10,26,10};require(b.size()<=max_png_bytes&&b.size()>=8&&std::equal(sig.begin(),sig.end(),b.begin()),"png_signature");
 std::size_t at=8;unsigned chunks=0;std::span<const std::uint8_t> z;
 while(at<b.size()){
  auto len=be(b,at);require(len<=b.size()-at-4&&b.size()-at>=12&&len<=b.size()-at-12,"png_chunk_bounds");const auto kind=be(b,at+4);auto data=b.subspan(at+8,len);
  require(smoke_png::crc(b.data()+at+4,std::size_t(len)+4)==be(b,at+8+len),"png_crc");
  if(chunks==0){require(kind==0x49484452&&len==13&&be(data,0)==1024&&be(data,4)==1024,"png_dimensions");require(data[8]==8&&data[9]==6&&data[10]==0&&data[11]==0&&data[12]==0,"png_format");}
  else if(chunks==1){require(kind==0x49444154,"png_idat");z=data;}
  else if(chunks==2){require(kind==0x49454e44&&len==0,"png_iend");}
  else throw std::runtime_error("png_extra_chunk");
  at+=std::size_t(len)+12;++chunks;
 }
 require(chunks==3&&z.size()>=6&&z[0]==0x78&&z[1]==0x01,"zlib_header");
 std::size_t pos=2,total=0;std::uint32_t adler_a=1,adler_b=0;bool final=false;
 while(!final){require(pos+5<=z.size()-4,"deflate_truncated");const auto header=z[pos++];require(header==0||header==1,"deflate_type");final=header==1;
  const unsigned n=unsigned(z[pos])|(unsigned(z[pos+1])<<8),inv=unsigned(z[pos+2])|(unsigned(z[pos+3])<<8);pos+=4;require((n^inv)==65535&&n<=z.size()-4-pos,"deflate_length");
  for(unsigned i=0;i<n;++i){auto v=z[pos+i];if(total%4097==0)require(v==0,"png_filter");else if(!expected_rgba.empty()){const auto index=(total/4097)*4096+(total%4097)-1;require(index<expected_rgba.size()&&v==expected_rgba[index],"png_pixel_mismatch");}adler_a=(adler_a+v)%65521;adler_b=(adler_b+adler_a)%65521;++total;}
  pos+=n;
 }
 require(pos+4==z.size()&&total==1024*4097&&be(z,pos)==((adler_b<<16)|adler_a),"zlib_checksum_or_size");
}
class Stage final {
 fs::path destination_, temporary_;bool committed_=false;
public:
 explicit Stage(fs::path destination):destination_(std::move(destination)){
  require(!destination_.filename().empty()&&!fs::exists(destination_),"destination_exists_or_invalid");
  require(fs::is_directory(destination_.parent_path()),"missing_parent");
  temporary_=destination_.parent_path()/((".partial-"+destination_.filename().string()+"-")+std::to_string(getpid())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  require(fs::create_directory(temporary_),"stage_create_failed");
 }
 Stage(const Stage&)=delete;Stage& operator=(const Stage&)=delete;
 ~Stage(){if(!committed_){std::error_code ec;fs::remove_all(temporary_,ec);}}
 const fs::path& path()const{return temporary_;}
 void commit(){require(!fs::exists(destination_),"destination_exists");require(syscall(SYS_renameat2,AT_FDCWD,temporary_.c_str(),AT_FDCWD,destination_.c_str(),1 /* RENAME_NOREPLACE */)==0,"atomic_rename_failed");committed_=true;}
};
}
