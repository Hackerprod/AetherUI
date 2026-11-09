#include "frozen_node_record.h"
#include "../dom/dom_types.h"
namespace aether::style {
FrozenNodePayload::FrozenNodePayload(const NodeRecordInput& in) :
    origin_(in.origin),
    node_(in.node),
    style_(in.style),
    measures_(in.measures ? in.measures->payload_owner() : FrozenMeasurePayloadRef{}),
    custom_properties_(in.custom_properties),
    color_references_(in.color_references),
    declared_impact_(in.declared_impact),
    declared_inherited_impact_(in.declared_inherited_impact),
    inherited_change_impact_(in.inherited_change_impact),
    document_identity_(in.document_identity),
    node_identity_(in.node_identity),
    node_lifetime_(in.node_lifetime),
    document_lifetime_(in.document_lifetime),
    generated_leaf_(in.generated_leaf) {}
FrozenNodeRecord::FrozenNodeRecord(const NodeRecordInput& in) :
    payload_(new const FrozenNodePayload(in)), measures_(in.measures) {}
aether_status_t freeze_node_record(const NodeRecordInput& in, FrozenNodeRef& out) noexcept {
    if (!in.node || !in.style || (in.origin != RecordOrigin::legacy && in.origin != RecordOrigin::runtime)) return AETHER_ERROR_INVALID_STATE;
    if (in.node_lifetime) {
        if (!in.node_lifetime->alive || in.node_lifetime->owner != in.node ||
            in.node_lifetime != in.node->identity ||
            in.node_identity != reinterpret_cast<uintptr_t>(in.node_lifetime.get())) return AETHER_ERROR_INVALID_STATE;
    } else if (in.node_identity) return AETHER_ERROR_INVALID_STATE;
    if (in.document_lifetime) {
        if (!in.document_lifetime->alive || !in.document_lifetime->owner ||
            static_cast<const aether_node_s*>(in.document_lifetime->owner)->identity != in.document_lifetime ||
            in.document_identity != reinterpret_cast<uintptr_t>(in.document_lifetime.get())) return AETHER_ERROR_INVALID_STATE;
    } else if (in.document_identity) return AETHER_ERROR_INVALID_STATE;
    if (in.generated_leaf && (in.generated_leaf.get() != in.node || !in.node->is_generated ||
        !in.node_lifetime || in.generated_leaf->identity != in.node_lifetime)) return AETHER_ERROR_INVALID_STATE;
    if (in.node->is_generated && !in.generated_leaf) return AETHER_ERROR_INVALID_STATE;
    if (in.origin == RecordOrigin::runtime && (!in.measures || !in.custom_properties || !in.color_references ||
        !in.node_lifetime || !in.document_lifetime || in.measures->document().values().identity != in.document_identity)) return AETHER_ERROR_INVALID_STATE;
    try { FrozenNodeRef result(new const FrozenNodeRecord(in)); out = std::move(result); return AETHER_OK; }
    catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
}
