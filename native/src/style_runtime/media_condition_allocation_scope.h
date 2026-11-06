#pragma once
#include "style_runtime_types.h"

// Compilation-local ownership of nodes, not edges. Folding can discard roots,
// while selectors and nested branches can share children. Never recursively
// destroy an arena node or register an existing operand for a second time.
struct media_condition_allocation_scope_s {
    std::vector<std::unique_ptr<media_condition_s>> allocated;

    media_condition_s* make_node() {
        auto node = std::make_unique<media_condition_s>();
        auto* result = node.get();
        allocated.push_back(std::move(node));
        return result;
    }

    using reachable_set = std::unordered_set<const media_condition_s*>;

    static reachable_set reachable_from(const std::vector<compiled_rule_s>& rules) {
        reachable_set reached;
        std::vector<const media_condition_s*> pending;
        for (const auto& rule : rules) pending.push_back(rule.condition);
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            if (!node || node == media_condition_always() || node == media_condition_never() ||
                node == media_condition_unknown() || !reached.insert(node).second) continue;
            pending.push_back(node->left);
            pending.push_back(node->right);
            pending.push_back(node->inner);
        }
        return reached;
    }

    // Reachability and destination capacity must be prepared before commit.
    // Transfer the existing raw success contract, and delete local orphans.
    void release_reachable(const reachable_set& reached) noexcept {
        for (auto& node : allocated)
            if (reached.find(node.get()) != reached.end()) node.release();
    }
};
