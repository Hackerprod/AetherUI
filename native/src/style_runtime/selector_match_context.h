#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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
    // Only the verified, callback-free preparation opts in. Generic callers
    // preserve their original matching path and allocate no ancestor scratch.
    explicit selector_match_context_s(bool enable_ancestor_ids = false)
        : ancestor_ids_enabled_(enable_ancestor_ids) {}
    selector_match_context_s(const selector_match_context_s&) = delete;
    selector_match_context_s& operator=(const selector_match_context_s&) = delete;
    selector_match_context_s(selector_match_context_s&&) = delete;
    selector_match_context_s& operator=(selector_match_context_s&&) = delete;
    size_t position(aether_node_s* node);
    aether_node_s* previous_sibling(aether_node_s* node);

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
    void remember_previous(aether_node_s* parent, aether_node_s* node,
                           aether_node_s* previous, size_t position, size_t visited);
    using previous_s = std::unordered_map<aether_node_s*, aether_node_s*>;
    struct previous_parent_s {
        previous_s previous;
        size_t remaining;
        size_t observed = 0;
        bool complete = false;
        explicit previous_parent_s(size_t budget) : remaining(budget) {}
    };
    using previous_parents_s = std::unordered_map<aether_node_s*, previous_parent_s>;
    // Navigation identity is independent of nth positions and selector results.
    // No adjacent lookup means no extra heap allocation or wider nth entries.
    std::optional<previous_parents_s> previous_parents_;
    enum class ancestor_state_s { unseen, complete, unavailable };
    bool ancestor_ids_enabled_ = false;
    aether_node_s* ancestor_subject_ = nullptr;
    ancestor_state_s ancestor_state_ = ancestor_state_s::unseen;
    // Delay even the container's construction; retain only one subject's owned
    // ID copies. No node, rule, transaction or later preparation owns this data.
    std::optional<std::vector<std::string>> ancestor_ids_;
    void collect_ancestor_ids(aether_node_s* subject);
    friend bool selector_match(const compiled_selector_s&, aether_node_s*,
                               selector_match_context_s&);
    friend struct selector_match_context_test_access;
};

bool selector_match(const compiled_selector_s& selector, aether_node_s* element,
                    selector_match_context_s& context);
