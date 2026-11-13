#pragma once
#include <string>
#include <vector>
#include <stdexcept>
namespace aether::rendering::text {
// Diagnostic only: this cannot certify CSS eligibility or SDK availability.
enum class ContextReadiness { unknown, unsupported, contract_complete };
struct ShapingContext {
 bool complete=false;
 std::string family, language, direction, features;
 int weight=0;
 bool italic=false, vertical=false, fallback=false, variations=false;
 bool operator==(const ShapingContext&) const = default;
};
inline ContextReadiness inspect_context(const ShapingContext& c, bool sdk_available) {
 if(!c.complete || c.family.empty() || c.language.empty() || c.direction.empty() || c.features.empty() || c.weight<=0 || !sdk_available)
  return ContextReadiness::unknown;
 if(c.direction!="ltr" || c.vertical || c.fallback || c.variations) return ContextReadiness::unsupported;
 return ContextReadiness::contract_complete;
}
inline void require_context(const ShapingContext& requested,const ShapingContext& selected) {
 if(inspect_context(requested,true)!=ContextReadiness::contract_complete ||
    inspect_context(selected,true)!=ContextReadiness::contract_complete || requested!=selected)
  throw std::invalid_argument("native shaping context missing, unsupported or mismatched");
}
struct SourcePiece { std::string text; ShapingContext context; bool forced_break=false; };
// Preparation contract, NOT the layout implementation: preserve complete source
// context across nodes. Whitespace must already be resolved by the CSS owner;
// never collapse here. Caller keeps offsets, including zero-length pieces.
struct ContextGroup { std::string text; std::vector<std::size_t> offsets; ShapingContext context; };
inline ContextGroup prepare_context_group(const std::vector<SourcePiece>& pieces) {
 ContextGroup out;
 if(pieces.empty()) return out;
 out.context=pieces.front().context;
 for(const auto& p:pieces) {
  require_context(p.context,out.context);
  if(p.forced_break) throw std::invalid_argument("forced break requires a separate group");
  out.offsets.push_back(out.text.size());out.text+=p.text;
 }
 return out;
}
}
