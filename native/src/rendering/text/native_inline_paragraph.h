#pragma once
#include "shaped_run.h"
#include <limits>
#include <string_view>

namespace aether::rendering::text {
// Resource identity, not authority to perform layout and not proof of a parsed
// real font face. Published values never retain a provider, session or DOM.
enum class FaceSealKind { SyntheticTest };
class SyntheticFaceSeal final {
public:
 const FaceSealKind kind = FaceSealKind::SyntheticTest;
 const std::shared_ptr<const FontInstance> font;
 const std::string fixture_id, language, script, features;
 SyntheticFaceSeal(std::shared_ptr<const FontInstance> f, std::string fixture,
                   std::string lang="en", std::string scr="Latn", std::string feat="default")
 : font(std::move(f)),fixture_id(std::move(fixture)),language(std::move(lang)),
   script(std::move(scr)),features(std::move(feat)) {
  if(!font || fixture_id.empty() || language.empty() || script!="Latn" || features.empty())
   throw std::invalid_argument("incomplete synthetic face seal");
 }
};
// Each raw byte is represented once. Multiple space ranges may point to the
// same output byte. Empty/stripped ranges have output_length zero. These are
// provenance offsets, NOT glyph geometry or permission to slice a shaped run.
struct SourceMapRange {
 std::size_t piece{}, source_begin{}, source_length{}, output_begin{}, output_length{};
 bool operator==(const SourceMapRange&) const = default;
};
class NativeInlineParagraph final {
 struct Normalized { std::string text; std::vector<SourceMapRange> map; };
 static Normalized normalize(const std::vector<std::string>& pieces) {
  if(pieces.size()>max_glyphs) throw std::invalid_argument("paragraph piece bound");
  std::size_t total=0;
  for(const auto& s:pieces) {
   if(s.size()>max_text_bytes-total) throw std::invalid_argument("paragraph source bound");
   total+=s.size();
   for(unsigned char c:s) if(c<0x20 || c>0x7e)
    throw std::invalid_argument("synthetic paragraph requires printable ASCII or U+0020");
  }
  std::string raw;raw.reserve(total);
  for(const auto& s:pieces) raw+=s;
  Normalized out;
  std::vector<std::size_t> anchors(total+1), lengths(total,0);
  std::size_t pos=0;
  while(pos<total) {
   if(raw[pos]!=' ') {
    anchors[pos]=out.text.size();lengths[pos]=1;
    out.text+=raw[pos++];
   } else {
    const auto start=pos;
    while(pos<total && raw[pos]==' ') ++pos;
    const bool emit=start!=0 && pos!=total;
    const auto anchor=out.text.size();
    if(emit) out.text+=' ';
    for(auto i=start;i<pos;++i) {anchors[i]=anchor;lengths[i]=emit?1:0;}
   }
  }
  anchors[total]=out.text.size();
  std::size_t offset=0;
  out.map.reserve(total+pieces.size());
  for(std::size_t p=0;p<pieces.size();++p) {
   if(pieces[p].empty()) out.map.push_back({p,0,0,anchors[offset],0});
   for(std::size_t i=0;i<pieces[p].size();++i)
    out.map.push_back({p,i,1,anchors[offset+i],lengths[offset+i]});
   offset+=pieces[p].size();
  }
  return out;
 }
 NativeInlineParagraph(Normalized n,std::shared_ptr<const SyntheticFaceSeal> s,int)
 :text(std::move(n.text)),source_map(std::move(n.map)),seal(std::move(s)) {
  if(!seal) throw std::invalid_argument("missing synthetic paragraph seal");
 }
public:
 const std::string text;
 const std::vector<SourceMapRange> source_map;
 const std::shared_ptr<const SyntheticFaceSeal> seal;
 NativeInlineParagraph(const std::vector<std::string>& pieces,
                       std::shared_ptr<const SyntheticFaceSeal> s)
 :NativeInlineParagraph(normalize(pieces),std::move(s),0) {}
};
struct ContextualMeasurement {
 std::shared_ptr<const NativeInlineParagraph> paragraph;
 std::size_t begin{}, end{};
 Measurement measured;
};
class ContextualMeasurementProvider {
public:
 virtual ~ContextualMeasurementProvider() = default;
 // Only [0,text.size()) at max_width=0 is admitted by the initial nowrap path.
 // Empty paragraphs must not be sent to a provider. The retained request is
 // immutable; providers must not return or stash mutable runtime handles in it.
 virtual ContextualMeasurement measure(std::shared_ptr<const NativeInlineParagraph> paragraph,
     std::size_t begin,std::size_t end,float max_width) const = 0;
};
inline void validate_contextual_measurement(const std::shared_ptr<const NativeInlineParagraph>& owner,
                                             const ContextualMeasurement& response) {
 if(!owner || response.paragraph!=owner || response.begin!=0 || response.end!=owner->text.size())
  throw std::invalid_argument("contextual request identity or range mismatch");
 const auto& paragraph=*owner;
 const auto& result=response.measured;
 const auto& run=result.run;
 if(paragraph.text.empty() || !run || !paragraph.seal ||
    run->paragraph!=owner || run->source!=paragraph.text || run->font!=paragraph.seal->font ||
    run->glyphs.empty() || run->glyphs.front().cluster!=0 ||
    result.metrics.width!=run->metrics.width ||
    result.metrics.height!=run->metrics.height ||
    result.metrics.baseline!=run->metrics.baseline)
  throw std::invalid_argument("incoherent contextual measurement identity");
 std::int64_t sum=0;
 std::uint32_t previous=0;
 for(const auto& glyph:run->glyphs) {
  if(glyph.cluster<previous || glyph.cluster>=paragraph.text.size() ||
     glyph.advance_x<0 || glyph.advance_y!=0)
   throw std::invalid_argument("invalid synthetic LTR cluster or advance");
  previous=glyph.cluster;
  if(sum>std::numeric_limits<std::int64_t>::max()-glyph.advance_x)
   throw std::overflow_error("synthetic advance sum overflow");
  sum+=glyph.advance_x;
 }
 // ShapedRun validates finite/bounded metrics and offsets; require its logical
 // width to be exactly the checked 26.6 sum. Offsets do not change advance width.
 if(sum>65536LL*64 || static_cast<double>(run->metrics.width)!=static_cast<double>(sum)/64.)
  throw std::invalid_argument("synthetic logical width differs from advance sum");
}
}
