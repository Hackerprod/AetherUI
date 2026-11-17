#pragma once
#include "truetype_outline.h"
#include "glyph_mask_a8.h"
namespace aether::rendering::text::truetype {
inline constexpr std::uint32_t q1_unhinted=1;
inline constexpr std::uint64_t max_raster_work=50000000;
enum class RasterError { invalid_input, unsupported, numeric_failure, budget_exceeded, out_of_memory, cancelled };
// split_segments counts retained nonhorizontal pieces (the scan cost S).
// generated_pieces includes horizontal pieces and is independently capped.
struct RasterWork {
 // Document-only reservation diagnostics; zero on unchanged legacy entrypoints.
 std::uint64_t edge_scan_pairs{},active_pairs{},prepass_work{},prepass_reserved{},main_reserved{};
 std::uint64_t transformed_segments{},generated_pieces{},split_segments{},edge_checks{},root_steps{},sort_comparisons{},sort_swaps{},sweep_operations{},sample_tests{},output_operations{},reserved{},aggregate_charged{},mask_charged{},units{},cancel_checks{};
};
// Non-refundable reservation, including a later allocation failure/cancellation.
// Single caller thread; not a process CPU budget or shared concurrent counter.
struct AggregateBudget { std::uint64_t remaining{}; };
struct RasterOutput { std::shared_ptr<const GlyphMaskA8> mask; double advance_px{}; std::int32_t advance_26_6{}; };
using RasterResult=std::variant<RasterOutput,RasterError>;
// Unhinted design-unit outlines only. No HTML/provider/shaping registration.
RasterResult rasterize(std::shared_ptr<const Outline>,std::shared_ptr<const FontInstance>,
 unsigned phase_x,unsigned phase_y,Cancellation* =nullptr,RasterWork* =nullptr,AggregateBudget* =nullptr) noexcept;
// Masks charged before A8 allocation, even if subsequently deduplicated.
// Document-only tighter reservation. Pixel algorithm and legacy reservations unchanged.
RasterResult rasterize_document_budgeted(std::shared_ptr<const Outline>,std::shared_ptr<const FontInstance>,unsigned phase_x,unsigned phase_y,AggregateBudget& raster,AggregateBudget& mask_bytes,Cancellation* =nullptr,RasterWork* =nullptr) noexcept;
RasterResult rasterize_budgeted(std::shared_ptr<const Outline>,std::shared_ptr<const FontInstance>,
 unsigned phase_x,unsigned phase_y,AggregateBudget& raster,AggregateBudget& mask_bytes,Cancellation* =nullptr,RasterWork* =nullptr) noexcept;
}
