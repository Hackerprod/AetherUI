#pragma once
#include "shaped_run.h"
#include <string_view>
namespace aether::rendering::text {
// Internal contract only. No provider is shipped and no public capture gate is
// enabled. Implementations must return immutable owned values, never HB/FT handles.
class MeasurementProvider {
public:
 virtual ~MeasurementProvider() = default;
 // max_width == 0 means unbounded measurement; layout owns wrapping.
 // Positive values are constraints; negative/nonfinite values are invalid.
 virtual Measurement measure(std::string_view source,
     std::shared_ptr<const FontInstance> font, float max_width) const = 0;
};
enum class MeasurementMode { legacy, native };
class MeasurementSelection final {
public:
 const MeasurementMode mode;
 const std::shared_ptr<const MeasurementProvider> provider;
 const std::shared_ptr<const FontInstance> font;
 MeasurementSelection(MeasurementMode m, std::shared_ptr<const MeasurementProvider> p={},
                      std::shared_ptr<const FontInstance> f={})
   : mode(m),provider(std::move(p)),font(std::move(f)) {
    if(m!=MeasurementMode::legacy && m!=MeasurementMode::native)
      throw std::invalid_argument("unknown measurement mode");
 }
};
inline Measurement native_measure(const MeasurementSelection& selection,
                                  const char* text, float size, float width) {
 if(selection.mode!=MeasurementMode::native || !selection.provider || !selection.font || !text || !std::isfinite(size) ||
    size != float(selection.font->size_26_6)/64.f || !std::isfinite(width) || width<0)
   throw std::invalid_argument("native measurement configuration");
 std::size_t length=0;
 while(length<=max_text_bytes && text[length]) ++length;
 if(length>max_text_bytes) throw std::invalid_argument("native text bound");
 const std::string_view source(text,length);
 // Empty input is absence of text, not a fabricated glyph or run.
 if(source.empty()) return {};
 auto result=selection.provider->measure(source,selection.font,width);
 const auto& run=result.run;
 if(!run || run->source!=source || run->font!=selection.font ||
    result.metrics.width!=run->metrics.width || result.metrics.height!=run->metrics.height ||
    result.metrics.baseline!=run->metrics.baseline)
   throw std::invalid_argument("incoherent native measurement");
 return result;
}
}
