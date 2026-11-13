#pragma once
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <memory>
#include <vector>
#include <istream>
#include <cstdint>
#include <string>
#include <string_view>
namespace aether::rendering::text::detail {
class Sha256 {
 std::array<std::uint32_t,8> state_{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
 std::array<unsigned char,64> block_{}; std::uint64_t bytes_=0; std::size_t used_=0;
 void compress();
public: void update(std::string_view); std::string finish() const;
};
std::string sha256(std::string_view);
// SHA-256, FIPS PUB 180-4 sections 4.1.2, 4.2.2, 5 and 6.2.
// https://doi.org/10.6028/NIST.FIPS.180-4
inline void Sha256::compress() {
 static constexpr std::array<std::uint32_t,64> k{{
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}};
 std::array<std::uint32_t,64> w{};
 for(unsigned i=0;i<16;++i) w[i]=(std::uint32_t(block_[4*i])<<24)|(std::uint32_t(block_[4*i+1])<<16)|(std::uint32_t(block_[4*i+2])<<8)|std::uint32_t(block_[4*i+3]);
 for(unsigned i=16;i<64;++i) {auto x=w[i-15],y=w[i-2];w[i]=w[i-16]+(std::rotr(x,7)^std::rotr(x,18)^(x>>3))+w[i-7]+(std::rotr(y,17)^std::rotr(y,19)^(y>>10));}
 auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
 for(unsigned i=0;i<64;++i) {auto t1=h+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^((~e)&g))+k[i]+w[i];auto t2=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
 state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
}
inline void Sha256::update(std::string_view input) {
 if(input.size()>std::numeric_limits<std::uint64_t>::max()/8-bytes_) throw std::length_error("HASH_SIZE_LIMIT");
 bytes_+=input.size();
 for(unsigned char c:input) {block_[used_++]=c;if(used_==64){compress();used_=0;}}
}
inline std::string Sha256::finish() const {
 auto s=*this;const auto bits=s.bytes_*8;s.block_[s.used_++]=0x80;
 if(s.used_>56) {while(s.used_<64)s.block_[s.used_++]=0;s.compress();s.used_=0;}
 while(s.used_<56)s.block_[s.used_++]=0;
 for(int i=7;i>=0;--i) {
  s.block_[s.used_++]=static_cast<unsigned char>(bits>>(i*8));
 }
 s.compress();
 constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(64);
 for(auto word:s.state_) {
  for(int shift=28;shift>=0;shift-=4) {
   out+=hex[(word>>shift)&15];
  }
 }
 return out;
}
inline std::string sha256(std::string_view input){Sha256 s;s.update(input);return s.finish();}
}

namespace aether::rendering::text {
inline constexpr std::size_t max_font_bytes = 16 * 1024 * 1024;
// Digest identifies the retained bytes, not authenticity, validity or licensing.
// No external font-library handles, paths or borrowed byte storage are retained.
class FontSource final {
 static std::vector<std::uint8_t> bounded(std::vector<std::uint8_t> bytes) {
  if(bytes.empty() || bytes.size()>max_font_bytes)
   throw std::invalid_argument("font source bounds");
  return bytes;
 }
public:
 const std::vector<std::uint8_t> bytes;
 const std::string digest;
 const std::string provenance;
 explicit FontSource(std::vector<std::uint8_t> b, std::string origin = {})
 : bytes(bounded(std::move(b))),
   digest(detail::sha256(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()))),
   provenance(std::move(origin)) {
  if(provenance.size()>4096) throw std::invalid_argument("font provenance bounds");
 }
 // Caller supplies the expected bounded length from its source manifest. Read
 // once into owned storage, rejecting both truncation and trailing bytes. Hash
 // only that retained buffer; never reopen or hash a path separately.
 static std::shared_ptr<const FontSource> read_exact(std::istream& input,
     std::size_t expected_size, std::string origin = {}) {
  if(!expected_size || expected_size>max_font_bytes)
   throw std::invalid_argument("font source bounds");
  std::vector<std::uint8_t> b(expected_size);
  input.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));
  if(input.gcount()!=static_cast<std::streamsize>(expected_size) || input.bad())
   throw std::runtime_error("truncated font source");
  if(input.peek()!=std::char_traits<char>::eof() || input.bad())
   throw std::runtime_error("font source length mismatch");
  return std::make_shared<const FontSource>(std::move(b),std::move(origin));
 }
};
}
