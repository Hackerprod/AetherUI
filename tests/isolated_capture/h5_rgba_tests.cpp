#include "../../native/src/style_runtime/h5_color_profile.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <new>
#include <cstdlib>
static long fail_after = -1;
void* operator new(std::size_t n) { if (fail_after == 0) throw std::bad_alloc(); if (fail_after > 0) --fail_after; if(void* p=std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
int main() {
 unsigned checks = 0;
 auto accept = [&](const char* s, float alpha) { aether_color_rgba c{}; assert(aether::css::parse_legacy_rgba_strict(s,c)); assert(std::abs(c.a-alpha)<1e-6); ++checks; };
 accept("rgba(0,0,0,0)",0); accept("rgba(0,0,0,.07)",.07); accept("rgba(0,0,0,.08)",.08); accept("rgba(255,255,255,1)",1);
 accept("rgba(100%,0%,50%,7%)",.07); accept(" RGBA(300,-1,0,2) ",1); accept("rgba(0,0,0,1e-40)",0); accept("rgba(0,/**/0,0,.08)",.08);
 for(const char* s:{"rgba(1,2,3)","rgba(1,2,3,)","rgba(1 2 3 / 1)","rgba(1%,2,3,1)","rgba(1,2,3,1) tail","rgba(1e999,2,3,1)","rgba(1,2,3,1e99)","rgba(NaN,2,3,1)","rgba(calc(1),2,3,1)","rgba(1,2,3,1))","rgba(1,2,3,1","rgba(1,2,3,1,2)","rgb(1,2,3)","r\\67 ba(1,2,3,1)"}) {
  aether_color_rgba c{.2f,.3f,.4f,.5f}, before=c; assert(!aether::css::parse_legacy_rgba_strict(s,c)); assert(!std::memcmp(&c,&before,sizeof c)); ++checks;
 }
 std::string nul("rgba(1,2,3,1)\0x",15); aether_color_rgba c{}; assert(!aether::css::parse_legacy_rgba_strict(nul,c)); ++checks;
 // Existing public failure behavior stays transparent, not transactional.
 c={1,1,1,1}; assert(!aether_color_rgba_try_parse("invalid",&c)); assert(c.r==0&&c.g==0&&c.b==0&&c.a==0); ++checks;
 for(const char* s:{"0 2px 8px rgba(0,0,0,.07)","rgba(0,0,0,.08) 0 0", "0 0 0 1px rgba(0,0,0,.08), 1px 2px rgba(1,2,3,1)"}) {assert(h5_rgba_declaration_supported("box-shadow",s)); ++checks;}
 for(const char* s:{"0 rgba(0,0,0,.1)","0 0 rgba(0,0,0,.1) tail","0 0 -1px rgba(0,0,0,.1)","inset 0 0 rgba(0,0,0,.1)","0 0 rgba(0,0,0,1e999)","0 0 rgba(0,0,0,.1),","0 0 calc(1px) rgba(0,0,0,.1)","0 0 1em rgba(0,0,0,.1)"}) { assert(!h5_rgba_declaration_supported("box-shadow",s)); ++checks; }
 assert(h5_rgba_declaration_supported("background-color","rgba(0,0,0,.07)")); ++checks;
 assert(!h5_rgba_declaration_supported("background","rgba(0,0,0,.07)")); ++checks;
 assert(!h5_rgba_declaration_supported("background-color","rgba(0,0,0,.07) garbage")); ++checks;
 unsigned oom = 0;
 const std::string allocating = "rgba(123.456789,234.56789,123.45678,0.123456789)";
 for (long i=0; i<128; ++i) {
  aether_color_rgba candidate{.2f,.3f,.4f,.5f}, before=candidate;
  fail_after=i;
  try { bool ok=aether::css::parse_legacy_rgba_strict(allocating,candidate); fail_after=-1; assert(ok); break; }
  catch (const std::bad_alloc&) { fail_after=-1; assert(!std::memcmp(&candidate,&before,sizeof before)); ++oom; }
 }
 assert(oom>0); std::cout << "H5 rgba OOM points=" << oom << '\n';
 std::cout << "H5 rgba checks=" << checks << '\n';
}
