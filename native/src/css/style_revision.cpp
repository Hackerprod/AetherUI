#include "style_revision.h"
#include "../dom/dom_types.h"
#include <climits>
#include <unordered_set>

using namespace aether::style;
style_revision_s::style_revision_s(const StyleRevisionInput& in) :
    contextual_(in.contextual), sheet_epoch_(in.sheet_epoch), generation_(in.generation), document_identity_(in.document_identity),
    input_epoch_(in.input_epoch), structure_epoch_(in.structure_epoch), style_tree_epoch_(in.style_tree_epoch),
    environment_(in.environment), document_(in.document), document_lifetime_(in.document_lifetime), backing_(in.backing),
    entries_(ImmutableArray<FrozenRevisionEntry>::generate(in.entries.size(), [&](size_t i) {
        return FrozenRevisionEntry(in.entries[i]);
    })),
    changes_(ImmutableArray<FrozenRevisionEntry>::generate(in.entries.size() + in.retired.size(), [&](size_t i) {
        if (i < in.entries.size()) return FrozenRevisionEntry(in.entries[i]);
        const auto& retired = in.retired[i-in.entries.size()];
        return FrozenRevisionEntry({retired.previous_record->node(), {}, retired.impact});
    })),
    retired_nodes_(ImmutableArray<FrozenRetiredNode>::generate(in.retired.size(), [&](size_t i) {
        return FrozenRetiredNode(in.retired[i].previous_record);
    })) {}

namespace aether::style {
namespace {
template<class T> bool same_owner(const std::shared_ptr<T>& a, const std::shared_ptr<T>& b) noexcept {
    return a == b && !a.owner_before(b) && !b.owner_before(a);
}
}
aether_status_t freeze_style_revision(const StyleRevisionInput& in, StyleRevisionRef& out) noexcept {
    if (!in.document || !in.backing || !in.document_lifetime || !in.generation ||
        !in.document_lifetime->alive || !in.document_lifetime->owner ||
        in.document_identity != reinterpret_cast<uintptr_t>(in.document_lifetime.get()) ||
        in.document->values().identity != in.document_identity || in.document->values().generation != in.generation ||
        in.entries.size() != in.backing->size() || in.entries.size() > INT32_MAX ||
        in.retired.size() > static_cast<size_t>(INT32_MAX) - in.entries.size())
        return AETHER_ERROR_INVALID_STATE;
    // Liveness is checked before touching the DOM observer's raw owner.
    if (static_cast<const aether_node_s*>(in.document_lifetime->owner)->identity != in.document_lifetime)
        return AETHER_ERROR_INVALID_STATE;
    try {
        bool has_typography=false;
        for (const auto& entry:in.entries) if (entry.record && entry.record->typography()) has_typography=true;
        std::unordered_set<const aether_node_s*> seen;
        seen.reserve(in.entries.size() + in.retired.size());
        for (const auto& entry : in.entries) {
            const auto& r = entry.record;
            if (!entry.node || !r || r->node() != entry.node || !seen.insert(entry.node).second ||
                !same_owner(in.backing->find_owner(entry.node), r) || r->origin() != RecordOrigin::runtime ||
                !r->node_lifetime() || !r->node_lifetime()->alive || r->node_lifetime()->owner != entry.node ||
                !same_owner(r->document_lifetime(), in.document_lifetime) || r->document_identity() != in.document_identity ||
                !r->measures() || !same_owner(r->measures()->document_owner(), in.document))
                return AETHER_ERROR_INVALID_STATE;
            // Evidence cannot migrate independently from its exact revision.
            if (has_typography && !r->typography()) return AETHER_ERROR_INVALID_STATE;
            if (r->typography()) {
                const auto& t=*r->typography();
                if (t.generation != in.generation || t.document_identity != in.document_identity ||
                    t.input_epoch != in.input_epoch || t.structure_epoch != in.structure_epoch ||
                    t.style_tree_epoch != in.style_tree_epoch || t.sheet_epoch != in.sheet_epoch || t.registry_version != 1)
                    return AETHER_ERROR_INVALID_STATE;
            }
            if (entry.node->identity != r->node_lifetime()) return AETHER_ERROR_INVALID_STATE;
        }
        for (const auto& retired : in.retired) {
            const auto& r = retired.previous_record;
            // A previous factory-produced record supplies provenance. Never
            // dereference retired raw DOM: normal nodes may already be dead.
            if (!r || !r->node() || r->origin() != RecordOrigin::runtime || !r->node_lifetime() ||
                r->node_lifetime()->owner != r->node() ||
                r->node_identity() != reinterpret_cast<uintptr_t>(r->node_lifetime().get()) ||
                (r->generated_leaf() && r->generated_leaf().get() != r->node()) ||
                in.backing->find(r->node()) || !seen.insert(r->node()).second)
                return AETHER_ERROR_INVALID_STATE;
        }
        StyleRevisionRef result(new const style_revision_s(in));
        out = std::move(result);
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
}
