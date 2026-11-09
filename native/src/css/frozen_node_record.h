#pragma once
#include "frozen_style.h"
#include "../dom/node_identity.h"
struct aether_node_s;
namespace aether::style {
class FrozenCustomPropertyScope;
class FrozenColorMetadata;
using FrozenScopeRef = std::shared_ptr<const FrozenCustomPropertyScope>;
using FrozenColorRef = std::shared_ptr<const FrozenColorMetadata>;
using FrozenMeasuresRef = std::shared_ptr<const FrozenMeasures>;
using FrozenDocumentRef = std::shared_ptr<const FrozenDocumentMeasureContext>;
enum class RecordOrigin { legacy, runtime };
// Inputs are synchronous private transport. Raw nodes must be live for validation.
// generated_leaf MUST copy an authentic prepared/DOM owner, never shared_ptr(raw).
// The factory verifies identity/token/pointer, not arbitrary caller ownership claims.
struct NodeRecordInput {
    RecordOrigin origin = RecordOrigin::legacy;
    aether_node_s* node = nullptr;
    FrozenStyleRef style = {};
    FrozenMeasuresRef measures = {};
    FrozenScopeRef custom_properties = {};
    FrozenColorRef color_references = {};
    uint32_t declared_impact = 0;
    uint32_t declared_inherited_impact = 0;
    uint32_t inherited_change_impact = 0;
    uint64_t document_identity = 0;
    uint64_t node_identity = 0;
    std::shared_ptr<node_identity_s> node_lifetime = {};
    std::shared_ptr<node_identity_s> document_lifetime = {};
    std::shared_ptr<aether_node_s> generated_leaf = {};
};
// Unknown/imported dependencies conservatively consume every parent input.
namespace ParentDependencies {
inline constexpr uint32_t background_color = 1u << 0;
inline constexpr uint32_t border_colors = 1u << 1;
inline constexpr uint32_t other = 1u << 2;
inline constexpr uint32_t all = ~uint32_t{0};
}
struct NodePayloadInput {
    uint32_t parent_dependencies = ParentDependencies::all;
    RecordOrigin origin = RecordOrigin::legacy;
    aether_node_s* node = nullptr;
    FrozenStyleRef style = {};
    FrozenMeasurePayloadRef measures = {};
    FrozenScopeRef custom_properties = {};
    FrozenColorRef color_references = {};
    FrozenScopeRef backdrop_custom_properties = {};
    uint32_t declared_impact = 0;
    uint32_t declared_inherited_impact = 0;
    uint32_t inherited_change_impact = 0;
    uint64_t document_identity = 0;
    uint64_t node_identity = 0;
    std::shared_ptr<node_identity_s> node_lifetime = {};
    std::shared_ptr<node_identity_s> document_lifetime = {};
    std::shared_ptr<aether_node_s> generated_leaf = {};
};
class FrozenNodePayload;
class BoundNodeView;
using FrozenNodePayloadRef = std::shared_ptr<const FrozenNodePayload>;
aether_status_t freeze_node_payload(const NodePayloadInput&, FrozenNodePayloadRef&) noexcept;
class FrozenNodeRecord;
using FrozenNodeRef = std::shared_ptr<const FrozenNodeRecord>;
aether_status_t freeze_node_record(const NodeRecordInput&, FrozenNodeRef&) noexcept;
// Factory-certified, revision-independent node data. It retains identities and
// generated storage, but never a document measure context or revision.
class FrozenNodePayload {
    const uint32_t parent_dependencies_;
    const RecordOrigin origin_;
    aether_node_s* const node_;
    const FrozenStyleRef style_;
    const FrozenMeasurePayloadRef measures_;
    const FrozenScopeRef custom_properties_;
    const FrozenColorRef color_references_;
    const FrozenScopeRef backdrop_custom_properties_;
    const uint32_t declared_impact_;
    const uint32_t declared_inherited_impact_;
    const uint32_t inherited_change_impact_;
    const uint64_t document_identity_;
    const uint64_t node_identity_;
    const std::shared_ptr<node_identity_s> node_lifetime_;
    const std::shared_ptr<node_identity_s> document_lifetime_;
    const std::shared_ptr<aether_node_s> generated_leaf_;
    const std::shared_ptr<EditableNodeStorage> editable_lease_;
    const std::shared_ptr<EditableNodeStorage> document_lease_;
    std::vector<std::shared_ptr<EditableNodeStorage>> owner_leases_;
    explicit FrozenNodePayload(const NodePayloadInput&);
    friend aether_status_t freeze_node_payload(const NodePayloadInput&, FrozenNodePayloadRef&) noexcept;
    friend class FrozenNodeRecord;
    friend aether_status_t freeze_node_record(const NodeRecordInput&, FrozenNodeRef&) noexcept;
public:
    uint32_t parent_dependencies() const noexcept { return parent_dependencies_; }
    const RecordOrigin& origin() const noexcept { return origin_; }
    aether_node_s* node() const noexcept { return node_; }
    const FrozenStyleRef& style() const noexcept { return style_; }
    const FrozenMeasurePayloadRef& measures() const noexcept { return measures_; }
    const FrozenScopeRef& custom_properties() const noexcept { return custom_properties_; }
    const FrozenColorRef& color_references() const noexcept { return color_references_; }
    const FrozenScopeRef& backdrop_custom_properties() const noexcept { return backdrop_custom_properties_; }
    const uint32_t& declared_impact() const noexcept { return declared_impact_; }
    const uint32_t& declared_inherited_impact() const noexcept { return declared_inherited_impact_; }
    const uint32_t& inherited_change_impact() const noexcept { return inherited_change_impact_; }
    const uint64_t& document_identity() const noexcept { return document_identity_; }
    const uint64_t& node_identity() const noexcept { return node_identity_; }
    const std::shared_ptr<node_identity_s>& node_lifetime() const noexcept { return node_lifetime_; }
    const std::shared_ptr<node_identity_s>& document_lifetime() const noexcept { return document_lifetime_; }
    const std::shared_ptr<aether_node_s>& generated_leaf() const noexcept { return generated_leaf_; }
    const std::shared_ptr<EditableNodeStorage>& editable_lease() const noexcept { return editable_lease_; }
    const std::shared_ptr<EditableNodeStorage>& document_lease() const noexcept { return document_lease_; }
    ComputedStyleReadView style_view() const noexcept { return style_->view(); }
};

using FrozenNodePayloadRef = std::shared_ptr<const FrozenNodePayload>;
// Legacy/export bound record. Its measures retain exactly one immutable
// document owner; consumers keep the existing same_owner validation contract.
class FrozenNodeRecord {
    const FrozenNodePayloadRef payload_;
    const FrozenMeasuresRef measures_;
    FrozenNodeRecord(FrozenNodePayloadRef payload, FrozenMeasuresRef measures)
        : payload_(std::move(payload)), measures_(std::move(measures)) {}
    friend class BoundNodeView;
    friend aether_status_t freeze_node_record(const NodeRecordInput&, FrozenNodeRef&) noexcept;
public:
    const FrozenNodePayloadRef& payload_owner() const noexcept { return payload_; }
    const FrozenMeasuresRef& measures() const noexcept { return measures_; }
    const RecordOrigin& origin() const noexcept { return payload_->origin(); }
    aether_node_s* node() const noexcept { return payload_->node(); }
    const FrozenStyleRef& style() const noexcept { return payload_->style(); }
    const FrozenScopeRef& custom_properties() const noexcept { return payload_->custom_properties(); }
    const FrozenColorRef& color_references() const noexcept { return payload_->color_references(); }
    const uint32_t& declared_impact() const noexcept { return payload_->declared_impact(); }
    const uint32_t& declared_inherited_impact() const noexcept { return payload_->declared_inherited_impact(); }
    const uint32_t& inherited_change_impact() const noexcept { return payload_->inherited_change_impact(); }
    const uint64_t& document_identity() const noexcept { return payload_->document_identity(); }
    const uint64_t& node_identity() const noexcept { return payload_->node_identity(); }
    const std::shared_ptr<node_identity_s>& node_lifetime() const noexcept { return payload_->node_lifetime(); }
    const std::shared_ptr<node_identity_s>& document_lifetime() const noexcept { return payload_->document_lifetime(); }
    const std::shared_ptr<aether_node_s>& generated_leaf() const noexcept { return payload_->generated_leaf(); }
    ComputedStyleReadView style_view() const noexcept { return payload_->style_view(); }
};
}
