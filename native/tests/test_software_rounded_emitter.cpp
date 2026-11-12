#ifdef NDEBUG
#error Assertions required
#endif
// Includes the real TU to exercise its private command emitter, not a copy.
// Section GC omits unrelated engine paths. This is not a full HTML test.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#include "../src/rendering/display_list_builder.cpp"
#pragma GCC diagnostic pop
#include "../src/rendering/software/software_renderer.h"
#include <aether/rendering/preparation/aether_command_source.hpp>
#include <cassert>
namespace p=aether::rendering::preparation;
int main(){
 aether_display_list_s list{};
 emit_fill_rounded_rect(list.commands,{1,1,8,8},{3,3,3,3,3,3,3,3},{1,0,0,1});
 p::AetherApi api{&aether_abi_version,&aether_abi_capabilities,&aether_paint_command_size,&aether_display_list_get_count,&aether_display_list_get_command,&aether_paint_command_free};
 auto snap=p::snapshot_commands(api,&list);assert(std::holds_alternative<p::Snapshot>(snap));
 assert(std::get<p::Fill>(std::get<p::Snapshot>(snap).commands[0]).radii.top_left_x==3);
 auto prepared=p::prepare(std::get<p::Snapshot>(snap));assert(std::holds_alternative<p::PreparedFrame>(prepared));
 aether::rendering::FrameStore store;assert(store.resize({12,12,1})==aether::rendering::FrameStatus::ok);
 assert(aether::rendering::software::render(store,std::get<p::PreparedFrame>(prepared)).status==aether::rendering::software::Status::ok);
 std::optional<aether::rendering::Frame> f;assert(store.copy_confirmed(f)==aether::rendering::FrameStatus::ok&&f);
 assert(f->pixels[4*f->stride+4*4+3]==255);assert(f->pixels[f->stride+4+3]<255);
}
