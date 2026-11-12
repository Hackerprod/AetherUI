#pragma once
#include <algorithm>
namespace smoke_png {
using Bytes=std::vector<std::uint8_t>;
inline void be(Bytes& b,std::uint32_t n) {for(int i=24;i>=0;i-=8)b.push_back(std::uint8_t(n>>i));}
inline std::uint32_t crc(const std::uint8_t* p,std::size_t n) {
 std::uint32_t c=~0U;for(std::size_t i=0;i<n;++i){c^=p[i];for(int k=0;k<8;++k)c=(c>>1)^((c&1)?0xedb88320U:0U);}return ~c;
}
inline void chunk(Bytes& out,const char* kind,const Bytes& data) {
 be(out,std::uint32_t(data.size()));auto start=out.size();out.insert(out.end(),kind,kind+4);
 out.insert(out.end(),data.begin(),data.end());be(out,crc(out.data()+start,data.size()+4));
}
inline Bytes rgba(const aether::rendering::Frame& f) {
 Bytes b;b.reserve(f.pixels.size());
 for(std::size_t i=0;i<f.pixels.size();i+=4) {
  const unsigned a=f.pixels[i+3];
  for(int c:{2,1,0}) b.push_back(std::uint8_t(a?std::min(255U,(unsigned(f.pixels[i+std::size_t(c)])*255+a/2)/a):0));
  b.push_back(std::uint8_t(a));
 }return b;
}
inline Bytes png(const Bytes& pixels,int width,int height) {
 Bytes raw;for(int y=0;y<height;++y){raw.push_back(0);auto begin=pixels.begin()+std::size_t(y)*std::size_t(width)*4;raw.insert(raw.end(),begin,begin+width*4);}
 Bytes z{0x78,0x01};for(std::size_t offset=0;offset<raw.size();) {
  const auto n=std::min(std::size_t(65535),raw.size()-offset);z.push_back(offset+n==raw.size()?1:0);
  const auto len=std::uint16_t(n),inv=std::uint16_t(~len);
  z.push_back(std::uint8_t(len));z.push_back(std::uint8_t(len>>8));z.push_back(std::uint8_t(inv));z.push_back(std::uint8_t(inv>>8));
  z.insert(z.end(),raw.begin()+offset,raw.begin()+offset+n);offset+=n;
 }
 std::uint32_t a=1,b=0;for(auto c:raw){a=(a+c)%65521;b=(b+a)%65521;}be(z,(b<<16)|a);
 Bytes out{137,80,78,71,13,10,26,10},head;be(head,std::uint32_t(width));be(head,std::uint32_t(height));
 head.insert(head.end(),{8,6,0,0,0});chunk(out,"IHDR",head);chunk(out,"IDAT",z);chunk(out,"IEND",{});return out;
}
}
