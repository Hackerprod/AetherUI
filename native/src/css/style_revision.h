#pragma once
#include <aether/aether_contextual_style.h>
#include "immutable_style_backing.h"
#include "frozen_measure_read.h"
#include <vector>

class style_revision_s;
namespace aether::style {
using StyleRevisionRef = std::shared_ptr<const style_revision_s>;
struct RevisionEntryInput {
    aether_node_s* node = nullptr;
    FrozenNodeRef record;
    uint32_t impact = 0;
};
struct RetiredEntryInput { FrozenNodeRef previous_record; uint32_t impact = 0; };
class FrozenRevisionEntry {
    aether_node_s* const node_;
    const FrozenNodeRef record_;
    const uint32_t impact_;
public:
    explicit FrozenRevisionEntry(const RevisionEntryInput& in) : node_(in.node), record_(in.record), impact_(in.impact) {}
    aether_node_s* node() const noexcept { return node_; }
    const FrozenNodeRef& record() const noexcept { return record_; }
    uint32_t impact() const noexcept { return impact_; }
};
class FrozenRetiredNode {
    aether_node_s* const node_;
    const std::shared_ptr<node_identity_s> node_lifetime_;
    const std::shared_ptr<aether_node_s> generated_leaf_;
public:
    explicit FrozenRetiredNode(const FrozenNodeRef& record) : node_(record->node()),
        node_lifetime_(record->node_lifetime()), generated_leaf_(record->generated_leaf()) {}
    aether_node_s* node() const noexcept { return node_; }
    const std::shared_ptr<node_identity_s>& node_lifetime() const noexcept { return node_lifetime_; }
    const std::shared_ptr<aether_node_s>& generated_leaf() const noexcept { return generated_leaf_; }
};
struct StyleRevisionInput {
    bool contextual = true;
    uint64_t generation = 0, document_identity = 0, input_epoch = 0, structure_epoch = 0, style_tree_epoch = 0;
    aether_style_environment_t environment{};
    FrozenDocumentRef document;
    std::shared_ptr<node_identity_s> document_lifetime;
    BackingRef backing;
    std::vector<RevisionEntryInput> entries;
    std::vector<RetiredEntryInput> retired;
};
aether_status_t freeze_style_revision(const StyleRevisionInput&, StyleRevisionRef&) noexcept;
}

class style_revision_s final {
    const bool contextual_;
    const uint64_t generation_, document_identity_, input_epoch_, structure_epoch_, style_tree_epoch_;
    const aether_style_environment_t environment_;
    const aether::style::FrozenDocumentRef document_;
    const std::shared_ptr<node_identity_s> document_lifetime_;
    const aether::style::BackingRef backing_;
    const aether::style::ImmutableArray<aether::style::FrozenRevisionEntry> entries_, changes_;
    const aether::style::ImmutableArray<aether::style::FrozenRetiredNode> retired_nodes_;
    explicit style_revision_s(const aether::style::StyleRevisionInput&);
    friend aether_status_t aether::style::freeze_style_revision(const aether::style::StyleRevisionInput&, aether::style::StyleRevisionRef&) noexcept;
public:
    bool contextual() const noexcept { return contextual_; }
    uint64_t generation() const noexcept { return generation_; }
    uint64_t document_identity() const noexcept { return document_identity_; }
    uint64_t input_epoch() const noexcept { return input_epoch_; }
    uint64_t structure_epoch() const noexcept { return structure_epoch_; }
    uint64_t style_tree_epoch() const noexcept { return style_tree_epoch_; }
    const aether_style_environment_t& environment() const noexcept { return environment_; }
    const aether::style::FrozenDocumentRef& document() const noexcept { return document_; }
    const std::shared_ptr<node_identity_s>& document_lifetime() const noexcept { return document_lifetime_; }
    const aether::style::BackingRef& backing() const noexcept { return backing_; }
    auto entries() const noexcept { return entries_.view(); }
    auto changes() const noexcept { return changes_.view(); }
    auto retired_nodes() const noexcept { return retired_nodes_.view(); }
};

namespace aether::style {
// Allocation-free operation-scoped view. Only an authentic revision can bind a
// payload; the pin protects its document, payload, and generated node storage.
// Legacy exported records remain bound owners and keep their existing ABI.
class BoundNodeView {
    StyleRevisionRef revision_;
    FrozenNodePayloadRef payload_;
    BoundNodeView(StyleRevisionRef revision, FrozenNodePayloadRef payload) noexcept
        : revision_(std::move(revision)), payload_(std::move(payload)) {}
public:
    BoundNodeView() noexcept = default;
    static BoundNodeView find(StyleRevisionRef revision, const aether_node_s* node) noexcept {
        if (!revision) return {};
        const auto* record = revision->backing()->find(node);
        if (!record) return {};
        auto payload = record->payload_owner();
        return {std::move(revision), std::move(payload)};
    }
    explicit operator bool() const noexcept { return bool(payload_); }
    const StyleRevisionRef& revision_owner() const noexcept { return revision_; }
    const FrozenNodePayloadRef& payload_owner() const noexcept { return payload_; }
    ComputedStyleReadView style_view() const noexcept { return payload_->style_view(); }
    const FrozenDocumentMeasureContext& document() const noexcept { return *revision_->document(); }
    MeasuresReadView measures_view() const noexcept { return MeasuresReadView(*payload_->measures(), document()); }
    uint64_t generation() const noexcept { return revision_->generation(); }
};
}
