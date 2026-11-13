#pragma once
#include "../dom/dom_types.h"

namespace aether::style {
// R0a evidence only. This is NOT a selector/cache eligibility certificate and
// never authorizes skipping computation, diagnostics, freezing or publication.
struct HoverProvenance {
    struct Entry {
        std::shared_ptr<node_identity_s> identity, parent;
        int32_t bits = 0;
    };
    std::shared_ptr<node_identity_s> document;
    uint64_t non_state_epoch = 0, input_epoch = 0;
    bool exhausted = true;
    std::vector<Entry> entries;

    static HoverProvenance capture(aether_node_s* root) {
        HoverProvenance result;
        if (!root) return result;
        root = aether_dom_root(root);
        result.document = root->identity;
        result.input_epoch = root->identity->mutation_epoch;
        result.non_state_epoch = root->identity->non_state_epoch;
        result.exhausted = root->identity->non_state_exhausted;
        std::vector<aether_node_s*> pending{root};
        while (!pending.empty()) {
            auto* node = pending.back(); pending.pop_back();
            if (node->is_generated || node->anonymous_flex_owner) continue;
            result.entries.push_back({node->identity,
                node->parent ? node->parent->identity : nullptr, node->state});
            for (auto it = node->children.rbegin(); it != node->children.rend(); ++it)
                pending.push_back(*it);
        }
        return result;
    }
    // Compare full retained identity + bit inventories, never a caller-supplied
    // changed list. Removal/reinsert and mixed writes fail via non_state_epoch.
    bool only_hover_delta_from(const HoverProvenance& previous) const noexcept {
        if (!document || document != previous.document || !document->alive ||
            exhausted || previous.exhausted || document->non_state_exhausted ||
            input_epoch != document->mutation_epoch ||
            non_state_epoch != previous.non_state_epoch ||
            non_state_epoch != document->non_state_epoch || entries.size() != previous.entries.size()) return false;
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& a = entries[i]; const auto& b = previous.entries[i];
            if (a.identity != b.identity || a.parent != b.parent ||
                !a.identity->alive || !a.identity->attached ||
                ((a.bits ^ b.bits) & ~AETHER_STATE_HOVER)) return false;
        }
        return true;
    }
    void swap(HoverProvenance& other) noexcept {
        document.swap(other.document); std::swap(non_state_epoch, other.non_state_epoch);
        std::swap(input_epoch, other.input_epoch);
        std::swap(exhausted, other.exhausted); entries.swap(other.entries);
    }
};
}
