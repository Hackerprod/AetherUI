#ifdef NDEBUG
#error "R0 tests require assertions enabled; do not compile with NDEBUG"
#endif
#include "../src/rendering/native_frame.h"
#include <cassert>
#include <limits>
#include <new>
#include <thread>
using namespace aether::rendering;
namespace aether::rendering {
struct FrameStoreTestAccess {
    static void issued(FrameStore& s, std::uint64_t n) { s.issued_ = n; }
    static void sequence(FrameStore& s, std::uint64_t n) { s.sequence_ = n; }
};
}
int main() {
    using S = FrameStatus;
    bool fail = false;
    FrameStore s([&] { if(fail) throw std::bad_alloc(); });
    FrameStore::Token t, old;
    std::optional<Frame> copy;
    assert(s.copy_confirmed(copy)==S::ok && !copy);
    assert(s.prepare(t)==S::suspended);
    for(auto v : {Viewport{-1,1,1}, Viewport{1,-1,1}, Viewport{8193,1,1},
                  Viewport{1,8193,1}, Viewport{8192,8192,1},
                  Viewport{1,1,0}, Viewport{1,1,-1},
                  Viewport{1,1,std::numeric_limits<float>::infinity()},
                  Viewport{1,1,std::numeric_limits<float>::quiet_NaN()}})
        assert(s.resize(v)==S::invalid_viewport);
    assert(s.resize({8192,2048,1})==S::ok); // exact 64 MiB without allocation
    assert(s.resize({8192,2049,1})==S::invalid_viewport);
    assert(s.resize({2048,8192,1})==S::ok);
    assert(s.resize({0,8192,1})==S::ok && s.prepare(t)==S::suspended);
    assert(s.resize({8192,0,1})==S::ok && s.prepare(t)==S::suspended);
    assert(s.resize({2,3,2})==S::ok && s.prepare(t)==S::ok);
    std::span<std::uint8_t> pixels;
    assert(s.candidate(t,pixels)==S::ok && pixels.size()==24);
    pixels[3]=255; pixels[0]=123;
    old=t;
    fail=true;
    assert(s.prepare(t)==S::allocation_failed);
    fail=false;
    assert(s.candidate(old,pixels)==S::ok && pixels[0]==123);
    assert(s.resize({-1,3,1})==S::invalid_viewport);
    assert(s.commit(t)==S::ok && s.commit(t)==S::stale_token);
    pixels={}; // obsolete borrow must not be used after commit
    assert(s.copy_confirmed(copy)==S::ok);
    assert(copy && copy->stride==8 && copy->pixels.size()==24 && copy->sequence==1);
    assert(copy->viewport.width==2 && copy->viewport.height==3 && copy->viewport.dpi==2);
    copy->pixels[0]=10;
    assert(s.copy_confirmed(copy)==S::ok && copy->pixels[0]==123);
    assert(s.resize({4,4,1})==S::ok && s.prepare(t)==S::ok);
    assert(s.resize({4,4,1})==S::ok && s.commit(t)==S::stale_token);
    assert(s.copy_confirmed(copy)==S::ok && copy->viewport.width==2 && copy->sequence==1);
    fail=true;
    copy->pixels[0]=77;
    assert(s.copy_confirmed(copy)==S::allocation_failed && copy->pixels[0]==77);
    fail=false;
    assert(s.copy_confirmed(copy)==S::ok && copy->pixels[0]==123);
    assert(s.prepare(t)==S::ok); old=t;
    assert(s.prepare(t)==S::ok && s.commit(old)==S::stale_token);
    assert(s.discard(old)==S::stale_token && s.discard(t)==S::ok);
    assert(s.discard(t)==S::stale_token);
    assert(s.prepare(t)==S::ok);
    FrameStore other;
    FrameStore::Token foreign;
    assert(other.resize({1,1,1})==S::ok && other.prepare(foreign)==S::ok);
    assert(s.commit(foreign)==S::stale_token && other.commit(t)==S::stale_token);
    assert(s.resize({0,1,1})==S::ok && s.commit(t)==S::stale_token);
    assert(s.copy_confirmed(copy)==S::ok && copy->sequence==1);
    assert(s.resize({1,1,1})==S::ok && s.prepare(t)==S::ok && s.commit(t)==S::ok);
    assert(s.copy_confirmed(copy)==S::ok && copy->sequence==2 && copy->stride==4);
    assert(s.prepare(t)==S::ok);
    FrameStoreTestAccess::issued(s,std::numeric_limits<std::uint64_t>::max());
    assert(s.prepare(old)==S::exhausted && s.candidate(t,pixels)==S::ok);
    FrameStoreTestAccess::sequence(s,std::numeric_limits<std::uint64_t>::max());
    assert(s.commit(t)==S::exhausted && s.candidate(t,pixels)==S::ok);
    assert(s.copy_confirmed(copy)==S::ok && copy->sequence==2);
    pixels={};
    std::thread wrong([&] {
        FrameStore::Token q;
        std::span<std::uint8_t> p;
        std::optional<Frame> c;
        assert(s.resize({1,1,1})==S::wrong_thread);
        assert(s.prepare(q)==S::wrong_thread);
        assert(s.commit(t)==S::wrong_thread);
        assert(s.discard(t)==S::wrong_thread);
        assert(s.candidate(t,p)==S::wrong_thread);
        assert(s.copy_confirmed(c)==S::wrong_thread);
    });
    wrong.join();
    assert(s.discard(t)==S::ok);
}
