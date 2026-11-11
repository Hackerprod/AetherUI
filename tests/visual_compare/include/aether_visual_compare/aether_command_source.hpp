#pragma once
#include "aether_visual_compare/paint_snapshot.hpp"
#include <array>

namespace avc::preparation {
// Types come from the real ABI header, but no real symbols are referenced.
// All callbacks MUST NOT throw, including free. The provider and list must
// remain alive and immutable during this operation. A trusted provider must
// supply readable text memory through NUL or the complete bounded scan range.
// Bounds do not validate pointers or make a hostile provider safe.
struct AetherApi {
 decltype(&aether_abi_version) abi_version{};
 decltype(&aether_abi_capabilities) abi_capabilities{};
 decltype(&aether_paint_command_size) paint_command_size{};
 decltype(&aether_display_list_get_count) get_count{};
 decltype(&aether_display_list_get_command) get_command{};
 decltype(&aether_paint_command_free) free_command{};
};
inline constexpr std::array snapshot_supported_tags{
 AETHER_PAINT_FILL_ROUNDED_RECT, AETHER_PAINT_STROKE_ROUNDED_RECT,
 AETHER_PAINT_DRAW_TEXT, AETHER_PAINT_PUSH_CLIP, AETHER_PAINT_POP_CLIP,
 AETHER_PAINT_PUSH_TRANSFORM, AETHER_PAINT_POP_TRANSFORM,
 AETHER_PAINT_BEGIN_PAINT_CHUNK, AETHER_PAINT_END_PAINT_CHUNK};
using SnapshotResult = std::variant<Snapshot, Error>;
// A successful snapshot owns all strings; it is not an image or a prepared
// frame. prepare() separately validates scopes and revalidates all fields.
SnapshotResult snapshot_commands(const AetherApi&, aether_display_list_t,
                                 Cancellation* = nullptr) noexcept;
} // namespace avc::preparation
