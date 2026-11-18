#pragma once
#include "font_source.h"
#include <variant>
namespace aether::rendering::text::truetype {
namespace detail { struct FontKernel; struct LegacyFontStorage; }
inline constexpr std::size_t max_tables=128, max_contours=256, max_points=4096, max_segments=8192;
enum class Error { malformed, unsupported, limit_exceeded, out_of_memory, cancelled };
// Synchronous, noexcept; no reentry or mutation of inputs from this callback.
struct Cancellation { virtual ~Cancellation()=default; virtual bool cancelled() noexcept=0; };
struct Work { std::size_t bytes_checked{},tables_checked{},points_decoded{},segments_retained{},contours_retained{},units{},cancel_checks{},component_references{},max_component_depth{}; };
struct Point { double x{},y{}; bool operator==(const Point&) const=default; };
enum class SegmentKind { line, quadratic };
struct Segment { SegmentKind kind{}; Point from{}, control{}, to{}; };
struct Contour { std::size_t first_segment{}, segment_count{}; };
struct Metrics { std::uint16_t advance{}; std::int16_t lsb{}; std::int32_t pp1{}; };
struct Bounds { std::int16_t x_min{},y_min{},x_max{},y_max{}; };
// Design units, Y up, unhinted; no shaping, scaling, masks or rasterization.
// Composite profile: integer XY translations only, depth <=16 edges, <=256
// expanded references. Outline limits are aggregate, including repeated DAG leaves.
// Segment cap is defensive (dominated by the point cap for admitted geometry).
class Outline final {
 friend struct detail::FontKernel;
 friend struct detail::LegacyFontStorage;
 const std::shared_ptr<const FontSource> source_;
 const std::uint16_t units_;
 friend class ValidatedFace;
 Outline(std::shared_ptr<const FontSource> s,std::uint16_t units,std::uint16_t id)
  :source_(std::move(s)),units_(units),glyph_id(id){}
public:
 Outline(const Outline&)=delete; Outline(Outline&&)=delete;
 Outline& operator=(const Outline&)=delete; Outline& operator=(Outline&&)=delete;
 const std::uint16_t glyph_id;
 Metrics metrics{}; Bounds bounds{};
 std::vector<Segment> segments; std::vector<Contour> contours;
 const std::shared_ptr<const FontSource>& source()const noexcept{return source_;}
 const std::string& digest()const noexcept{return source_->digest;}
 std::uint32_t face_index()const noexcept{return 0;}
 std::uint16_t units_per_em()const noexcept{return units_;}
};
using OutlineResult=std::variant<std::shared_ptr<const Outline>,Error>;
class ValidatedFace;
using FaceResult=std::variant<std::shared_ptr<const ValidatedFace>,Error>;
// Directory/metrics/loca validation does not imply support for every glyph.
class UnicodeMap;
class ValidatedFace final {
 friend struct detail::FontKernel;
 friend struct detail::LegacyFontStorage;
 friend class UnicodeMap;
 struct Table { std::size_t offset{},length{}; };
 std::shared_ptr<const FontSource> source_;
 Table glyf_{},hmtx_{},cmap_{};
 bool has_cmap_{};
 std::vector<std::uint32_t> locations_;
 std::uint16_t units_{},glyphs_{},horizontal_metrics_{};
 explicit ValidatedFace(std::shared_ptr<const FontSource> s):source_(std::move(s)){}
public:
 ValidatedFace(const ValidatedFace&)=delete; ValidatedFace& operator=(const ValidatedFace&)=delete;
 // Optional work is diagnostic progress even on failure, never a partial face.
 static FaceResult create(std::shared_ptr<const FontSource>,std::uint32_t face_index=0,Cancellation* =nullptr,Work* =nullptr) noexcept;
 OutlineResult outline(std::uint32_t glyph_id,Cancellation* =nullptr,Work* =nullptr) const noexcept;
 const std::shared_ptr<const FontSource>& source() const noexcept{return source_;}
 const std::string& digest() const noexcept{return source_->digest;}
 std::uint32_t face_index() const noexcept{return 0;}
 std::uint16_t units_per_em() const noexcept{return units_;}
 std::uint16_t glyph_count() const noexcept{return glyphs_;}
 // Logical existing geometry payload only, excludes capacity, source and allocator.
 std::size_t logical_payload_bytes()const noexcept{return sizeof(ValidatedFace)+locations_.size()*sizeof(std::uint32_t);}
};
}
