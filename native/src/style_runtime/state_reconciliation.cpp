#include "state_reconciliation.h"
#include "../dom/dom_types.h"
#include <algorithm>
namespace aether::style {
namespace {
template<class A,class B> bool same_owner(const std::shared_ptr<A>& a,const std::shared_ptr<B>& b)noexcept {
    return a.get()==b.get()&&!a.owner_before(b)&&!b.owner_before(a);
}
bool live_entry(const PayloadDirectoryEntry* entry,const std::shared_ptr<const node_identity_s>& token)noexcept {
    // Authentic immutable membership supplies storage AND current-owner relays
    // before reading mutable liveness. Journal tokens alone own no DOM storage.
    return entry&&entry->has_committed_state&&token&&
        same_owner(entry->payload->node_lifetime(),token)&&token->owner==entry->payload->node()&&
        entry->payload->node_identity()==reinterpret_cast<uintptr_t>(token.get())&&token->alive&&token->attached;
}
}
aether_state_fallback_t reconcile_state_inputs(const StyleRevisionRef& base,
    const aether::dom::MutationJournal& journal,uint64_t current_epoch,
    const aether_style_state_change_t* declarations,size_t declaration_count,
    std::vector<ReconciledState>& out) {
    if(!base||!base->authentic_owner(base))return AETHER_STATE_FALLBACK_BASE_REVISION;
    if(journal.coverage_lost(current_epoch)||base->input_epoch()>current_epoch||
        base->input_epoch()<journal.floor_epoch())
        return AETHER_STATE_FALLBACK_JOURNAL_INCOMPLETE;
    std::vector<ReconciledState> result;
    uint64_t expected=base->input_epoch();
    for(const auto& record:journal) {
        if(record.mutation_epoch<=base->input_epoch())continue;
        if(expected==UINT64_MAX||record.mutation_epoch!=expected+1)
            return AETHER_STATE_FALLBACK_JOURNAL_INCOMPLETE;
        expected=record.mutation_epoch;
        if(record.kind!=aether::dom::MutationKind::state)return AETHER_STATE_FALLBACK_NON_STATE_MUTATION;
        const auto& token=record.subject;
        // owner is immutable identity metadata, used solely as an opaque key.
        // Do not inspect mutable alive/attached until the authentic base payload
        // has proved membership and pinned this node's current ownership domain.
        if(!token)return AETHER_STATE_FALLBACK_IDENTITY;
        const auto* entry=base->directory().find(reinterpret_cast<uintptr_t>(token->owner));
        if(!live_entry(entry,token))return AETHER_STATE_FALLBACK_IDENTITY;
        auto found=std::find_if(result.begin(),result.end(),[&](const auto& item){return item.token==token;});
        if(found==result.end()) {
            if(entry->committed_state!=record.old_state)return AETHER_STATE_FALLBACK_LEDGER;
            result.push_back({entry->payload->node(),token,record.old_state,record.new_state});
        } else {
            if(found->new_state!=record.old_state)return AETHER_STATE_FALLBACK_LEDGER;
            found->new_state=record.new_state;
        }
    }
    if(expected!=current_epoch)return AETHER_STATE_FALLBACK_JOURNAL_INCOMPLETE;
    for(const auto& item:result)
        if(!same_owner(item.node->identity,item.token)||item.node->state!=item.new_state)
            return AETHER_STATE_FALLBACK_LEDGER;
    for(size_t i=0;i<declaration_count;++i) {
        const auto& declared=declarations[i];
        const auto* entry=base->directory().find(reinterpret_cast<uintptr_t>(declared.node));
        const auto token=entry?entry->payload->node_lifetime():std::shared_ptr<node_identity_s>{};
        if(!live_entry(entry,token)||declared.node_identity!=entry->payload->node_identity())
            return AETHER_STATE_FALLBACK_IDENTITY;
        // Declarations are consolidated against the committed revision, not the
        // most recent event. Duplicates are harmless only when they agree.
        if(declared.old_state!=entry->committed_state||declared.new_state!=declared.node->state)
            return AETHER_STATE_FALLBACK_CALLER_BATCH;
        if(declared.old_state!=declared.new_state&&std::none_of(result.begin(),result.end(),
            [&](const auto& item){return item.node==declared.node;}))return AETHER_STATE_FALLBACK_CALLER_BATCH;
    }
    out=std::move(result);
    return AETHER_STATE_FALLBACK_NONE;
}
}
