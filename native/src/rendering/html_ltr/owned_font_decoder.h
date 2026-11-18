#pragma once
#include "owned_font_transport.h"
#include "../text/detail/font_kernels.h"
namespace aether::rendering::html_ltr::owned_text {
namespace tt=text::truetype;
struct Face final {
 struct Table {size_t offset{},length{};};
 std::shared_ptr<const Source> source_;
 Table glyf_{},hmtx_{},cmap_{};bool has_cmap_{};
 OwnedVector<uint32_t> locations_;
 uint16_t units_{},glyphs_{},horizontal_metrics_{};
 explicit Face(std::shared_ptr<const Source> s):source_(std::move(s)),locations_(OwnedAllocator<uint32_t>(source_->domain(),OwnedCategory::glyphs)){}
 const auto& source()const noexcept{return source_;}
 uint16_t units_per_em()const noexcept{return units_;}
 uint16_t glyph_count()const noexcept{return glyphs_;}
};
struct Outline final {
 std::shared_ptr<const Source> source_;
 uint16_t units_,glyph_id;
 tt::Metrics metrics{};tt::Bounds bounds{};
 OwnedVector<tt::Segment> segments;OwnedVector<tt::Contour> contours;
 Outline(std::shared_ptr<const Source> s,uint16_t units,uint16_t id):source_(std::move(s)),units_(units),glyph_id(id),segments(OwnedAllocator<tt::Segment>(source_->domain(),OwnedCategory::glyphs)),contours(OwnedAllocator<tt::Contour>(source_->domain(),OwnedCategory::glyphs)){}
 const auto& source()const noexcept{return source_;}
 uint16_t units_per_em()const noexcept{return units_;}
};
struct Map final {
 struct Range {uint32_t first,last,glyph;uint16_t delta;size_t array;};
 std::shared_ptr<const Face> face_;
 OwnedVector<Range> ranges_;size_t offset_{};uint16_t format_{};
 explicit Map(std::shared_ptr<const Face> f):face_(std::move(f)),ranges_(OwnedAllocator<Range>(face_->source()->domain(),OwnedCategory::glyphs)){}
 const auto& source()const noexcept{return face_->source();}
};
struct Storage {
 using Source=owned_text::Source;using Face=owned_text::Face;using Outline=owned_text::Outline;using Map=owned_text::Map;
 using OutlineMutable=std::shared_ptr<Outline>;
 static auto make_face(std::shared_ptr<const Source> s){auto a=OwnedAllocator<Face>(s->domain(),OwnedCategory::glyphs);return std::allocate_shared<Face>(a,std::move(s));}
 static auto share_face(std::shared_ptr<Face> p){return p;}
 static auto make_map(std::shared_ptr<const Face> f){auto a=OwnedAllocator<Map>(f->source()->domain(),OwnedCategory::glyphs);return std::allocate_shared<Map>(a,std::move(f));}
 static auto make_outline(std::shared_ptr<const Source> s,uint16_t units,uint16_t id){auto a=OwnedAllocator<Outline>(s->domain(),OwnedCategory::glyphs);return std::allocate_shared<Outline>(a,std::move(s),units,id);}
 static auto freeze_outline(std::shared_ptr<Outline> p){return std::shared_ptr<const Outline>(std::move(p));}
 template<class T> static auto vector(const std::shared_ptr<const Source>& s){return OwnedVector<T>(OwnedAllocator<T>(s->domain(),OwnedCategory::temporary));}
 [[noreturn]] static tt::Error unexpected_outline(){throw;}
 [[noreturn]] static tt::CmapError unexpected_cmap(){throw;}
};
}
