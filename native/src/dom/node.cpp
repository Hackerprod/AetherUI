#include "dom_types.h"
#include "../abi/aether_abi.h"
#include <aether/aether_dom_sync.h>

namespace {
// Layout wrappers contain borrowed aliases, and generated text may carry no
// marker of its own. Neither is part of the editable, reciprocal DOM forest.
// Handles must be live and the initial DOM coherent; this is not a lifetime or
// pre-existing-corruption check.
bool is_synthetic_endpoint(const aether_node_s* node) {
    for (auto* ancestor = node; ancestor; ancestor = ancestor->parent) {
        if (ancestor->is_generated || ancestor->anonymous_flex_owner) return true;
    }
    return false;
}
}

AETHER_API aether_node_type_t aether_node_get_type(aether_node_t node) {
    return node ? node->type : AETHER_NODE_DOCUMENT;
}

AETHER_API aether_node_t aether_node_get_parent(aether_node_t node) {
    return node ? node->parent : nullptr;
}

AETHER_API int32_t aether_node_get_child_count(aether_node_t node) {
    if (!node) return 0;
    return static_cast<int32_t>(std::count_if(node->children.begin(), node->children.end(),
        [](const aether_node_s* child) { return !child->is_generated; }));
}

AETHER_API aether_node_t aether_node_get_child(aether_node_t node, int32_t index) {
    if (!node || index < 0) return nullptr;
    for (auto* child : node->children) {
        if (child->is_generated) continue;
        if (index-- == 0) return child;
    }
    return nullptr;
}

AETHER_API char* aether_node_get_text_content(aether_node_t node) {
    if (!node) return nullptr;
    return aether::strdup(node->text.c_str());
}

AETHER_API int64_t aether_node_get_style_node_id(aether_node_t node) {
    return node ? node->style_node_id : 0;
}

namespace {
bool has_binding(aether_node_s* node) noexcept {auto owner=node?node->document_owner.lock():nullptr;return owner&&!owner->binding.expired();}
aether_status_t bound_structure(aether_node_s* parent,aether_node_s* child,int32_t kind) noexcept {
    aether_dom_token_v1_t token{},output{};auto status=aether_dom_read_token_v1(parent,&token);if(status!=AETHER_OK)return status;
    aether_dom_edit_spec_v1_t spec{};spec.kind=kind;spec.target=parent;spec.child=child;spec.index=-1;
    aether_dom_edit_t edit=nullptr;status=aether_dom_prepare_edit_v1(&token,&spec,&edit);
    if(status==AETHER_OK)status=aether_dom_edit_commit_v1(edit,&output);aether_dom_edit_destroy_v1(edit);return status;
}
void collect_subtree(aether_node_s* node, std::vector<aether_node_s*>& nodes) {
    nodes.push_back(node); for (auto* child : node->children) collect_subtree(child,nodes);
}
bool can_disconnect(const std::vector<aether_node_s*>& nodes) noexcept {
    for(auto* node:nodes) if(node->identity->disconnect_serial==UINT64_MAX) return false;
    return true;
}
void disconnect(const std::vector<aether_node_s*>& nodes) noexcept {
    for(auto* node:nodes) { ++node->identity->disconnect_serial; node->identity->attached=false; }
}
}

AETHER_API aether_status_t aether_node_append_child(aether_node_t parent, aether_node_t child) {
    if (!parent || !child) return AETHER_ERROR_NULL_ARG;
    if(has_binding(parent)||has_binding(child))return bound_structure(parent,child,AETHER_DOM_INSERT_CHILD);
    if (is_synthetic_endpoint(parent) || is_synthetic_endpoint(child) || child->type==AETHER_NODE_DOCUMENT)
        return AETHER_ERROR_INVALID_STATE;
    for(auto* ancestor=parent;ancestor;ancestor=ancestor->parent) if(ancestor==child) return AETHER_ERROR_INVALID_STATE;
    if(aether_dom_access(parent)!=AETHER_OK || aether_dom_access(child)!=AETHER_OK) return AETHER_ERROR_INVALID_STATE;
    auto target_owner=parent->document_owner.lock(), source_owner=child->document_owner.lock();
    const bool legacy_attach=target_owner && !source_owner && !aether_editable_lease(child);
    const bool adopt=target_owner!=source_owner && !legacy_attach;
    const bool disconnecting=adopt||(child->identity->attached&&!parent->identity->attached);
    // Factory owned nodes may not escape into an unowned legacy tree.
    if(adopt && (!target_owner || !source_owner)) return AETHER_ERROR_INVALID_STATE;
    auto* old_parent=child->parent;
    const bool two_touches=old_parent || adopt;
    const bool shared_root=old_parent && aether_dom_root(parent)==aether_dom_root(old_parent);
    if(!aether_dom_can_touch(parent,shared_root?2:1,true) ||
       (two_touches && !aether_dom_can_touch(old_parent?old_parent:child,shared_root?2:1,true)) ||
       (target_owner && source_owner==target_owner && two_touches && target_owner->observation_serial>UINT64_MAX-2))
        return AETHER_ERROR_INVALID_STATE;
    try {
        auto target_children=parent->children;
        std::vector<aether_node_s*> source_children;
        if(old_parent==parent) target_children.erase(std::remove(target_children.begin(),target_children.end(),child),target_children.end());
        else if(old_parent) { source_children=old_parent->children; source_children.erase(std::remove(source_children.begin(),source_children.end(),child),source_children.end()); }
        target_children.push_back(child);
        std::vector<aether_node_s*> moving;
        std::vector<std::shared_ptr<DomNodeOwnerPin>> moving_relays;
        decltype(aether_doc_s::editable_nodes) source_registry, target_registry;
        std::shared_ptr<EditableNodeStorage> source_pin,target_pin;
        aether_doc_s* source_doc=nullptr; aether_doc_s* target_doc=nullptr;
        int64_t next_id=0;
        if(legacy_attach||(!adopt&&disconnecting)) collect_subtree(child,moving);
        if(!adopt&&disconnecting&&!can_disconnect(moving))return AETHER_ERROR_INVALID_STATE;
        if(adopt) {
            collect_subtree(child,moving); if(!can_disconnect(moving)) return AETHER_ERROR_INVALID_STATE;
            source_pin=source_owner->document.lock(); target_pin=target_owner->document.lock();
            if(!source_pin||!target_pin) return AETHER_ERROR_INVALID_STATE;
            source_doc=static_cast<aether_doc_s*>(source_pin->node.get()); target_doc=static_cast<aether_doc_s*>(target_pin->node.get());
            source_registry=source_doc->editable_nodes; target_registry=target_doc->editable_nodes; next_id=target_doc->next_style_node_id;
            for(auto* node:moving) if(auto lease=aether_editable_lease(node)) {
                if(next_id==INT64_MAX) return AETHER_ERROR_INVALID_STATE;
                ++next_id; source_registry.erase(node); target_registry.emplace(node,std::move(lease));
            }
        }
        if(adopt) {moving_relays.reserve(moving.size());for(auto* node:moving)moving_relays.push_back(aether_live_owner_pin(node));}
        // No callback/allocation from this point through the completed links.
        if(old_parent) aether_node_touch_typed(old_parent,aether::dom::MutationKind::structure,true,0,0,child);
        else if(adopt) aether_node_touch_typed(child,aether::dom::MutationKind::structure,true,0,0,child);
        aether_node_touch_typed(parent,aether::dom::MutationKind::structure,true,0,0,child);
        if(old_parent && old_parent!=parent) old_parent->children.swap(source_children);
        parent->children.swap(target_children); child->parent=parent;
        if(disconnecting)disconnect(moving);
        if(adopt) {
            for(const auto& relay:moving_relays)if(relay)relay->current_root=target_pin;
            int64_t id=target_doc->next_style_node_id;
            for(auto* node:moving) {
                node->document_owner=target_owner; node->identity->document_owner=target_owner;
                if(aether_editable_lease(node)) node->style_node_id=id++;
            }
            source_doc->editable_nodes.swap(source_registry); target_doc->editable_nodes.swap(target_registry); target_doc->next_style_node_id=next_id;
        }
        if(legacy_attach) for(auto* node:moving) {node->document_owner=target_owner;node->identity->document_owner=target_owner;}
        aether_node_mark_attached(child,parent->identity->attached);
        return AETHER_OK;
    } catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch(...) { return AETHER_ERROR_INVALID_STATE; }
}

AETHER_API aether_status_t aether_node_remove_child(aether_node_t parent, aether_node_t child) {
    if(!parent||!child) return AETHER_ERROR_NULL_ARG;
    if(has_binding(parent)||has_binding(child))return bound_structure(parent,child,AETHER_DOM_REMOVE_CHILD);
    if(is_synthetic_endpoint(parent)||is_synthetic_endpoint(child)||aether_dom_access(child)!=AETHER_OK || !aether_dom_can_touch(parent,1,true)) return AETHER_ERROR_INVALID_STATE;
    auto it=std::find(parent->children.begin(),parent->children.end(),child);
    if(it==parent->children.end()||child->parent!=parent) return AETHER_ERROR_INVALID_STATE;
    try {
        std::vector<aether_node_s*> leaving; collect_subtree(child,leaving);
        if(!can_disconnect(leaving)) return AETHER_ERROR_INVALID_STATE;
        aether_node_touch_typed(parent,aether::dom::MutationKind::structure,true,0,0,child);
        parent->children.erase(it); disconnect(leaving); child->parent=nullptr;
        return AETHER_OK;
    } catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch(...) { return AETHER_ERROR_INVALID_STATE; }
}
