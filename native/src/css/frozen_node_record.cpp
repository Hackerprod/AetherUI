#include "frozen_node_record.h"
#include "../dom/dom_types.h"
namespace aether::style {
namespace {
template<class T> bool same_owner(const std::shared_ptr<T>& a,const std::shared_ptr<T>& b)noexcept {
    return a==b&&!a.owner_before(b)&&!b.owner_before(a);
}
}
FrozenNodePayload::FrozenNodePayload(const NodePayloadInput& in) :
    parent_dependencies_(in.parent_dependencies),
    origin_(in.origin),
    node_(in.node),
    style_(in.style),
    measures_(in.measures),
    custom_properties_(in.custom_properties),
    color_references_(in.color_references),
    backdrop_custom_properties_(in.backdrop_custom_properties),
    declared_impact_(in.declared_impact),
    declared_inherited_impact_(in.declared_inherited_impact),
    inherited_change_impact_(in.inherited_change_impact),
    document_identity_(in.document_identity),
    node_identity_(in.node_identity),
    node_lifetime_(in.node_lifetime),
    document_lifetime_(in.document_lifetime),
    generated_leaf_(in.generated_leaf), editable_lease_(aether_editable_lease(in.node)),
    document_lease_(aether_identity_lease(in.document_lifetime)),
    live_owner_pin_(aether_live_owner_pin(in.node)) {
        auto retain_owner = [&](aether_node_s* owner) {
            if(auto lease=aether_editable_lease(owner)) owner_leases_.push_back(std::move(lease));
            if(auto relay=aether_live_owner_pin(owner)) owner_relays_.push_back(std::move(relay));
        };
        for(auto* owner=in.node->parent;owner;owner=owner->parent) retain_owner(owner);
        if(in.node->style_origin && in.node->style_origin!=in.node->parent) retain_owner(in.node->style_origin);
    }
aether_status_t freeze_node_payload(const NodePayloadInput& in, FrozenNodePayloadRef& out) noexcept {
    const auto document_pin=aether_identity_lease(in.document_lifetime);
    if (!in.node || !in.style || (in.origin != RecordOrigin::legacy && in.origin != RecordOrigin::runtime)) return AETHER_ERROR_INVALID_STATE;
    if (in.node_lifetime) {
        if (!in.node_lifetime->alive || in.node_lifetime->owner != in.node ||
            !same_owner(in.node_lifetime,in.node->identity) ||
            in.node_identity != reinterpret_cast<uintptr_t>(in.node_lifetime.get())) return AETHER_ERROR_INVALID_STATE;
    } else if (in.node_identity) return AETHER_ERROR_INVALID_STATE;
    if (in.document_lifetime) {
        if (!in.document_lifetime->alive || !in.document_lifetime->owner ||
            !same_owner(static_cast<const aether_node_s*>(in.document_lifetime->owner)->identity,in.document_lifetime) ||
            in.document_identity != reinterpret_cast<uintptr_t>(in.document_lifetime.get())) return AETHER_ERROR_INVALID_STATE;
    } else if (in.document_identity) return AETHER_ERROR_INVALID_STATE;
    if (in.generated_leaf && (in.generated_leaf.get() != in.node || !in.node->is_generated ||
        !in.node_lifetime || !same_owner(in.generated_leaf->identity,in.node_lifetime))) return AETHER_ERROR_INVALID_STATE;
    if (in.node->is_generated && !in.generated_leaf) return AETHER_ERROR_INVALID_STATE;
    if (in.origin == RecordOrigin::runtime && (!in.measures || !in.custom_properties || !in.color_references ||
        !in.node_lifetime || !in.document_lifetime)) return AETHER_ERROR_INVALID_STATE;
    try { FrozenNodePayloadRef result(new const FrozenNodePayload(in)); out = std::move(result); return AETHER_OK; }
    catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
    catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
aether_status_t freeze_node_record(const NodeRecordInput& in, FrozenNodeRef& out) noexcept {
    if (in.origin == RecordOrigin::runtime && (!in.measures ||
        in.measures->document().values().identity != in.document_identity)) return AETHER_ERROR_INVALID_STATE;
    NodePayloadInput local;
    local.origin=in.origin; local.node=in.node; local.style=in.style;
    local.measures=in.measures ? in.measures->payload_owner() : FrozenMeasurePayloadRef{};
    local.custom_properties=in.custom_properties;local.color_references=in.color_references;
    local.declared_impact=in.declared_impact;local.declared_inherited_impact=in.declared_inherited_impact;
    local.inherited_change_impact=in.inherited_change_impact;
    local.document_identity=in.document_identity;local.node_identity=in.node_identity;
    local.node_lifetime=in.node_lifetime;local.document_lifetime=in.document_lifetime;local.generated_leaf=in.generated_leaf;
    FrozenNodePayloadRef payload;const auto status=freeze_node_payload(local,payload);
    if(status!=AETHER_OK)return status;
    try { FrozenNodeRef result(new const FrozenNodeRecord(std::move(payload),in.measures));out=std::move(result);return AETHER_OK; }
    catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}

}
