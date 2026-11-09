#pragma once
#include <aether/aether_state_style.h>
#include "../css/style_revision.h"
#include "../dom/mutation_journal.h"
#include <vector>
namespace aether::style {
struct ReconciledState {
    aether_node_s* node=nullptr;
    std::shared_ptr<const node_identity_s> token;
    int32_t old_state=0, new_state=0;
};
// An observation, not an authorization certificate. All state records are
// included regardless of the caller's declaration list. Never mutates the DOM.
aether_state_fallback_t reconcile_state_inputs(const StyleRevisionRef& base,
    const aether::dom::MutationJournal& journal, uint64_t current_epoch,
    const aether_style_state_change_t* declarations, size_t declaration_count,
    std::vector<ReconciledState>& out);
}
