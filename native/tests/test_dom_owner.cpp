#include <aether/aether_dom_owner.h>
#include <aether/aether_dom_sync.h>
#include <aether/aether_transition_sync.h>
#include <aether/aether_contextual_transitions.h>
#include "css/computed_style_handle.h"
#include "dom/dom_types.h"
#include "css/style_map_version.h"
#include "layout/layout_assets.h"
#include "animations/transition_runtime.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
static thread_local long fail_after = -1;
void* operator new(std::size_t n) { if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();} } while(0)
static aether_dom_owner_t make(aether_doc_t& doc) {
    aether_dom_owner_t owner=nullptr;
    CHECK(aether_dom_owner_create_v1(&owner,&doc)==AETHER_OK);
    CHECK(aether_doc_parse_html(doc,"<body><div id='a'>text<!--comment--></div></body>")==AETHER_OK);
    return owner;
}
static void release_elsewhere(aether_dom_owner_t owner) {
    std::thread release([=]{aether_dom_owner_release_v1(owner);});release.join();
}
int main() {
    bool success=false;
    for(long n=0;n<20;++n) {
        auto owner=reinterpret_cast<aether_dom_owner_t>(1);auto doc=reinterpret_cast<aether_doc_t>(1);
        fail_after=n;const auto status=aether_dom_owner_create_v1(&owner,&doc);fail_after=-1;
        if(status==AETHER_OK){aether_dom_owner_release_v1(owner);success=true;break;}
        CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!owner&&!doc);
    }
    CHECK(success);
    {
        aether_doc_t doc=nullptr;auto owner=make(doc);auto weak=doc->editable_storage;
        auto* node=aether_doc_get_element_by_id(doc,"a");auto token=node->identity;
        aether_dom_node_lease_t lease=nullptr;CHECK(aether_dom_retain_node_v1(node,&lease)==AETHER_OK);
        release_elsewhere(owner);CHECK(!weak.expired()&&token->alive);
        aether_dom_token_v1_t observed{};CHECK(aether_dom_read_token_v1(node,&observed)==AETHER_OK);
        CHECK(aether_element_set_attribute(node,"after-gc","yes")==AETHER_OK);
        aether_dom_node_lease_destroy_v1(lease);CHECK(weak.expired()&&token->storage.expired()&&!token->alive);
    }
    {
        aether_doc_t doc=nullptr;auto owner=make(doc);auto weak=doc->editable_storage;
        aether::style::MapVersionRef map;
        {
            aether::style::MapVersionInput input;aether::style::BackingBuildInput empty;
            CHECK(aether::style::freeze_backing(empty,input.backing)==AETHER_OK);
            input.document_lifetime=doc->identity;input.document_identity=aether_node_identity(doc);
            input.structure_epoch=doc->identity->structure_epoch;
            CHECK(aether::style::make_map_version(input,map)==AETHER_OK);
        }
        release_elsewhere(owner);CHECK(!weak.expired()&&map->size()==0);
        CHECK(aether::style::structure_valid(map.get())&&aether::style::inputs_current(map.get()));
        map.reset();CHECK(weak.expired());
    }
    {
        aether_doc_t doc=nullptr;auto owner=make(doc);auto weak=doc->editable_storage;
        auto* text=aether_doc_get_element_by_id(doc,"a")->children.front();auto textweak=text->editable_storage;
        auto assets=std::make_shared<aether::layout::LayoutAssets>(nullptr,nullptr,nullptr,
            doc->identity,doc->identity->structure_epoch,aether_pin_dom_snapshot(doc));
        release_elsewhere(owner);CHECK(!weak.expired()&&!textweak.expired()&&assets->structure_valid());
        assets.reset();CHECK(weak.expired()&&textweak.expired());
    }
    {
        aether_doc_t doc=nullptr;auto owner=make(doc);auto weak=doc->editable_storage;
        auto* node=aether_doc_get_element_by_id(doc,"a");
        aether_transition_runtime_t runtime=nullptr;CHECK(aether_transition_runtime_create(&runtime)==AETHER_OK);
        auto before=aether_computed_style_default(),after=before;
        after.opacity=0;after.transition_properties=AETHER_TRANSITION_PROPERTY_OPACITY;after.transition_duration=1;
        CHECK(aether_transition_runtime_observe(runtime,node,&before,&after,0)==AETHER_OK);
        aether_transition_candidate_t candidate=nullptr;
        CHECK(aether_transition_runtime_prepare(runtime,nullptr,0,nullptr,0,0,&candidate)==AETHER_OK);
        release_elsewhere(owner);CHECK(!weak.expired());
        CHECK(aether_transition_candidate_validate(candidate)==AETHER_OK);
        CHECK(aether_transition_runtime_has_active(runtime));
        CHECK(aether_transition_runtime_tick(runtime,500));
        aether_transition_runtime_destroy(runtime);CHECK(!weak.expired());
        aether_transition_candidate_destroy(candidate);CHECK(weak.expired());
    }
    {
        aether_doc_t doc=nullptr;auto owner=make(doc);auto weak=doc->editable_storage;
        auto* node=aether_doc_get_element_by_id(doc,"a");auto token=node->identity;
        aether_dom_node_lease_t lease=nullptr;CHECK(aether_dom_retain_node_v1(node,&lease)==AETHER_OK);
        aether_status_t wrong=AETHER_OK;std::thread thread([&]{wrong=aether_dom_owner_retire_v1(owner);});thread.join();
        CHECK(wrong==AETHER_ERROR_INVALID_STATE&&token->alive);
        CHECK(aether_dom_owner_retire_v1(owner)==AETHER_OK&&!token->alive);
        CHECK(aether_dom_owner_retire_v1(owner)==AETHER_OK);
        release_elsewhere(owner);CHECK(!weak.expired());aether_dom_node_lease_destroy_v1(lease);CHECK(weak.expired());
    }
    // The native node lease alone follows two ownership changes. Historical A
    // remains pinned independently, but the live relay must retain B then C.
    {
        aether_doc_t a=nullptr,b=nullptr,c=nullptr;auto oa=make(a),ob=make(b),oc=make(c);
        auto wa=a->editable_storage,wb=b->editable_storage,wc=c->editable_storage;
        auto* node=aether_doc_get_element_by_id(a,"a");auto identity=node->identity;
        aether_dom_node_lease_t lease=nullptr;CHECK(aether_dom_retain_node_v1(node,&lease)==AETHER_OK);
        auto history=aether_pin_dom_snapshot(a);
        CHECK(aether_node_append_child(aether_doc_get_body(b),node)==AETHER_OK);
        release_elsewhere(ob);CHECK(!wb.expired());
        aether_dom_token_v1_t token{};CHECK(aether_dom_read_token_v1(node,&token)==AETHER_OK);
        CHECK(token.owner_identity==aether_node_identity(b));
        CHECK(aether_node_append_child(aether_doc_get_body(c),node)==AETHER_OK);
        CHECK(wb.expired());release_elsewhere(oc);release_elsewhere(oa);
        CHECK(!wc.expired()&&!wa.expired());CHECK(aether_dom_read_token_v1(node,&token)==AETHER_OK);
        CHECK(token.owner_identity==aether_node_identity(c));
        history.reset();aether_dom_node_lease_destroy_v1(lease);
        CHECK(wa.expired()&&wc.expired()&&identity->storage.expired());
    }
    // Generated descendants have no editable storage of their own. Their
    // relays follow the host, while the historical generated owner is pinned.
    {
        aether_doc_t a=nullptr,b=nullptr;auto oa=make(a),ob=make(b);
        auto wa=a->editable_storage,wb=b->editable_storage;
        auto* host=aether_doc_get_element_by_id(a,"a");
        auto pseudo=std::make_shared<aether_node_s>();pseudo->type=AETHER_NODE_ELEMENT;
        pseudo->is_generated=true;pseudo->snapshot_owned=true;pseudo->parent=host;pseudo->style_origin=host;
        host->generated_storage.push_back(pseudo);host->children.push_back(pseudo.get());
        auto pin=aether_pin_dom_snapshot(a);
        auto relay=pseudo->identity->live_owner_pin.lock();CHECK(relay&&relay->current_root==wa.lock());
        CHECK(aether_node_append_child(aether_doc_get_body(b),host)==AETHER_OK);
        CHECK(relay->current_root==wb.lock());release_elsewhere(ob);release_elsewhere(oa);
        CHECK(!wb.expired()&&pseudo->identity->alive&&pseudo->style_origin==host);
        relay.reset();pin.reset();CHECK(wa.expired()&&wb.expired());
        CHECK(!pseudo->identity->alive);pseudo.reset();
    }
    // Allocation failure cannot publish adoption or move its current-root relay.
    {
        bool completed=false;
        for(long n=0;n<160&&!completed;++n) {
            aether_doc_t a=nullptr,b=nullptr;auto oa=make(a),ob=make(b);
            auto* node=aether_doc_get_element_by_id(a,"a");auto* parent=node->parent;
            auto relay=aether_live_owner_pin(node);auto original=relay->current_root;
            aether_dom_token_v1_t expected{};CHECK(aether_dom_read_token_v1(b,&expected)==AETHER_OK);
            aether_dom_edit_spec_v1_t spec{};spec.kind=AETHER_DOM_INSERT_CHILD;
            spec.target=aether_doc_get_body(b);spec.child=node;spec.index=-1;
            aether_dom_edit_t edit=nullptr;fail_after=n;
            const auto status=aether_dom_prepare_edit_v1(&expected,&spec,&edit);fail_after=-1;
            CHECK(node->parent==parent&&relay->current_root==original);
            if(status==AETHER_OK){completed=true;aether_dom_edit_destroy_v1(edit);}
            else CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!edit);
            CHECK(aether_dom_owner_retire_v1(oa)==AETHER_OK);CHECK(aether_dom_owner_retire_v1(ob)==AETHER_OK);
            aether_dom_owner_release_v1(oa);aether_dom_owner_release_v1(ob);
        }
        CHECK(completed);
    }
    // Retired payloads preserve both immutable A provenance and current B life.
    {
        aether_doc_t a=nullptr,b=nullptr;auto oa=make(a),ob=make(b);
        auto wa=a->editable_storage,wb=b->editable_storage;auto* node=aether_doc_get_element_by_id(a,"a");
        std::unique_ptr<aether::style::FrozenRetiredNode> retired;
        {
            aether::style::NodePayloadInput input;input.node=node;input.node_lifetime=node->identity;
            input.document_lifetime=a->identity;input.node_identity=aether_node_identity(node);
            input.document_identity=aether_node_identity(a);
            auto flat=aether_computed_style_default();CHECK(aether::style::freeze_style(flat,input.style)==AETHER_OK);
            aether::style::FrozenNodePayloadRef payload;CHECK(aether::style::freeze_node_payload(input,payload)==AETHER_OK);
            retired=std::make_unique<aether::style::FrozenRetiredNode>(payload,0);
        }
        CHECK(aether_node_append_child(aether_doc_get_body(b),node)==AETHER_OK);
        release_elsewhere(oa);release_elsewhere(ob);CHECK(!wa.expired()&&!wb.expired()&&retired->node_lifetime()->alive);
        retired.reset();CHECK(wa.expired()&&wb.expired());
    }
    // A retired pseudo is no longer in the adopted live subtree. Its retained
    // origin/ancestor storage must still pin the origin's NEW owner through relay.
    {
        aether_doc_t a=nullptr,b=nullptr;auto oa=make(a),ob=make(b);
        auto wa=a->editable_storage,wb=b->editable_storage;auto* host=aether_doc_get_element_by_id(a,"a");
        auto pseudo=std::make_shared<aether_node_s>();pseudo->type=AETHER_NODE_ELEMENT;
        pseudo->is_generated=true;pseudo->snapshot_owned=true;pseudo->parent=host;pseudo->style_origin=host;
        host->generated_storage.push_back(pseudo);host->children.push_back(pseudo.get());
        std::unique_ptr<aether::style::FrozenRetiredNode> retired;
        {
            aether::style::NodePayloadInput input;input.node=pseudo.get();input.node_lifetime=pseudo->identity;
            input.document_lifetime=a->identity;input.node_identity=aether_node_identity(pseudo.get());
            input.document_identity=aether_node_identity(a);input.generated_leaf=pseudo;
            auto flat=aether_computed_style_default();CHECK(aether::style::freeze_style(flat,input.style)==AETHER_OK);
            aether::style::FrozenNodePayloadRef payload;CHECK(aether::style::freeze_node_payload(input,payload)==AETHER_OK);
            retired=std::make_unique<aether::style::FrozenRetiredNode>(payload,0);
        }
        host->children.pop_back();host->generated_storage.clear();pseudo->identity->attached=false;
        CHECK(aether_node_append_child(aether_doc_get_body(b),host)==AETHER_OK);
        release_elsewhere(oa);release_elsewhere(ob);CHECK(!wa.expired()&&!wb.expired());
        CHECK(pseudo->style_origin==host&&host->identity->alive&&host->parent->identity->alive);
        pseudo.reset();retired.reset();CHECK(wa.expired()&&wb.expired());
    }
    // Contextual batches independently pin entries after inputs/runtime die.
    {
        aether_doc_t a=nullptr,b=nullptr;auto oa=make(a),ob=make(b);
        auto wa=a->editable_storage,wb=b->editable_storage;auto* node=aether_doc_get_element_by_id(a,"a");
        aether_contextual_transition_runtime_t runtime=nullptr;
        CHECK(aether_contextual_transition_create(&runtime)==AETHER_OK);
        aether_contextual_transition_batch_t batch=nullptr;
        {
            aether_computed_style_handle_s before,after;
            before.style=after.style=aether_computed_style_default();after.style.opacity=0;
            after.style.transition_properties=AETHER_TRANSITION_PROPERTY_OPACITY;after.style.transition_duration=1;
            before.node_lifetime=after.node_lifetime=node->identity;before.document_lifetime=after.document_lifetime=a->identity;
            before.node_identity=after.node_identity=aether_node_identity(node);
            before.document_identity=after.document_identity=aether_node_identity(a);
            const aether_contextual_transition_event event{1,0,node,&before,&after};
            CHECK(aether_contextual_transition_prepare(runtime,&event,1,0,&batch)==AETHER_OK);
        }
        CHECK(aether_contextual_transition_commit(runtime,batch)==AETHER_OK);
        CHECK(aether_node_append_child(aether_doc_get_body(b),node)==AETHER_OK);
        release_elsewhere(oa);release_elsewhere(ob);aether_contextual_transition_destroy(runtime);
        CHECK(!wa.expired()&&!wb.expired());(void)aether_contextual_transition_batch_has_active(batch);
        aether_contextual_transition_batch_destroy(batch);CHECK(wa.expired()&&wb.expired());
    }
    // Preflight owns both domains and relay objects while finalizers drop the
    // external owner handles. Commit only swaps refs on the owner thread.
    {
        aether_doc_t a=nullptr,b=nullptr;auto oa=make(a),ob=make(b);
        auto wa=a->editable_storage,wb=b->editable_storage;
        auto* node=aether_doc_get_element_by_id(a,"a");
        aether_dom_node_lease_t lease=nullptr;CHECK(aether_dom_retain_node_v1(node,&lease)==AETHER_OK);
        aether_dom_edit_spec_v1_t spec{};spec.kind=AETHER_DOM_INSERT_CHILD;
        spec.target=aether_doc_get_body(b);spec.child=node;spec.index=-1;
        aether_dom_token_v1_t expected{};CHECK(aether_dom_read_token_v1(b,&expected)==AETHER_OK);
        aether_dom_edit_t edit=nullptr;CHECK(aether_dom_prepare_edit_v1(&expected,&spec,&edit)==AETHER_OK);
        std::atomic<bool> go=false;
        std::thread finalizer([&]{while(!go.load(std::memory_order_acquire)){};aether_dom_owner_release_v1(oa);aether_dom_owner_release_v1(ob);});
        go.store(true,std::memory_order_release);
        aether_dom_token_v1_t token{};CHECK(aether_dom_edit_commit_v1(edit,&token)==AETHER_OK);
        finalizer.join();aether_dom_edit_destroy_v1(edit);
        CHECK(!wb.expired());CHECK(aether_dom_read_token_v1(node,&token)==AETHER_OK);
        aether_dom_node_lease_destroy_v1(lease);CHECK(wa.expired()&&wb.expired());
    }
    std::puts("DOM owner lifetime tests PASS");
}
