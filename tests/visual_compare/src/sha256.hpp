#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
namespace avc {
class Sha256 {
 std::array<std::uint32_t,8> state_{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
 std::array<unsigned char,64> block_{}; std::uint64_t bytes_=0; std::size_t used_=0;
 void compress();
public: void update(std::string_view); std::string finish() const;
};
std::string sha256(std::string_view);
}
