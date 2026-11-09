#include "dom_types.h"
#include "dom_strings.h"
#include "../style_runtime/aether_dom_style_binding.h"
#include <aether/aether_dom_interaction.h>

struct DomInteractionRegistry {
    std::shared_ptr<EditableNodeStorage> document;
    std::shared_ptr<DomOwnerControl> owner;
    DomStyleBindingRef binding;
    uint64_t host_identity=0;
    bool active=false,prepared=true;
    uint64_t prepared_serial=0;
    std::vector<std::shared_ptr<DomInteractionClaim>> claims;
};
struct DomInteractionClaim {
    std::weak_ptr<DomInteractionRegistry> registry;
    std::shared_ptr<EditableNodeStorage> node;
    std::shared_ptr<DomNodeOwnerPin> live_owner;
    aether_dom_interaction_claim_info_v1_t info{};
    std::string route;
    bool route_present=false,active=false,lost=false,acknowledged=false;
    uint64_t loss_disconnect=0,loss_stamp=0;
    int32_t loss_reason=0;
};
struct DomInteractionUpdate {
    std::shared_ptr<DomInteractionClaim> claim;
    std::string route;
    bool route_changed=false,route_present=false,loss=false;
    uint64_t disconnect=0,stamp=0;
    int32_t reason=0;
};
struct DomInteractionStage {
    std::shared_ptr<DomInteractionRegistry> registry;
    uint64_t serial=0;
    std::vector<DomInteractionUpdate> updates;
};
struct aether_dom_interaction_owner_s { std::shared_ptr<DomInteractionRegistry> value; };
struct aether_dom_interaction_claim_s { std::shared_ptr<DomInteractionClaim> value; };
struct aether_dom_interaction_edit_s {
    std::shared_ptr<DomInteractionRegistry> registry;
    std::shared_ptr<DomInteractionClaim> claim,previous;
    std::unique_ptr<aether_dom_interaction_claim_s> output;
    std::vector<std::shared_ptr<DomInteractionClaim>> claims;
    aether_dom_edit_t state_edit=nullptr;
    aether_dom_interaction_candidate_v1_t candidate{};
    aether_dom_token_v1_t no_op_token{};
    uint64_t serial=0,record_id=0,record_serial=0;
    bool acquire=false,committed=false;
    ~aether_dom_interaction_edit_s(){aether_dom_edit_destroy_v1(state_edit);}
};
namespace {
unsigned bit_index(int32_t bit) noexcept {return bit==AETHER_STATE_HOVER?0:bit==AETHER_STATE_ACTIVE?1:2;}
bool valid_bit(int32_t bit) noexcept {return bit==AETHER_STATE_HOVER||bit==AETHER_STATE_ACTIVE||bit==AETHER_STATE_FOCUS;}
aether_doc_s* document(const std::shared_ptr<DomInteractionRegistry>& r) noexcept {return static_cast<aether_doc_s*>(r->document->node.get());}
bool thread(const std::shared_ptr<DomInteractionRegistry>& r) noexcept {return r&&r->owner->owner_thread==std::this_thread::get_id();}
// Document retirement leaves records readable. Explicit host/binding disposal
// or replacement revokes their delivery, even while retained handles survive.
bool readable(const std::shared_ptr<DomInteractionRegistry>& r) noexcept {
    return thread(r)&&r->active&&r->owner->interaction.lock()==r&&r->owner->binding.lock()==r->binding;
}
bool live(const std::shared_ptr<DomInteractionRegistry>& r) noexcept {
    return readable(r)&&r->owner->sticky_status==AETHER_OK&&aether_dom_style_binding_current(r->binding,document(r));
}
bool can_advance(const std::shared_ptr<DomInteractionRegistry>& r) noexcept {return r->owner->interaction_serial!=UINT64_MAX;}
void advance(const std::shared_ptr<DomInteractionRegistry>& r) noexcept {
    if(can_advance(r))++r->owner->interaction_serial;else r->owner->sticky_status=AETHER_ERROR_INVALID_STATE;
}
bool authority(const std::shared_ptr<DomInteractionClaim>& c) noexcept {
    return c->node->node->identity->interaction_claims[bit_index(c->info.bit)].lock()==c;
}
bool current(const std::shared_ptr<DomInteractionClaim>& c,const std::shared_ptr<DomInteractionRegistry>& r) noexcept {
    auto* node=c->node->node.get();return live(r)&&c->active&&!c->lost&&authority(c)&&node->identity->alive&&node->identity->attached&&
        node->document_owner.lock()==r->owner&&node->identity->disconnect_serial==c->info.disconnect_serial;
}
void revoke(const std::shared_ptr<DomInteractionClaim>& c) noexcept {
    if(authority(c))c->node->node->identity->interaction_claims[bit_index(c->info.bit)].reset();c->active=false;
}
void erase(const std::shared_ptr<DomInteractionRegistry>& r,const std::shared_ptr<DomInteractionClaim>& c) noexcept {
    auto& list=r->claims;list.erase(std::remove(list.begin(),list.end(),c),list.end());
}
void freeze(DomInteractionClaim& c,uint64_t disconnect,uint64_t stamp,int32_t reason) noexcept {
    if(c.lost||c.acknowledged)return;c.lost=true;c.loss_disconnect=disconnect;c.loss_stamp=stamp;c.loss_reason=reason;
}
bool exact_loss(const DomInteractionClaim& c,uint64_t id,uint64_t serial) noexcept {
    return c.lost&&!c.acknowledged ? id==c.info.generation&&serial==c.info.generation : !id&&!serial;
}
aether_dom_interaction_claim_info_v1_t info(const std::shared_ptr<DomInteractionClaim>& c,const std::shared_ptr<DomInteractionRegistry>& r) noexcept {
    auto result=c->info;result.current=current(c,r)?1:0;return result;
}
aether_status_t prepare_state(aether_dom_interaction_edit_s& e,const aether_dom_token_v1_t& expected,int32_t state) noexcept {
    aether_dom_edit_spec_v1_t spec{};spec.kind=AETHER_DOM_SET_STATE;spec.target=e.claim->node->node.get();spec.state=state;
    return aether_dom_prepare_edit_v1(&expected,&spec,&e.state_edit);
}
}
aether_status_t aether_dom_interaction_prepare_stage(const std::shared_ptr<DomOwnerControl>& owner,
    const std::vector<aether_node_s*>& disconnect,aether_node_s* target,
    const std::map<std::string,std::string,std::less<>>* attributes,DomInteractionStageRef& out,bool retiring) noexcept {
    out.reset();auto r=owner->interaction.lock();if(!r)return AETHER_OK;
    if(!thread(r))return AETHER_ERROR_INVALID_STATE;
    try {
        auto stage=std::make_shared<DomInteractionStage>();stage->registry=r;stage->serial=owner->interaction_serial;
        if(readable(r)&&(!disconnect.empty()||attributes))for(const auto& claim:r->claims) {
            if(!claim->active||claim->lost||claim->acknowledged)continue;
            auto* node=claim->node->node.get();DomInteractionUpdate update;update.claim=claim;
            if(attributes&&node==target&&claim->info.bit==AETHER_STATE_FOCUS) {
                const auto route=attributes->find("onblur");update.route_present=route!=attributes->end();
                if(update.route_present){if(!aether_dom_valid_string(route->second))return AETHER_ERROR_INVALID_STATE;update.route=route->second;}
                update.route_changed=update.route_present!=claim->route_present||update.route!=claim->route;
            }
            if(std::find(disconnect.begin(),disconnect.end(),node)!=disconnect.end()) {
                if(node->identity->disconnect_serial==UINT64_MAX)return AETHER_ERROR_INVALID_STATE;
                update.loss=true;update.disconnect=node->identity->disconnect_serial+1;
                update.stamp=node->identity->state_bit_write_serial[bit_index(claim->info.bit)];
                update.reason=retiring?AETHER_DOM_LOSS_RETIRED:AETHER_DOM_LOSS_DISCONNECT;
            }
            if(update.route_changed||update.loss)stage->updates.push_back(std::move(update));
        }
        if(!stage->updates.empty()&&!can_advance(r))return AETHER_ERROR_INVALID_STATE;
        out=std::move(stage);return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
bool aether_dom_interaction_validate_stage(const DomInteractionStageRef& s) noexcept {
    return !s||(thread(s->registry)&&s->registry->owner->interaction_serial==s->serial&&s->registry->owner->interaction.lock()==s->registry&&s->registry->owner->binding.lock()==s->registry->binding);
}
void aether_dom_interaction_commit_stage(const DomInteractionStageRef& s) noexcept {
    if(!s)return;for(auto& u:s->updates) {
        if(u.route_changed){u.claim->route.swap(u.route);u.claim->route_present=u.route_present;}
        if(u.loss)freeze(*u.claim,u.disconnect,u.stamp,u.reason);
    }
    if(!s->updates.empty())advance(s->registry);
}
void aether_dom_interaction_retire_node(aether_node_s* node,bool exhausted) noexcept {
    for(auto& weak:node->identity->interaction_claims)if(auto claim=weak.lock())if(auto r=claim->registry.lock()) {
        if(claim->active&&!claim->lost&&!claim->acknowledged&&readable(r)) {
            freeze(*claim,node->identity->disconnect_serial,node->identity->state_bit_write_serial[bit_index(claim->info.bit)],
                exhausted?AETHER_DOM_LOSS_RETIRED_EXHAUSTED:AETHER_DOM_LOSS_RETIRED);advance(r);
        }
    }
}
AETHER_API aether_status_t aether_dom_interaction_owner_prepare_v1(aether_doc_t doc,uint64_t host,aether_dom_interaction_owner_t* out) {
    if(out)*out=nullptr;if(!doc||!out)return AETHER_ERROR_NULL_ARG;
    if(doc->owner_control->owner_thread!=std::this_thread::get_id()||!host||!doc->owner_control->interaction.expired()||doc->owner_control->interaction_serial==UINT64_MAX||aether_dom_access(doc)!=AETHER_OK)return AETHER_ERROR_INVALID_STATE;
    try {
        auto handle=std::make_unique<aether_dom_interaction_owner_s>();auto r=std::make_shared<DomInteractionRegistry>();
        r->document=aether_editable_lease(doc);if(!r->document)return AETHER_ERROR_INVALID_STATE;
        r->owner=doc->owner_control;r->host_identity=host;r->prepared_serial=r->owner->interaction_serial;handle->value=r;
        *out=handle.release();return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API aether_status_t aether_dom_interaction_owner_validate_prepared_v1(aether_dom_interaction_owner_t owner) {
    if(!owner)return AETHER_ERROR_NULL_ARG;auto r=owner->value;
    return thread(r)&&r->prepared&&!r->active&&aether_dom_access(document(r))==AETHER_OK&&r->owner->interaction.expired()&&
        r->owner->interaction_serial==r->prepared_serial&&can_advance(r)?AETHER_OK:AETHER_ERROR_INVALID_STATE;
}
AETHER_API aether_status_t aether_dom_interaction_owner_activate_v1(aether_dom_interaction_owner_t owner,aether_dom_style_binding_t binding) {
    if(!owner||!binding)return AETHER_ERROR_NULL_ARG;auto status=aether_dom_interaction_owner_validate_prepared_v1(owner);if(status!=AETHER_OK)return status;
    auto r=owner->value;DomStyleBindingRef ref;status=aether_dom_style_binding_get_ref(binding,document(r),ref);if(status!=AETHER_OK)return status;
    r->binding=std::move(ref);r->active=true;r->prepared=false;
    r->owner->interaction_retire=aether_dom_interaction_retire_node;r->owner->interaction=r;advance(r);return AETHER_OK;
}
AETHER_API aether_status_t aether_dom_interaction_owner_create_v1(aether_doc_t doc,aether_dom_style_binding_t binding,uint64_t host,aether_dom_interaction_owner_t* out) {
    if(out)*out=nullptr;if(!doc||!binding||!out)return AETHER_ERROR_NULL_ARG;
    aether_dom_interaction_owner_t prepared=nullptr;auto status=aether_dom_interaction_owner_prepare_v1(doc,host,&prepared);if(status!=AETHER_OK)return status;
    status=aether_dom_interaction_owner_activate_v1(prepared,binding);if(status!=AETHER_OK){aether_dom_interaction_owner_release_v1(prepared);return status;}*out=prepared;return AETHER_OK;
}
AETHER_API aether_status_t aether_dom_interaction_owner_dispose_v1(aether_dom_interaction_owner_t owner) {
    if(!owner)return AETHER_ERROR_NULL_ARG;auto r=owner->value;if(!thread(r))return AETHER_ERROR_INVALID_STATE;
    if(!r->active){r->prepared=false;return AETHER_OK;}r->active=false;for(const auto& c:r->claims){revoke(c);c->acknowledged=true;}
    if(r->owner->interaction.lock()==r)r->owner->interaction.reset();advance(r);r->claims.clear();return AETHER_OK;
}
AETHER_API void aether_dom_interaction_owner_release_v1(aether_dom_interaction_owner_t owner){delete owner;}
AETHER_API aether_status_t aether_dom_interaction_prepare_acquire_v1(aether_dom_interaction_owner_t owner,
    const aether_dom_token_v1_t* expected,aether_node_t node,int32_t bit,uint64_t registry,aether_dom_interaction_edit_t* out) {
    if(out)*out=nullptr;if(!owner||!expected||!node||!out)return AETHER_ERROR_NULL_ARG;
    auto r=owner->value;if(!live(r)||!valid_bit(bit)||!registry||!can_advance(r)||r->owner->next_interaction_generation==UINT64_MAX||
        (!(node->state&bit)&&node->identity->state_bit_write_serial[bit_index(bit)]==UINT64_MAX)||
        aether_dom_access(node)!=AETHER_OK||node->type!=AETHER_NODE_ELEMENT||node->is_generated||!node->identity->attached||node->document_owner.lock()!=r->owner)return AETHER_ERROR_INVALID_STATE;
    try {
        auto e=std::make_unique<aether_dom_interaction_edit_s>();e->registry=r;e->serial=r->owner->interaction_serial;e->acquire=true;
        auto c=std::make_shared<DomInteractionClaim>();e->claim=c;c->registry=r;c->node=aether_editable_lease(node);if(!c->node)return AETHER_ERROR_INVALID_STATE;c->live_owner=aether_live_owner_pin(node);
        c->info.node=node;c->info.host_identity=r->host_identity;c->info.binding_identity=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(r->binding.get()));
        c->info.owner_identity=aether_node_identity(document(r));c->info.generation=r->owner->next_interaction_generation;c->info.node_identity=aether_node_identity(node);
        c->info.disconnect_serial=node->identity->disconnect_serial;c->info.bit=bit;c->info.registry_identity=registry;
        c->info.state_bit_write_serial=node->identity->state_bit_write_serial[bit_index(bit)]+((node->state&bit)?0:1);
        if(bit==AETHER_STATE_FOCUS)if(auto route=node->attributes.find("onblur");route!=node->attributes.end()) {
            if(!aether_dom_valid_string(route->second))return AETHER_ERROR_INVALID_STATE;c->route_present=true;c->route=route->second;
        }
        e->previous=node->identity->interaction_claims[bit_index(bit)].lock();e->claims=r->claims;
        if(e->previous&&!e->previous->lost)e->claims.erase(std::remove(e->claims.begin(),e->claims.end(),e->previous),e->claims.end());e->claims.push_back(c);
        e->output=std::make_unique<aether_dom_interaction_claim_s>();e->output->value=c;
        e->candidate={c->info,node->state,node->state|bit,1,(node->state&bit)?0:1};e->candidate.claim.current=1;
        auto status=prepare_state(*e,*expected,e->candidate.after_state);if(status!=AETHER_OK)return status;
        *out=e.release();return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API aether_status_t aether_dom_interaction_prepare_cleanup_v1(aether_dom_interaction_claim_t handle,
    uint64_t generation,uint64_t record_id,uint64_t record_serial,aether_dom_interaction_edit_t* out) {
    if(out)*out=nullptr;if(!handle||!out)return AETHER_ERROR_NULL_ARG;auto c=handle->value;auto r=c->registry.lock();
    if(!readable(r)||generation!=c->info.generation||c->acknowledged||!exact_loss(*c,record_id,record_serial)||!can_advance(r))return AETHER_ERROR_INVALID_STATE;
    try {
        auto e=std::make_unique<aether_dom_interaction_edit_s>();e->registry=r;e->claim=c;e->serial=r->owner->interaction_serial;e->record_id=record_id;e->record_serial=record_serial;
        auto* node=c->node->node.get();const bool applied=live(r)&&c->active&&authority(c)&&node->identity->alive&&node->document_owner.lock()==r->owner&&node->identity->state_bit_write_serial[bit_index(c->info.bit)]==c->info.state_bit_write_serial;
        e->candidate={info(c,r),node->state,applied?node->state&~c->info.bit:node->state,applied?1:0,applied&&(node->state&c->info.bit)?1:0};
        if(applied) {
            aether_dom_token_v1_t expected{};auto status=aether_dom_read_token_v1(node,&expected);if(status!=AETHER_OK)return status;
            status=prepare_state(*e,expected,e->candidate.after_state);if(status!=AETHER_OK)return status;
        } else if(node->identity->alive) {
            const auto status=aether_dom_read_token_v1(node,&e->no_op_token);if(status!=AETHER_OK&&status!=AETHER_ERROR_DISPOSED)return status;
        }
        *out=e.release();return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API aether_status_t aether_dom_interaction_edit_candidate_v1(aether_dom_interaction_edit_t e,aether_dom_interaction_candidate_v1_t* out) {
    if(out)*out={};if(!e||!out)return AETHER_ERROR_NULL_ARG;*out=e->candidate;return AETHER_OK;
}
AETHER_API aether_status_t aether_dom_interaction_edit_validate_v1(aether_dom_interaction_edit_t e) {
    if(!e)return AETHER_ERROR_NULL_ARG;if(e->committed||!readable(e->registry)||e->registry->owner->interaction_serial!=e->serial)return AETHER_ERROR_INVALID_STATE;
    if(e->acquire&&!live(e->registry))return AETHER_ERROR_INVALID_STATE;
    if(!e->acquire&&!exact_loss(*e->claim,e->record_id,e->record_serial))return AETHER_ERROR_INVALID_STATE;
    if(e->state_edit)return aether_dom_edit_validate_v1(e->state_edit);
    // A stale cleanup never writes State or returns a token for a later value.
    auto* node=e->claim->node->node.get();if(node->identity->alive&&e->no_op_token.owner_identity) {
        aether_dom_token_v1_t current_token{};if(aether_dom_read_token_v1(node,&current_token)!=AETHER_OK||
            current_token.owner_identity!=e->no_op_token.owner_identity||current_token.observation_serial!=e->no_op_token.observation_serial)return AETHER_ERROR_INVALID_STATE;
    }
    return AETHER_OK;
}
AETHER_API aether_status_t aether_dom_interaction_edit_commit_v1(aether_dom_interaction_edit_t e,aether_dom_interaction_claim_t* out_claim,aether_dom_token_v1_t* out_token) {
    if(out_claim)*out_claim=nullptr;if(out_token)*out_token={};if(!e||!out_claim||!out_token)return AETHER_ERROR_NULL_ARG;
    auto status=aether_dom_interaction_edit_validate_v1(e);if(status!=AETHER_OK)return status;
    if(e->state_edit){status=aether_dom_edit_commit_v1(e->state_edit,out_token);if(status!=AETHER_OK)return status;}else *out_token=e->no_op_token;
    auto r=e->registry;auto c=e->claim;
    if(e->acquire) {
        if(e->previous&&!e->previous->lost)e->previous->active=false;
        r->claims.swap(e->claims);c->active=true;c->node->node->identity->interaction_claims[bit_index(c->info.bit)]=c;
        ++r->owner->next_interaction_generation;*out_claim=e->output.release();
    } else {
        revoke(c);if(!c->lost)erase(r,c);
    }
    advance(r);e->committed=true;return AETHER_OK;
}
AETHER_API void aether_dom_interaction_edit_release_v1(aether_dom_interaction_edit_t e){delete e;}
AETHER_API aether_status_t aether_dom_interaction_claim_read_v1(aether_dom_interaction_claim_t handle,aether_dom_interaction_claim_info_v1_t* out) {
    if(out)*out={};if(!handle||!out)return AETHER_ERROR_NULL_ARG;auto c=handle->value;auto r=c->registry.lock();if(!readable(r))return AETHER_ERROR_DISPOSED;*out=info(c,r);return AETHER_OK;
}
AETHER_API aether_status_t aether_dom_interaction_claim_read_loss_v1(aether_dom_interaction_claim_t handle,int32_t* present,aether_dom_interaction_loss_v1_t* out) {
    if(present)*present=0;if(out)*out={};if(!handle||!present||!out)return AETHER_ERROR_NULL_ARG;auto c=handle->value;auto r=c->registry.lock();if(!readable(r))return AETHER_ERROR_DISPOSED;
    if(!c->lost||c->acknowledged)return AETHER_OK;*present=1;out->claim=c->info;out->claim.current=0;
    out->record_id=c->info.generation;out->record_serial=c->info.generation;out->disconnect_serial=c->loss_disconnect;out->state_bit_write_serial=c->loss_stamp;out->reason=c->loss_reason;
    out->handler_present=c->route_present?1:0;out->handler={c->route.data(),c->route.size()};return AETHER_OK;
}
AETHER_API aether_status_t aether_dom_interaction_claim_ack_v1(aether_dom_interaction_claim_t handle,uint64_t generation,uint64_t record_id,uint64_t record_serial) {
    if(!handle)return AETHER_ERROR_NULL_ARG;auto c=handle->value;auto r=c->registry.lock();if(!readable(r)||generation!=c->info.generation||!c->lost||c->acknowledged||!exact_loss(*c,record_id,record_serial))return AETHER_ERROR_INVALID_STATE;
    revoke(c);c->acknowledged=true;erase(r,c);advance(r);return AETHER_OK;
}
AETHER_API aether_status_t aether_dom_interaction_claim_dispose_v1(aether_dom_interaction_claim_t handle) {
    if(!handle)return AETHER_ERROR_NULL_ARG;auto c=handle->value;auto r=c->registry.lock();if(!readable(r)||(c->lost&&!c->acknowledged))return AETHER_ERROR_INVALID_STATE;
    if(c->acknowledged)return AETHER_OK;revoke(c);c->acknowledged=true;erase(r,c);advance(r);return AETHER_OK;
}
AETHER_API void aether_dom_interaction_claim_release_v1(aether_dom_interaction_claim_t handle){delete handle;}
