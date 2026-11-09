#pragma once

#include "style_runtime_types.h"
#include "../dom/node_identity.h"

// C2 planner foundation. These objects alone do not authorize incremental resolution:
// callers still need the journal, stylesheet/environment and revision certificates.
namespace aether::style::invalidation {
enum class Walk : uint8_t { self, children, descendants, next_sibling, following_siblings, ancestors };
enum class Coverage : uint8_t { proven_superset, document_fallback };
enum class Fallback : uint8_t { none, unsupported_ast, route_budget };

// Only necessary positive tests from the *same* compound as this route position.
// Logical branches, negations, attributes and every dynamic predicate are omitted.
struct StaticFilter {
    std::string tag;
    std::vector<std::string> ids;
    std::vector<std::string> classes;
};
struct Hop { Walk walk = Walk::self; StaticFilter filter; };
struct Occurrence {
    uint32_t state_mask = 0;
    bool forbidden_state = false;
    bool under_negation = false;
    Coverage coverage = Coverage::proven_superset;
    Fallback fallback = Fallback::none;
    StaticFilter source_filter; // necessary at the state occurrence, even on fallback
    std::vector<Hop> route; // changed element -> possible selector subject/host
};
struct Program {
    int32_t rule_id = 0;
    aether_pseudo_element_t slot = aether_pseudo_element_t::none;
    uint32_t state_mask = 0;
    std::vector<Occurrence> occurrences;
};
// Owns all filter data; selector AST/source can be destroyed after compilation.
Program compile(int32_t rule_id, const compiled_selector_s& selector);
// Necessary source filter only, never current dynamic matching.
bool source_applies(const StaticFilter&, aether_node_s*) noexcept;

struct StateChange { aether_node_s* node = nullptr; uint32_t changed_bits = 0; };
struct Candidate {
    aether_node_s* host = nullptr;
    std::shared_ptr<const node_identity_s> token;
    aether_pseudo_element_t slot = aether_pseudo_element_t::none;
    std::vector<int32_t> rules;
    bool used_fallback = false;
};
// Private synchronous reference planner, not an indexed production scheduler.
// DOM/AST must remain stable during this call. No current state or matcher reads.
// Generated/layout-only subtrees are excluded, including for fallback.
// Allocation failure propagates; a partial agenda is never returned as success.
std::vector<Candidate> plan(aether_node_s* root, const std::vector<Program>& programs,
                            const std::vector<StateChange>& changes);
}
