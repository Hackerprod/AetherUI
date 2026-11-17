#pragma once
#include "truetype_outline.h"
namespace aether::rendering::text::truetype {
inline constexpr std::size_t max_cmap_bytes=1024*1024, max_cmap_records=128,
 max_cmap_segments=8192, max_cmap_groups=65536, max_cmap_work=1000000;
enum class CmapError { invalid_input, malformed, unsupported, limit_exceeded, cancelled, out_of_memory };
struct CmapWork { std::size_t units{}, cancel_checks{}, records{}, ranges{}, codepoints{}, comparisons{}; };
struct Mapped { std::uint16_t glyph_id; };
struct Missing {};
using LookupResult=std::variant<Mapped,Missing,CmapError>;
class UnicodeMap;
using CmapResult=std::variant<std::shared_ptr<const UnicodeMap>,CmapError>;
class UnicodeMap final {
 struct Range { std::uint32_t first,last,glyph; std::uint16_t delta; std::size_t array; };
 std::shared_ptr<const ValidatedFace> face_;
 std::vector<Range> ranges_;
 std::size_t offset_{};
 std::uint16_t format_{};
 explicit UnicodeMap(std::shared_ptr<const ValidatedFace> f):face_(std::move(f)){}
public:
 UnicodeMap(const UnicodeMap&)=delete; UnicodeMap& operator=(const UnicodeMap&)=delete;
 static CmapResult create(std::shared_ptr<const ValidatedFace>,Cancellation* =nullptr,CmapWork* =nullptr) noexcept;
 LookupResult lookup(std::uint32_t scalar,CmapWork* =nullptr)const noexcept;
 const std::shared_ptr<const ValidatedFace>& face()const noexcept{return face_;}
 const std::shared_ptr<const FontSource>& source()const noexcept{return face_->source();}
 const std::string& digest()const noexcept{return face_->digest();}
 std::uint16_t format()const noexcept{return format_;}
 // Logical existing map payload only, excludes capacity and shared face/source.
 std::size_t logical_payload_bytes()const noexcept{return sizeof(UnicodeMap)+ranges_.size()*sizeof(Range);}
};
}
