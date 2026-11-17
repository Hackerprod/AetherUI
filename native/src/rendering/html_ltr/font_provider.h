#pragma once
#include "admission.h"
#include "../text/real_font_metrics.h"
#include <map>
#include <string_view>
#include <utility>

namespace aether::rendering::html_ltr {
namespace tt = text::truetype;
struct FontProduct {
 std::shared_ptr<const tt::RealFontBinding> binding;
 std::string source;
 std::vector<text::Glyph> glyphs;
 // Logical design-unit endpoints, including initial zero.
 std::vector<std::uint64_t> prefixes;
 aether_text_metrics_t metrics{};
};
struct FontProviderUsage {
 std::size_t payload{}, instances{}, outlines{}, requests{}, scalar_visits{}, cmap_lookups{};
 std::uint64_t outline_work_reserved{};
};
// Session-local physical resources. No DOM identities, paint authority or public hooks.
class FontProvider final {
 std::shared_ptr<const text::FontSource> regular_, bold_;
 std::map<std::pair<int,int>,std::shared_ptr<const tt::RealFontBinding>> bindings_;
 std::map<std::pair<const text::FontSource*,std::uint32_t>,std::uint16_t> cmap_;
 std::map<std::pair<const text::FontSource*,std::uint32_t>,std::shared_ptr<const tt::Outline>> outlines_;
 FontProviderUsage usage_{};
 void validate_source(const std::shared_ptr<const text::FontSource>&);
 std::shared_ptr<const tt::RealFontBinding> binding(int size, int weight);
 static void reserve(std::size_t n,std::size_t& used,std::size_t limit);
public:
 static constexpr std::size_t payload_limit=32u*1024u*1024u;
 static constexpr std::uint64_t outline_work_limit=256000000u;
 tt::AggregateBudget raster_budget{512000000u};
 tt::AggregateBudget mask_budget{16u*1024u*1024u};
 FontProvider(std::vector<std::uint8_t> regular,std::vector<std::uint8_t> bold);
 FontProvider(const FontProvider&)=delete;
 FontProvider& operator=(const FontProvider&)=delete;
 void reserve_payload(std::size_t bytes);
 const FontProviderUsage& usage()const noexcept{return usage_;}
 bool has_bold()const noexcept{return bool(bold_);}
 const std::shared_ptr<const text::FontSource>& regular_source()const noexcept{return regular_;}
 const std::shared_ptr<const text::FontSource>& bold_source()const noexcept{return bold_;}
 FontProduct product(std::string_view text,int size,int weight,int line_height);
 std::shared_ptr<const tt::Outline> outline(const std::shared_ptr<const tt::RealFontBinding>&,std::uint32_t glyph);
};
}
