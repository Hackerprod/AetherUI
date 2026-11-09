#include <aether/aether_dom_interaction.h>
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
static long fail_after=-1;
static bool fault_delivered=false;
void* operator new(size_t n){if(fail_after==0){fault_delivered=true;throw std::bad_alloc();}if(fail_after>0)--fail_after;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
#include "fault_inject_aligned.h"
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void ok(aether_status_t s){CHECK(s==AETHER_OK);}
static aether_dom_token_v1_t token(aether_node_t n){aether_dom_token_v1_t t{};ok(aether_dom_read_token_v1(n,&t));return t;}
struct Fixture {
    aether_doc_t doc=nullptr;aether_style_runtime_t runtime=nullptr;aether_dom_style_binding_t binding=nullptr;aether_dom_interaction_owner_t owner=nullptr;
    aether_node_t node=nullptr,body=nullptr,other=nullptr;
    Fixture(){ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,"<body><button id='x' onblur='handler A'>x</button><div id='other'></div></body>"));node=aether_doc_get_element_by_id(doc,"x");body=node->parent;other=aether_doc_get_element_by_id(doc,"other");ok(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&runtime));ok(aether_dom_style_binding_create_v1(doc,runtime,&binding));ok(aether_dom_interaction_owner_create_v1(doc,binding,77,&owner));}
    ~Fixture(){if(owner){ok(aether_dom_interaction_owner_dispose_v1(owner));aether_dom_interaction_owner_release_v1(owner);}aether_dom_style_binding_destroy_v1(binding);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
struct Claim {
    aether_dom_interaction_claim_t handle=nullptr;
    explicit Claim(aether_dom_interaction_claim_t c=nullptr):handle(c){}
    Claim(const Claim&)=delete;Claim& operator=(const Claim&)=delete;
    ~Claim(){aether_dom_interaction_claim_release_v1(handle);}
    aether_dom_interaction_claim_info_v1_t info(){aether_dom_interaction_claim_info_v1_t r{};ok(aether_dom_interaction_claim_read_v1(handle,&r));return r;}
    aether_dom_interaction_loss_v1_t loss(bool expected=true){aether_dom_interaction_loss_v1_t r{};int32_t present=-1;fail_after=0;const auto status=aether_dom_interaction_claim_read_loss_v1(handle,&present,&r);fail_after=-1;ok(status);CHECK(present==int(expected));return r;}
    void ack(){auto r=loss();ok(aether_dom_interaction_claim_ack_v1(handle,r.claim.generation,r.record_id,r.record_serial));}
};
static aether_dom_interaction_claim_t commit(aether_dom_interaction_edit_t e){aether_dom_interaction_claim_t claim=nullptr;aether_dom_token_v1_t output{};fail_after=0;auto status=aether_dom_interaction_edit_validate_v1(e);if(status==AETHER_OK)status=aether_dom_interaction_edit_commit_v1(e,&claim,&output);fail_after=-1;ok(status);aether_dom_interaction_edit_release_v1(e);return claim;}
static aether_dom_interaction_claim_t acquire(Fixture& f,int32_t bit=AETHER_STATE_FOCUS,aether_node_t target=nullptr){auto* node=target?target:f.node;auto t=token(node);aether_dom_interaction_edit_t e=nullptr;ok(aether_dom_interaction_prepare_acquire_v1(f.owner,&t,node,bit,991,&e));aether_dom_interaction_candidate_v1_t c{};ok(aether_dom_interaction_edit_candidate_v1(e,&c));CHECK(c.applied&&c.after_state==(node->state|bit));return commit(e);}
static aether_dom_interaction_edit_t cleanup(Claim& c,bool lost=true){auto i=c.info();auto l=c.loss(lost);aether_dom_interaction_edit_t e=nullptr;ok(aether_dom_interaction_prepare_cleanup_v1(c.handle,i.generation,l.record_id,l.record_serial,&e));return e;}
static bool applied(aether_dom_interaction_edit_t e){aether_dom_interaction_candidate_v1_t c{};ok(aether_dom_interaction_edit_candidate_v1(e,&c));return c.applied!=0;}
static aether_dom_edit_t prepare_remove(Fixture& f){auto t=token(f.body);aether_dom_edit_spec_v1_t s{};s.kind=AETHER_DOM_REMOVE_CHILD;s.target=f.body;s.child=f.node;aether_dom_edit_t e=nullptr;ok(aether_dom_prepare_edit_v1(&t,&s,&e));return e;}
static void route_and_first_loss(){
    Fixture f;Claim c(acquire(f));const auto first=c.info();
    ok(aether_element_set_attribute(f.node,"ONBLUR","handler B before disconnect"));
    ok(aether_node_remove_child(f.body,f.node));auto loss=c.loss();CHECK(loss.claim.generation==first.generation&&loss.claim.registry_identity==991&&std::string(loss.handler.data,loss.handler.length)=="handler B before disconnect");
    CHECK(loss.disconnect_serial==first.disconnect_serial+1&&loss.reason==AETHER_DOM_LOSS_DISCONNECT);
    ok(aether_element_set_attribute(f.node,"onblur","handler C after disconnect"));ok(aether_node_append_child(f.body,f.node));
    for(int i=0;i<180;++i)aether_element_set_state(f.other,i&1?AETHER_STATE_INVALID:0);
    ok(aether_node_remove_child(f.body,f.node));auto second=c.loss();CHECK(second.record_id==loss.record_id&&second.record_serial==loss.record_serial&&second.disconnect_serial==loss.disconnect_serial&&std::string(second.handler.data,second.handler.length)=="handler B before disconnect");
    aether_dom_interaction_edit_t rejected=nullptr;CHECK(aether_dom_interaction_prepare_cleanup_v1(c.handle,first.generation,0,0,&rejected)==AETHER_ERROR_INVALID_STATE&&!rejected);
    CHECK(aether_dom_interaction_claim_ack_v1(c.handle,first.generation,loss.record_id+1,loss.record_serial)==AETHER_ERROR_INVALID_STATE);
    aether_element_set_state(f.node,f.node->state|AETHER_STATE_INVALID);auto e=cleanup(c);CHECK(applied(e));CHECK(commit(e)==nullptr);CHECK(f.node->state==AETHER_STATE_INVALID);c.ack();c.loss(false);
    CHECK(aether_dom_interaction_claim_ack_v1(c.handle,first.generation,loss.record_id,loss.record_serial)==AETHER_ERROR_INVALID_STATE);
}
static void continuity_and_new_claim(){
    Fixture f;Claim old(acquire(f));const auto generation=old.info().generation;
    ok(aether_node_append_child(f.other,f.node));CHECK(old.info().current);old.loss(false);ok(aether_node_append_child(f.body,f.node));
    auto removal=prepare_remove(f);const auto before=token(f.node);Claim replacement(acquire(f));CHECK(replacement.info().generation>generation&&token(f.node).observation_serial==before.observation_serial);
    aether_dom_token_v1_t out{};CHECK(aether_dom_edit_commit_v1(removal,&out)==AETHER_ERROR_INVALID_STATE&&f.node->parent==f.body);aether_dom_edit_destroy_v1(removal);
    auto stale=cleanup(old,false);CHECK(!applied(stale));commit(stale);CHECK(f.node->state&AETHER_STATE_FOCUS);
    ok(aether_node_remove_child(f.body,f.node));auto lost=replacement.loss();ok(aether_node_append_child(f.body,f.node));Claim newer(acquire(f));
    stale=cleanup(replacement);CHECK(!applied(stale));commit(stale);replacement.ack();CHECK(newer.info().current&&(f.node->state&AETHER_STATE_FOCUS));
    // An old acknowledgment cannot consume the next claim's independent loss.
    ok(aether_node_remove_child(f.body,f.node));auto next_loss=newer.loss();CHECK(next_loss.record_id!=lost.record_id);
    CHECK(aether_dom_interaction_claim_ack_v1(replacement.handle,lost.claim.generation,lost.record_id,lost.record_serial)==AETHER_ERROR_INVALID_STATE);CHECK(newer.loss().record_id==next_loss.record_id);
}
static void adoption_and_stamps(){
    for(int32_t bit:{AETHER_STATE_FOCUS,AETHER_STATE_ACTIVE,AETHER_STATE_HOVER}) {
        Fixture source,dest;Claim old(acquire(source,bit));ok(aether_node_append_child(dest.body,source.node));old.loss();Claim next(acquire(dest,bit,source.node));
        auto e=cleanup(old);CHECK(!applied(e));commit(e);old.ack();CHECK(next.info().current&&(source.node->state&bit));
    }
    Fixture f;Claim c(acquire(f));ok(aether_node_remove_child(f.body,f.node));c.loss();
    aether_element_set_state(f.node,0);aether_element_set_state(f.node,AETHER_STATE_FOCUS);auto e=cleanup(c);CHECK(!applied(e));commit(e);CHECK(f.node->state==AETHER_STATE_FOCUS);c.ack();
}
static void destroy_and_retire(){
    {Fixture f;Claim c(acquire(f));aether_doc_destroy(f.doc);f.doc=nullptr;auto l=c.loss();CHECK(l.reason==AETHER_DOM_LOSS_RETIRED&&!f.node->identity->alive);auto e=cleanup(c);CHECK(!applied(e));commit(e);c.ack();}
    {Fixture f;Claim c(acquire(f));ok(aether_doc_parse_html(f.doc,"<body>new document</body>"));auto l=c.loss();CHECK(l.reason==AETHER_DOM_LOSS_RETIRED&&std::string(l.handler.data,l.handler.length)=="handler A");c.ack();}
    {Fixture f;Claim c(acquire(f));f.node->identity->disconnect_serial=UINT64_MAX;f.doc->owner_control->interaction_serial=UINT64_MAX;aether_doc_destroy(f.doc);f.doc=nullptr;auto l=c.loss();CHECK(l.reason==AETHER_DOM_LOSS_RETIRED_EXHAUSTED&&l.disconnect_serial==UINT64_MAX&&l.record_id&&l.record_serial);c.ack();CHECK(f.node->identity->disconnect_serial==UINT64_MAX);}
}
static void faults_and_limits(){
    Fixture f;const auto base=token(f.node);const auto generation=f.doc->owner_control->next_interaction_generation;const auto serial=f.doc->owner_control->interaction_serial;
    aether_dom_interaction_claim_t result=nullptr;bool success=false;
    for(int n=0;n<200;++n){aether_dom_interaction_edit_t e=nullptr;fail_after=n;auto status=aether_dom_interaction_prepare_acquire_v1(f.owner,&base,f.node,AETHER_STATE_FOCUS,991,&e);fail_after=-1;if(status==AETHER_OK){result=commit(e);success=true;break;}CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!e&&f.node->state==0&&token(f.node).observation_serial==base.observation_serial&&f.doc->owner_control->interaction_serial==serial&&f.doc->owner_control->next_interaction_generation==generation);}
    CHECK(success);Claim c(result);const auto stamped=c.info();
    for(int mode=0;mode<3;++mode){success=false;auto t=token(f.node);const auto old_serial=f.doc->owner_control->interaction_serial;for(int n=0;n<300;++n){fail_after=n;aether_status_t status=mode==0?aether_element_set_attribute(f.node,"onblur","a long route updated in staging before removal and therefore copied"):mode==1?aether_node_remove_child(f.body,f.node):aether_doc_parse_html(f.doc,"<body>replacement</body>");fail_after=-1;if(status==AETHER_OK){success=true;break;}CHECK(status==AETHER_ERROR_OUT_OF_MEMORY);CHECK(token(f.doc).observation_serial==t.observation_serial&&f.doc->owner_control->interaction_serial==old_serial);c.loss(mode==2);}CHECK(success);if(mode==0)c.loss(false);else c.loss();}
    CHECK(c.loss().claim.state_bit_write_serial==stamped.state_bit_write_serial);
    Fixture limited;limited.doc->owner_control->next_interaction_generation=UINT64_MAX;auto t=token(limited.node);aether_dom_interaction_edit_t e=nullptr;CHECK(aether_dom_interaction_prepare_acquire_v1(limited.owner,&t,limited.node,AETHER_STATE_FOCUS,9,&e)==AETHER_ERROR_INVALID_STATE&&!e&&limited.node->state==0);
}
static void threads_and_release_only(){
    Fixture f;Claim c(acquire(f));aether_status_t status=AETHER_OK;std::thread wrong([&]{status=aether_dom_interaction_owner_dispose_v1(f.owner);});wrong.join();CHECK(status==AETHER_ERROR_INVALID_STATE&&c.info().current);
    auto removal=prepare_remove(f);aether_dom_interaction_owner_release_v1(f.owner);f.owner=nullptr; // transaction retains registry and node.
    aether_dom_token_v1_t output{};fail_after=0;ok(aether_dom_edit_commit_v1(removal,&output));fail_after=-1;c.loss();c.ack();aether_dom_edit_destroy_v1(removal);
    aether_dom_interaction_claim_info_v1_t info{};CHECK(aether_dom_interaction_claim_read_v1(c.handle,&info)==AETHER_ERROR_DISPOSED);
    aether_dom_interaction_owner_t fresh=nullptr;ok(aether_dom_interaction_owner_create_v1(f.doc,f.binding,78,&fresh));f.owner=fresh;ok(aether_node_append_child(f.body,f.node));Claim next(acquire(f));CHECK(next.info().generation>1);
}
static void same_owner_detached_parent(){
    Fixture f;Claim c(acquire(f));const auto before=c.info().disconnect_serial;
    ok(aether_node_remove_child(f.body,f.other));ok(aether_node_append_child(f.other,f.node));
    CHECK(!f.node->identity->attached&&f.node->identity->disconnect_serial==before+1);auto loss=c.loss();
    ok(aether_node_append_child(f.body,f.other));CHECK(f.node->identity->attached&&c.loss().record_id==loss.record_id&&!c.info().current);
    c.ack();
    // The legacy path without any binding follows the same continuity rule.
    aether_doc_t doc=nullptr;ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,"<body><a id='a'></a><div id='p'></div></body>"));
    auto* a=aether_doc_get_element_by_id(doc,"a");auto* p=aether_doc_get_element_by_id(doc,"p");auto* body=a->parent;
    ok(aether_node_remove_child(body,p));const auto serial=a->identity->disconnect_serial;
    ok(aether_node_append_child(p,a));CHECK(!a->identity->attached&&a->identity->disconnect_serial==serial+1);
    ok(aether_node_append_child(body,p));CHECK(a->identity->attached&&a->identity->disconnect_serial==serial+1);aether_doc_destroy(doc);
}
static void prepared_owners_and_claims(){
    aether_doc_t doc=nullptr;aether_style_runtime_t runtime=nullptr;aether_dom_style_binding_t binding=nullptr;
    ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,"<body><button onblur='restored'>button</button></body>"));
    auto* body=aether_node_get_child(doc,0);auto* node=aether_node_get_child(body,0);
    ok(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&runtime));
    aether_element_set_state(node,AETHER_STATE_FOCUS|AETHER_STATE_HOVER);
    aether_dom_interaction_owner_t owner=nullptr;ok(aether_dom_interaction_owner_prepare_v1(doc,66,&owner));
    const auto serial=doc->owner_control->interaction_serial,generation=doc->owner_control->next_interaction_generation;const auto base=token(doc);
    aether_dom_interaction_claim_t first=nullptr;bool success=false;
    for(int i=0;i<100;++i){fail_after=i;auto status=aether_dom_interaction_owner_prepare_claim_v1(owner,node,AETHER_STATE_FOCUS,88,&first);fail_after=-1;if(status==AETHER_OK){success=true;break;}CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!first);CHECK(doc->owner_control->interaction.expired()&&doc->owner_control->interaction_serial==serial&&doc->owner_control->next_interaction_generation==generation&&node->identity->interaction_claims[2].expired());}
    CHECK(success);Claim focus(first);aether_dom_interaction_claim_info_v1_t unavailable{};CHECK(aether_dom_interaction_claim_read_v1(first,&unavailable)==AETHER_ERROR_DISPOSED);
    aether_dom_interaction_claim_t other=nullptr;CHECK(aether_dom_interaction_owner_prepare_claim_v1(owner,node,AETHER_STATE_FOCUS,88,&other)==AETHER_ERROR_INVALID_STATE&&!other);
    CHECK(aether_dom_interaction_owner_prepare_claim_v1(owner,node,AETHER_STATE_ACTIVE,88,&other)==AETHER_ERROR_INVALID_STATE&&!other);
    ok(aether_dom_interaction_owner_prepare_claim_v1(owner,node,AETHER_STATE_HOVER,88,&other));Claim hover(other);
    CHECK(token(doc).observation_serial==base.observation_serial&&doc->owner_control->interaction.expired());
    ok(aether_dom_style_binding_create_v1(doc,runtime,&binding));fail_after=0;ok(aether_dom_interaction_owner_validate_prepared_v1(owner));ok(aether_dom_interaction_owner_activate_v1(owner,binding));fail_after=-1;
    CHECK(focus.info().current&&hover.info().current&&focus.info().binding_identity&&doc->owner_control->next_interaction_generation==generation+2);
    ok(aether_dom_interaction_owner_dispose_v1(owner));aether_dom_interaction_owner_release_v1(owner);owner=nullptr;
    const auto disposed_serial=doc->owner_control->interaction_serial;
    ok(aether_dom_interaction_owner_prepare_v1(doc,67,&owner));ok(aether_dom_interaction_owner_prepare_claim_v1(owner,node,AETHER_STATE_FOCUS,89,&other));Claim aborted(other);
    aether_dom_interaction_owner_release_v1(owner);owner=nullptr;
    CHECK(aether_dom_interaction_claim_read_v1(other,&unavailable)==AETHER_ERROR_DISPOSED&&doc->owner_control->interaction_serial==disposed_serial&&doc->owner_control->interaction.expired());
    ok(aether_dom_interaction_owner_prepare_v1(doc,68,&owner));ok(aether_dom_interaction_owner_prepare_claim_v1(owner,node,AETHER_STATE_FOCUS,90,&other));Claim stale(other);
    aether_element_set_state(node,AETHER_STATE_HOVER);CHECK(aether_dom_interaction_owner_validate_prepared_v1(owner)==AETHER_ERROR_INVALID_STATE&&doc->owner_control->interaction.expired());aether_dom_interaction_owner_release_v1(owner);owner=nullptr;
    aether_element_set_state(node,AETHER_STATE_FOCUS);doc->owner_control->next_interaction_generation=UINT64_MAX;
    ok(aether_dom_interaction_owner_prepare_v1(doc,69,&owner));other=nullptr;CHECK(aether_dom_interaction_owner_prepare_claim_v1(owner,node,AETHER_STATE_FOCUS,90,&other)==AETHER_ERROR_INVALID_STATE&&!other);aether_dom_interaction_owner_release_v1(owner);
    aether_dom_style_binding_destroy_v1(binding);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
}
int main(){prepared_owners_and_claims();same_owner_detached_parent();route_and_first_loss();continuity_and_new_claim();adoption_and_stamps();destroy_and_retire();faults_and_limits();threads_and_release_only();std::puts("DOM interaction claims PASS");}
