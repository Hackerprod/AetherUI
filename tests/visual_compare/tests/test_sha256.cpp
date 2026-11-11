#include "sha256.hpp"
#include "test_support.hpp"
#include <string>
int main() {
 using avc::sha256;
 CHECK(sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
 CHECK(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
 const std::string multi="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
 CHECK(sha256(multi)=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
 CHECK(sha256(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
 for(std::size_t chunk=1;chunk<=65;++chunk) {
  avc::Sha256 s;for(std::size_t i=0;i<multi.size();i+=chunk)s.update(std::string_view(multi).substr(i,chunk));
  CHECK(s.finish()==sha256(multi));CHECK(s.finish()==sha256(multi));
  s.update("!");CHECK(s.finish()==sha256(multi+"!"));
 }
 avc::Sha256 binary;std::string bytes;for(unsigned i=0;i<256;++i)bytes+=static_cast<char>(i);
 binary.update(std::string_view(bytes).substr(0,63));binary.update(std::string_view(bytes).substr(63));CHECK(binary.finish()==sha256(bytes));
}
