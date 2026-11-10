#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>

struct aether_node_s;
struct compiled_selector_s;

namespace aether::selector_position {
// Zero is reserved for nonmembership. Count directly in one-based ordinals so
// neither the scan nor the nth predicate can overflow a signed index + 1.
inline size_t next_ordinal(size_t current) {
    if (current == std::numeric_limits<size_t>::max())
        throw std::length_error("DOM sibling ordinal exceeds size_t");
    return current + 1;
}
inline bool work_budget(size_t raw_count, size_t& budget) noexcept {
    constexpr size_t factor = 16;
    if (raw_count > std::numeric_limits<size_t>::max() / factor) return false;
    budget = raw_count * factor;
    return true;
}
}

// Internal, noncopyable scratch state for one immutable-topology prepare_tree
// traversal only. Never retain it in runtime/transaction/DOM state or across user
// callbacks. Generic selector_match remains uncached for all other callers.
class selector_match_context_s {
public:
    selector_match_context_s() = default;
    selector_match_context_s(const selector_match_context_s&) = delete;
    selector_match_context_s& operator=(const selector_match_context_s&) = delete;
    selector_match_context_s(selector_match_context_s&&) = delete;
    selector_match_context_s& operator=(selector_match_context_s&&) = delete;
    size_t position(aether_node_s* node);

private:
    void remember_position(aether_node_s* parent, aether_node_s* node,
                           size_t position, size_t visited);
    using positions_s = std::unordered_map<aether_node_s*, size_t>;
    struct parent_s {
        positions_s positions;
        size_t remaining;
        size_t observed = 0;
        bool complete = false;
        explicit parent_s(size_t budget) : remaining(budget) {}
    };
    using parents_s = std::unordered_map<aether_node_s*, parent_s>;
    // Some standard libraries allocate even for an empty unordered_map. Delay
    // its construction too, guaranteeing no cache allocation for cheap/no-nth.
    std::optional<parents_s> parents_;
    friend struct selector_match_context_test_access;
};

bool selector_match(const compiled_selector_s& selector, aether_node_s* element,
                    selector_match_context_s& context);
