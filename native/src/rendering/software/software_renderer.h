#pragma once
#include "../native_frame.h"
#include "../text/glyph_mask_a8.h"
#include <aether/rendering/preparation/paint_snapshot.hpp>
namespace aether::rendering::software {
namespace prep = aether::rendering::preparation;
enum class Status { ok, unsupported, limit_exceeded, cancelled, allocation_failed, frame_failed, numeric_error, missing_mask, invalid_mask };
struct Result {
 Status status{Status::ok};
 FrameStatus frame_status{FrameStatus::ok};
 std::size_t command_index{};
 std::uint64_t pixel_budget{}; // Includes transparent clear. Not a time metric.
 std::uint64_t corner_pixel_bound{}; // Sum across rounded draws, on success.
 std::uint64_t sampled_pixels{}; // Each incurs 256 membership tests, on success.
 std::uint64_t inner_corner_pixel_bound{}; // Rounded stroke inner shapes, separate.
 std::uint64_t inner_sampled_pixels{};
 std::uint64_t clip_corner_pixel_bound{};
 std::uint64_t joint_sampled_pixels{};
 std::uint64_t mask_count{}, mask_bytes{}, glyph_uses{}, mask_pixel_visits{};
 std::uint64_t preparation_ns{}, composition_ns{}; // measured only for successful render
 std::uint64_t clip_central_tests{}, clip_contains_tests{}; // Actual node predicates.
 std::uint64_t clip_node_visits{}; // Preflight + central + sample chain visits.
 std::uint64_t clip_node_count{}, max_clip_depth{}; // Persistent per-frame metadata.
};
// Uniform circular legacy paths unchanged; resolved independent elliptical fill/gradient/clip radii use joint grid16x16.
// Side sums must fit (zero-component corners are rectangular); no CSS radius normalization. Strokes remain uniform; DPI=1.
// S1: positive uniform local/effective scales in [1/16,16], bounded device
// coordinates/translation/radii (+/-65536). Fill, gradient, Image, clip and opacity only
// under effective nonidentity scale. Transform state composes in double.
// Any nonidentity local scale + any PushScroll rejects globally at the second
// relevant command, even in separate scopes or if scales cancel. DPI stays 1.
// Clips capture device geometry and scale at push; gradients retain full ramps.
// S1-I: Image scales destination only; source texels/crops and bilinear center
// sampling remain unchanged. Clip preserves the original transformed UV domain.
// StrictCrop clamps to integer crop; FullResource clamps to the whole resource.
// Owner-thread synchronous call. No reentry or store mutation by callbacks.
// Unsupported commands reject the whole frame before candidate allocation.
// Allocation and all failures preserve the confirmed frame.
Result render(FrameStore&, const prep::PreparedFrame&, prep::Cancellation* = nullptr) noexcept;
// Explicit synthetic/precomputed A8 input only; all nonidentity local scales reject. Ordinary render still rejects text.
// No provider registration, HTML gate, or public ABI change.
Result render_a8(FrameStore&, const prep::PreparedFrame&,
 std::span<const std::shared_ptr<const text::GlyphMaskA8>>, prep::Cancellation* = nullptr) noexcept;
}
