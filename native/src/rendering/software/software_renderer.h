#pragma once
#include "../native_frame.h"
#include <aether/rendering/preparation/paint_snapshot.hpp>
namespace aether::rendering::software {
namespace prep = aether::rendering::preparation;
enum class Status { ok, unsupported, limit_exceeded, cancelled, allocation_failed, frame_failed };
struct Result {
 Status status{Status::ok};
 FrameStatus frame_status{FrameStatus::ok};
 std::size_t command_index{};
 std::uint64_t pixel_budget{}; // Includes transparent clear. Not a time metric.
 std::uint64_t corner_pixel_bound{}; // Sum across rounded draws, on success.
 std::uint64_t sampled_pixels{}; // Each incurs 256 membership tests, on success.
 std::uint64_t inner_corner_pixel_bound{}; // Rounded stroke inner shapes, separate.
 std::uint64_t inner_sampled_pixels{};
};
// R10: uniform circular rounded fills (central bands + grid16x16), uniform rounded strokes, rectangle clips; DPI=1.
// Owner-thread synchronous call. No reentry or store mutation by callbacks.
// Unsupported commands reject the whole frame before candidate allocation.
// Allocation and all failures preserve the confirmed frame.
Result render(FrameStore&, const prep::PreparedFrame&, prep::Cancellation* = nullptr) noexcept;
}
