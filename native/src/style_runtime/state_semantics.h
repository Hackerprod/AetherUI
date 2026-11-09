#pragma once
#include "../css/frozen_node_record.h"
namespace aether::style {
using SemanticDelta = bool;
using PropagationDelta = uint32_t;
struct StateSemanticDelta {
    SemanticDelta semantic = false;
    PropagationDelta propagation = 0;
};
// Allocation-free complete comparison, independent of visual impact.
StateSemanticDelta compare_node_semantics(const FrozenNodePayload&, const FrozenNodePayload&) noexcept;
bool node_projection_equal(const FrozenNodePayload&, const FrozenNodePayload&) noexcept;
bool node_semantically_equal(const FrozenNodePayload&, const FrozenNodePayload&) noexcept;
}
