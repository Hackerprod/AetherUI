#ifdef NDEBUG
#error Assertions required
#endif
#ifndef AETHER_SURFACE_TESTING
#error Private wrapper harness must use AETHER_SURFACE_TESTING
#endif
#include "../src/abi/surface_test_hooks.h"
#include "../src/rendering/display_list_types.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
using aether::surface_test::Stage;
namespace {
bool active=false, armed=false, injected=false, unexpected=false;
Stage target=Stage::create, current=Stage::create, failed_at=Stage::create;
std::size_t live=0, failed_bytes=0;
}
// Single-threaded private harness. All authentic private objects are linked into
// this executable; no allocator or failure hooks are added to the shipped DSO.
void* operator new(std::size_t bytes) {
    if(armed) { armed=false; injected=true; failed_at=current; failed_bytes=bytes; throw std::bad_alloc(); }
    void* p=std::malloc(bytes ? bytes : 1);
    if(!p) throw std::bad_alloc();
    ++live; return p;
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { if(p) { --live; std::free(p); } }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p,std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }
namespace aether::surface_test {
void stage(Stage s) {
    if(s==Stage::unexpected) { if(unexpected) throw 42; return; }
    if(s==Stage::allocation) {
        if(current!=Stage::copy) current=Stage::candidate;
        if(active && !injected && target==current) armed=true;
        return;
    }
    current=s;
    if(active && !injected && s==target && s!=Stage::candidate && s!=Stage::copy) armed=true;
}
}
static void arm(Stage s) { target=s; active=true; armed=false; injected=false; failed_bytes=0; }
static void disarm(Stage expected) {
    active=false; armed=false;
    assert(injected && failed_at==expected && failed_bytes>0);
    std::printf("OOM stage=%d allocation_bytes=%zu\n",int(expected),failed_bytes);
}
struct Saved { aether_surface_frame_info info{}; std::array<unsigned char,64> pixels{}; };
static Saved save(aether_surface_t s) {
    Saved out; out.info.struct_size=sizeof(out.info);
    assert(aether_surface_copy_frame(s,&out.info,out.pixels.data(),out.pixels.size())==AETHER_OK);
    return out;
}
static void same(aether_surface_t s,const Saved& before) {
    const auto after=save(s);
    assert(after.info.width==before.info.width && after.info.height==before.info.height);
    assert(after.info.sequence==before.info.sequence && after.info.stride==before.info.stride);
    assert(after.info.byte_count==before.info.byte_count && after.info.format==before.info.format);
    assert(after.pixels==before.pixels);
}
int main() {
    const auto initial_live=live;
    {
        // Only the private harness constructs a list directly. Public C tests use
        // DOM/style/layout APIs and never supply fake pointers or corrupt lists.
        aether_display_list_s list;
        aether_paint_command_t c{}; c.tag=AETHER_PAINT_FILL_ROUNDED_RECT;
        c.data.fill_rounded_rect.rect={0,0,4,4};
        c.data.fill_rounded_rect.color={1,0,0,1}; list.commands.push_back(c);
        const auto baseline=live;
        aether_surface_t s=reinterpret_cast<aether_surface_t>(1);
        arm(Stage::create);
        assert(aether_surface_create(4,4,&s)==AETHER_ERROR_OUT_OF_MEMORY);
        disarm(Stage::create); assert(s==nullptr && live==baseline);
        assert(aether_surface_create(4,4,&s)==AETHER_OK);
        assert(aether_surface_render(s,&list)==AETHER_OK);
        auto before=save(s); assert(before.info.sequence==1);
        for(auto step:{Stage::snapshot,Stage::prepare,Stage::candidate}) {
            const auto allocations_before=live;
            arm(step);
            assert(aether_surface_render(s,&list)==AETHER_ERROR_OUT_OF_MEMORY);
            disarm(step); assert(live==allocations_before); same(s,before);
            assert(aether_surface_render(s,&list)==AETHER_OK);
            auto next=save(s); assert(next.info.sequence==before.info.sequence+1); before=next;
        }
        for(bool query:{false,true}) {
            auto info=before.info; auto original=info;
            std::array<unsigned char,80> output; output.fill(0xA7);
            const auto allocations_before=live;
            arm(Stage::copy);
            assert(aether_surface_copy_frame(s,&info,query?nullptr:output.data(),query?0:output.size())==AETHER_ERROR_OUT_OF_MEMORY);
            disarm(Stage::copy); assert(live==allocations_before);
            assert(std::memcmp(&info,&original,sizeof(info))==0);
            for(auto byte:output) assert(byte==0xA7);
            same(s,before);
        }
        // Invalid payload/stack testing is confined to this private harness.
        list.commands[0].data.fill_rounded_rect.rect.width=std::numeric_limits<float>::quiet_NaN();
        assert(aether_surface_render(s,&list)==AETHER_ERROR_INVALID_ARGUMENT); same(s,before);
        list.commands[0]=c;
        list.commands[0].tag=AETHER_PAINT_POP_CLIP;
        assert(aether_surface_render(s,&list)==AETHER_ERROR_INVALID_ARGUMENT); same(s,before);
        list.commands[0]=c;
        unexpected=true;
        assert(aether_surface_render(s,&list)==AETHER_ERROR_INTERNAL);
        unexpected=false; same(s,before);
        assert(aether_surface_render(s,&list)==AETHER_OK); before=save(s);
        // Counter access is private and does not alter the confirmed frame.
        aether::surface_test::set_counter(s,false,std::numeric_limits<std::uint64_t>::max());
        assert(aether_surface_render(s,&list)==AETHER_ERROR_EXHAUSTED); same(s,before);
        aether::surface_test::set_counter(s,false,0);
        aether::surface_test::set_counter(s,true,std::numeric_limits<std::uint64_t>::max());
        assert(aether_surface_render(s,&list)==AETHER_ERROR_EXHAUSTED); same(s,before);
        aether::surface_test::set_counter(s,true,before.info.sequence);
        assert(aether_surface_render(s,&list)==AETHER_OK);
        assert(save(s).info.sequence==before.info.sequence+1);
        assert(aether_surface_destroy(s)==AETHER_OK); assert(live==baseline);
    }
    assert(live==initial_live);
    std::puts("PASS private surface OOM, cleanup, atomicity, recovery, overflow and exception boundary");
}
