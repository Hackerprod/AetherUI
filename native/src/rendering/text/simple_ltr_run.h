#pragma once
#include "real_font_binding.h"
#include <string_view>
namespace aether::rendering::text::truetype {
struct LineBox { std::int64_t baseline26_6{},height26_6{}; };
enum class RunError { invalid_input,unsupported,missing_glyph,malformed,limit_exceeded,cancelled,out_of_memory,numeric_failure };
// Non-refundable operation-local budgets; legacy wrapper does not use these.
struct RunBudget {
 AggregateBudget outline{256000000},cmap{4352},raster{512000000};
 AggregateBudget masks{64ULL*1024*1024},canvas_pixels{16ULL*1024*1024},composite_visits{16ULL*1024*1024};
};
struct RunWork {
 std::uint64_t decoded{},lookups{},lookup_comparisons{},outline_reserved{},outline_units{},raster_reserved{},raster_units{},retained_mask_bytes{},source_pixel_visits{},clear_pixels{},conversion_pixels{},cancel_checks{};
 std::uint64_t cmap_reserved{},mask_reserved{},canvas_reserved{},composite_reserved{};
 bool composition_started{},canvas_complete{};
};
struct RunGlyph {
 std::uint16_t glyph_id{}; std::size_t cluster_byte{};
 std::int64_t advance_26_6{},pen_26_6{},bitmap_x{},bitmap_y{};
 std::shared_ptr<const GlyphMaskA8> mask;
};
struct RunBounds { std::int64_t x{},y{},width{},height{}; };
struct RunCanvas { std::int64_t origin_x{},origin_y{}; std::uint32_t width{},height{}; std::vector<std::uint8_t> rgba; };
class PreparedSimpleRun;
using RunResult=std::variant<std::shared_ptr<const PreparedSimpleRun>,RunError>;
RunResult build_simple_ltr_run(std::shared_ptr<const RealFontBinding>,std::string_view,LineBox,Cancellation* =nullptr,RunWork* =nullptr) noexcept;
RunResult build_simple_ltr_run_budgeted(std::shared_ptr<const RealFontBinding>,std::string_view,LineBox,RunBudget&,Cancellation* =nullptr,RunWork* =nullptr) noexcept;
// Fixed simple_ltr_unhinted_no_kerning profile. This is not HTML or shaping.
// mask_bounds are conservative bitmap rectangles, not tight ink bounds.
class PreparedSimpleRun final {
 friend struct SimpleRunBuilder;
 friend RunResult build_simple_ltr_run(std::shared_ptr<const RealFontBinding>,std::string_view,LineBox,Cancellation*,RunWork*) noexcept;
 PreparedSimpleRun(std::shared_ptr<const RealFontBinding> b,std::string s,std::vector<RunGlyph> g,LineBox l,std::int64_t w,RunBounds bounds,RunCanvas c)
 :binding(std::move(b)),source_text(std::move(s)),glyphs(std::move(g)),linebox(l),width_26_6(w),mask_bounds(bounds),canvas(std::move(c)){}
public:
 PreparedSimpleRun(const PreparedSimpleRun&)=delete; PreparedSimpleRun(PreparedSimpleRun&&)=delete;
 const std::shared_ptr<const RealFontBinding> binding;
 const std::string source_text;
 const std::vector<RunGlyph> glyphs;
 const LineBox linebox;
 const std::int64_t width_26_6;
 const RunBounds mask_bounds;
 const RunCanvas canvas;
};
}
