#pragma once
#include "owned_font_decoder.h"
#include "font_provider.h"
#include <array>
namespace aether::rendering::html_ltr {
struct OwnedFontBinding final {
 std::shared_ptr<const owned_text::Face> face;
 std::shared_ptr<const owned_text::Map> map;
 std::shared_ptr<const owned_text::Font> font;
};
struct OwnedFontProduct final {
 std::shared_ptr<const OwnedFontBinding> binding;
 std::shared_ptr<const owned_text::Run> run;
 OwnedVector<uint64_t> prefixes;
 explicit OwnedFontProduct(OwnedBudget::Handle domain):prefixes(OwnedAllocator<uint64_t>(domain,OwnedCategory::glyphs)){}
};
// New storage policy only. Legacy provider and native text storage remain intact.
class PersistentFontProvider final {
 struct Token {};
 OwnedBudget::Handle domain_;
 struct Bundle {std::shared_ptr<const owned_text::Source> source;std::shared_ptr<const owned_text::Face> face;std::shared_ptr<const owned_text::Map> map;};
 Bundle regular_,bold_;
 struct BindingSlot {int size{},weight{};std::shared_ptr<const OwnedFontBinding> value;};
 struct OutlineSlot {const owned_text::Source* source{};uint32_t glyph{};std::shared_ptr<const owned_text::Outline> value;};
 struct CmapSlot {const owned_text::Source* source{};uint32_t scalar{};uint16_t glyph{};};
 std::array<BindingSlot,16> bindings_{};
 std::array<OutlineSlot,256> outlines_{};
 std::array<CmapSlot,384> cmap_{};
 size_t next_outline_{},cmap_size_{};bool active_{};FontProviderUsage work_{};
 Bundle source(std::span<const uint8_t>);
 std::shared_ptr<const OwnedFontBinding> binding(int,int);
 void visit();
public:
 explicit PersistentFontProvider(Token,OwnedBudget::Handle d):domain_(std::move(d)){}
 static std::shared_ptr<PersistentFontProvider> create(OwnedBudget&,const uint8_t*,size_t,const uint8_t*,size_t);
 PersistentFontProvider(const PersistentFontProvider&)=delete;
 class Operation final {
  PersistentFontProvider* p_;
 public:
  explicit Operation(PersistentFontProvider&);
  ~Operation();Operation(const Operation&)=delete;
 };
 Operation operation(){return Operation(*this);}
 const FontProviderUsage& work()const noexcept{return work_;}
 std::shared_ptr<const OwnedFontProduct> product(std::string_view,int size,int weight,int line_height);
 std::shared_ptr<const owned_text::Outline> outline(const std::shared_ptr<const OwnedFontBinding>&,uint32_t glyph);
 const auto& regular_source()const noexcept{return regular_.source;}
 const auto& bold_source()const noexcept{return bold_.source;}
 bool has_bold()const noexcept{return bool(bold_.source);}
 tt::AggregateBudget raster_budget{512000000u},mask_budget{16u*1024u*1024u};
};
}
