#include "aether_visual_compare/native_capture.hpp"
#include <algorithm>
#include <stdexcept>
namespace avc {
namespace {
void be32(std::string& out,std::uint32_t v){for(int n=24;n>=0;n-=8)out.push_back(static_cast<char>(v>>n));}
std::uint32_t crc(std::string_view bytes){std::uint32_t v=~0u;for(unsigned char c:bytes){v^=c;for(int i=0;i<8;++i)v=(v>>1)^(0xedb88320u& (0u-(v&1u)));}return ~v;}
void chunk(std::string& out,const char* kind,std::string_view data){be32(out,static_cast<std::uint32_t>(data.size()));auto start=out.size();out.append(kind,4);out.append(data);be32(out,crc(std::string_view(out).substr(start)));}
}
std::string encode_native_png(unsigned w,unsigned h,std::span<const std::uint8_t> bgra){
 if(!w||!h||w>2048||h>2048||bgra.size()!=std::size_t(w)*h*4)throw std::invalid_argument("NATIVE_FRAME_INVALID");
 std::string raw;raw.reserve(std::size_t(w)*h*4+h);
 for(unsigned y=0;y<h;++y){raw.push_back(0);for(unsigned x=0;x<w;++x){auto p=bgra.data()+(std::size_t(y)*w+x)*4;unsigned a=p[3];for(unsigned c:{2u,1u,0u})raw.push_back(static_cast<char>(a?std::min(255u,(unsigned(p[c])*255+a/2)/a):0));raw.push_back(static_cast<char>(a));}}
 std::string z;z.reserve(raw.size()+raw.size()/65535*5+16);z.append("\x78\x01",2);
 for(std::size_t pos=0;pos<raw.size();){unsigned n=static_cast<unsigned>(std::min<std::size_t>(65535,raw.size()-pos));z.push_back(pos+n==raw.size()?1:0);z.push_back(static_cast<char>(n));z.push_back(static_cast<char>(n>>8));unsigned nn=n^65535;z.push_back(static_cast<char>(nn));z.push_back(static_cast<char>(nn>>8));z.append(raw,pos,n);pos+=n;}
 std::uint32_t a=1,b=0;for(unsigned char c:raw){a=(a+c)%65521;b=(b+a)%65521;}be32(z,(b<<16)|a);
 std::string out("\x89PNG\r\n\x1a\n",8),ihdr;be32(ihdr,w);be32(ihdr,h);ihdr.append("\x08\x06\0\0\0",5);chunk(out,"IHDR",ihdr);chunk(out,"IDAT",z);chunk(out,"IEND",{});return out;
}
}
