#pragma once
#include <string>
#include <cstddef>
// Requires the same initialized builtin registry as style_source_compile.
// Does not initialize global state, publish properties or invoke diagnostics.
// Private compile-time evidence. Coverage is accumulated before boolean folding.
struct supports_condition_evidence_s {
    bool syntax_valid=false, matches=false, profile_supported=false, budget_exhausted=false;
    size_t consumed=0;
};
supports_condition_evidence_s supports_condition_probe(const std::string&, size_t& remaining_budget);
