#if defined(AETHER_DOM_ALLOCATIONS_UNAVAILABLE)
#include <cstdio>
int main(){std::puts("UNAVAILABLE DOM allocation-failure coverage: unsupported link unit");return 77;}
#else
#include <aether/aether_dom_sync.h>
#include <aether/aether_contextual_style.h>
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#if defined(AETHER_DOM_FAULT_LINK_UNIT)
#include "fault_allocator.h"
struct FaultInterval {
    void operator=(long ordinal) const {
        if(ordinal<0){fault_alloc::end();fault_alloc::check(fault_alloc::valid(),"DOM fault interval validity");}
        else {fault_alloc::reset(static_cast<uint64_t>(ordinal));fault_alloc::begin();}
    }
};
static FaultInterval failure;
#else
static long failure=-1;void* operator new(std::size_t n){if(failure==0)throw std::bad_alloc();if(failure>0)--failure;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
// Keep nothrow allocations in the same fault-allocator family. Otherwise ASan
// intercepts nothrow new while our sized delete calls free, a harness mismatch.
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
#endif
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::abort();}}while(0)
static aether_dom_string_v1_t str(const char* s){return {s,std::strlen(s)};}
static aether_dom_token_v1_t token(aether_node_t n){aether_dom_token_v1_t t{};CHECK(aether_dom_read_token_v1(n,&t)==AETHER_OK);return t;}
static aether_dom_edit_t prepare(aether_dom_edit_spec_v1_t& s){auto t=token(s.target);aether_dom_edit_t e=nullptr;CHECK(aether_dom_prepare_edit_v1(&t,&s,&e)==AETHER_OK);return e;}
static void commit(aether_dom_edit_t e){aether_dom_token_v1_t t{};failure=0;auto status=aether_dom_edit_commit_v1(e,&t);failure=-1;
#if defined(AETHER_DOM_FAULT_LINK_UNIT)
    CHECK(fault_alloc::counts.requests==0);
#endif
    CHECK(status==AETHER_OK);aether_dom_edit_destroy_v1(e);}
static void edit(aether_dom_edit_spec_v1_t s){commit(prepare(s));}
static void full_style(aether_style_runtime_t runtime,aether_doc_t doc) {
    aether_style_transaction_t transaction=nullptr;aether_style_revision_t revision=nullptr;
    CHECK(aether_style_runtime_prepare_revision(runtime,doc,nullptr,&transaction)==AETHER_OK);
    CHECK(aether_style_runtime_commit_revision(runtime,transaction,&revision)==AETHER_OK);
    aether_style_revision_destroy(revision);aether_style_transaction_destroy(transaction);
}
static void style_replacement_invalidates_edit() {
    // Links-only, remove-descendants, source-domain adopt and destination-domain
    // adopt must all reject style-only R2, without dereferencing old generated.
    for(int mode=0;mode<4;++mode) {
        aether_doc_t source=nullptr,destination=nullptr;
        CHECK(aether_doc_create(&source)==AETHER_OK);CHECK(aether_doc_create(&destination)==AETHER_OK);
        CHECK(aether_doc_parse_html(source,"<body><div id='host'><span>authored</span></div></body>")==AETHER_OK);
        CHECK(aether_doc_parse_html(destination,"<body><div id='target'></div></body>")==AETHER_OK);
        aether_style_runtime_t source_runtime=nullptr,destination_runtime=nullptr;
        CHECK(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&source_runtime)==AETHER_OK);
        CHECK(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&destination_runtime)==AETHER_OK);
        int64_t sheet=0;
        CHECK(aether_style_runtime_add_stylesheet(source_runtime,"div::before,span::after{content:'generated'}",2,"test",&sheet,nullptr)==AETHER_OK);
        CHECK(aether_style_runtime_add_stylesheet(destination_runtime,"div::before{content:'target'}",2,"test",&sheet,nullptr)==AETHER_OK);
        full_style(source_runtime,source);full_style(destination_runtime,destination);
        auto* host=aether_doc_get_element_by_id(source,"host");auto* target=aether_doc_get_element_by_id(destination,"target");
        CHECK(!host->generated_storage.empty()&&!target->generated_storage.empty());
        std::weak_ptr<aether_node_s> old=mode==3?target->generated_storage.front():host->generated_storage.front();
        auto* old_parent=host->parent;
        aether_dom_node_spec_v1_t text{};text.type=AETHER_NODE_TEXT;text.parent_index=-1;text.text=str("new");
        aether_dom_edit_spec_v1_t spec{};spec.index=-1;
        if(mode==0){spec.kind=AETHER_DOM_INSERT_SUBTREE;spec.target=host;spec.nodes=&text;spec.node_count=1;}
        else if(mode==1){spec.kind=AETHER_DOM_REMOVE_CHILD;spec.target=old_parent;spec.child=host;}
        else{spec.kind=AETHER_DOM_INSERT_CHILD;spec.target=target;spec.child=host;}
        auto edit=prepare(spec);auto* changed=mode==3?destination:source;
        const auto before=token(changed);const auto style_epoch=changed->identity->style_tree_epoch;
        full_style(mode==3?destination_runtime:source_runtime,changed);
        const auto after=token(changed);
        CHECK(before.observation_serial==after.observation_serial&&before.input_epoch==after.input_epoch&&before.structure_epoch==after.structure_epoch);
        CHECK(changed->identity->style_tree_epoch>style_epoch);
        // No revision/runtime can accidentally keep the old allocation alive.
        aether_style_runtime_destroy(source_runtime);aether_style_runtime_destroy(destination_runtime);
        CHECK(!old.expired());
        const auto host_children=host->children,target_children=target->children;
        const auto source_size=source->editable_nodes.size(),destination_size=destination->editable_nodes.size();
        CHECK(aether_dom_edit_validate_v1(edit)==AETHER_ERROR_INVALID_STATE);
        aether_dom_token_v1_t output{};failure=0;auto status=aether_dom_edit_commit_v1(edit,&output);failure=-1;
        CHECK(status==AETHER_ERROR_INVALID_STATE&&host->parent==old_parent&&host->children==host_children&&target->children==target_children);
        CHECK(source->editable_nodes.size()==source_size&&destination->editable_nodes.size()==destination_size);
        CHECK(token(changed).observation_serial==after.observation_serial);
        aether_dom_edit_destroy_v1(edit);CHECK(old.expired());
        aether_doc_destroy(source);aether_doc_destroy(destination);
    }
}
static void materialize_document_atomic() {
    aether_doc_t source=nullptr,destination=nullptr;
    CHECK(aether_doc_create(&source)==AETHER_OK);CHECK(aether_doc_create(&destination)==AETHER_OK);
    CHECK(aether_doc_parse_html(source,"<body><button id='live'>current R2</button></body>")==AETHER_OK);
    auto* live=aether_doc_get_element_by_id(source,"live");auto* old_parent=live->parent;
    auto identity=live->identity;auto relay=aether_live_owner_pin(live);const auto old_serial=identity->disconnect_serial;
    aether_dom_node_spec_v1_t nodes[3]{};
    nodes[0].type=AETHER_NODE_ELEMENT;nodes[0].tag=str("main");nodes[0].parent_index=-1;
    nodes[1].existing_node=live;nodes[1].parent_index=0;
    nodes[2].type=AETHER_NODE_TEXT;nodes[2].text=str("sibling");nodes[2].parent_index=-1;
    aether_dom_edit_spec_v1_t spec{};spec.kind=AETHER_DOM_MATERIALIZE_DOCUMENT;spec.target=destination;spec.nodes=nodes;spec.node_count=3;
    auto candidate=prepare(spec);aether_node_t new_parent=nullptr;CHECK(aether_dom_edit_created_node_v1(candidate,0,&new_parent)==AETHER_OK);
    aether_dom_edit_destroy_v1(candidate);CHECK(live->parent==old_parent&&destination->children.empty()&&identity->disconnect_serial==old_serial);
    bool success=false;
    for(int n=0;n<200;++n) {
        auto before=token(destination);const auto source_before=token(source);candidate=nullptr;failure=n;
        auto status=aether_dom_prepare_edit_v1(&before,&spec,&candidate);failure=-1;
        if(status==AETHER_OK) {CHECK(aether_dom_edit_created_node_v1(candidate,0,&new_parent)==AETHER_OK);commit(candidate);success=true;break;}
        CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!candidate);CHECK(destination->children.empty()&&live->parent==old_parent);
        CHECK(token(source).observation_serial==source_before.observation_serial&&token(destination).observation_serial==before.observation_serial);
    }
    CHECK(success&&destination->children.size()==2&&destination->children[0]==new_parent);
    CHECK(new_parent->parent==destination&&live->parent==new_parent&&new_parent->children[0]==live);
    CHECK(live->identity==identity&&identity->attached&&identity->disconnect_serial==old_serial+1);
    CHECK(live->document_owner.lock()==destination->owner_control&&relay->current_root==aether_editable_lease(destination));
    CHECK(!source->editable_nodes.contains(live)&&destination->editable_nodes.contains(live));
    CHECK(old_parent->children.empty()&&live->children[0]->text=="current R2");
    CHECK(destination->children[1]->identity->attached&&destination->children[1]->parent==destination);
    // A nonempty/live owner cannot be silently replaced by materialization.
    auto current=token(destination);candidate=nullptr;CHECK(aether_dom_prepare_edit_v1(&current,&spec,&candidate)==AETHER_ERROR_INVALID_STATE&&!candidate);
    aether_doc_destroy(source);aether_doc_destroy(destination);
}
#include "dom_edit_fault_matrix.h"
int main(){
#if defined(AETHER_DOM_FAULT_LINK_UNIT)
    fault_alloc::positive_controls();
#endif
    dom_fault_matrix::run();
    materialize_document_atomic();
    style_replacement_invalidates_edit();
    aether_doc_t d=nullptr;CHECK(aether_doc_create(&d)==AETHER_OK);CHECK(aether_doc_parse_html(d,"<body><input id='a' value='base'><p id='b'></p></body>")==AETHER_OK);auto* a=aether_doc_get_element_by_id(d,"a");auto* b=aether_doc_get_element_by_id(d,"b");auto* body=a->parent;
    aether_dom_edit_spec_v1_t s{};s.target=a;s.kind=AETHER_DOM_SET_RUNTIME_VALUE;s.value=str("");s.value_present=1;edit(s);CHECK(a->runtime_value_present&&a->attributes.at("value").empty());s.kind=AETHER_DOM_SET_ATTRIBUTE;s.name=str("VALUE");s.value=str("new base");edit(s);CHECK(a->authored_value=="new base"&&a->attributes.at("value").empty());s.kind=AETHER_DOM_SET_RUNTIME_VALUE;s.value={};s.value_present=0;edit(s);CHECK(!a->runtime_value_present&&a->attributes.at("value")=="new base");
    s.kind=AETHER_DOM_REMOVE_ATTRIBUTE;s.name=str("value");edit(s);CHECK(!a->authored_value_present&&!a->attributes.contains("value"));s.kind=AETHER_DOM_SET_RUNTIME_VALUE;s.value=str("override");s.value_present=1;edit(s);CHECK(aether_element_set_attribute(a,"value","R2")==AETHER_OK);CHECK(!a->runtime_value_present&&a->authored_value=="R2");
    s.kind=AETHER_DOM_SET_STATE;s.state=9;auto e=prepare(s);aether_element_set_state(a,4);aether_dom_token_v1_t out{};CHECK(aether_dom_edit_commit_v1(e,&out)==AETHER_ERROR_INVALID_STATE&&a->state==4);aether_dom_edit_destroy_v1(e);edit(s);CHECK(a->state==9);
    auto before=token(a);e=prepare(s);commit(e);CHECK(token(a).observation_serial==before.observation_serial);
    aether_dom_attribute_v1_t attrs[]={{str("É"),str("UP")},{str("é"),str("lo")}};
    aether_dom_node_spec_v1_t nodes[4]{};nodes[0].type=AETHER_NODE_ELEMENT;nodes[0].tag=str("BR");nodes[0].parent_index=-1;nodes[0].attributes=attrs;nodes[0].attribute_count=2;
    nodes[1].type=AETHER_NODE_TEXT;nodes[1].parent_index=0;nodes[1].text=str("");nodes[2].type=AETHER_NODE_TEXT;nodes[2].parent_index=0;nodes[2].text=str("raw </style> &");nodes[3].type=AETHER_NODE_COMMENT;nodes[3].parent_index=0;nodes[3].text=str("comment");
    s={};s.kind=AETHER_DOM_INSERT_SUBTREE;s.target=b;s.nodes=nodes;s.node_count=4;s.index=-1;
    bool success=false;for(long n=0;n<200;++n){auto base=token(b);const auto ids=d->next_style_node_id;const auto owned=d->editable_nodes.size();e=nullptr;failure=n;auto status=aether_dom_prepare_edit_v1(&base,&s,&e);failure=-1;if(status==AETHER_OK){aether_node_t fresh=nullptr;CHECK(aether_dom_edit_created_node_v1(e,0,&fresh)==AETHER_OK);commit(e);CHECK(fresh==b->children.front()&&fresh->tag_name=="br"&&fresh->children.size()==3&&fresh->children[0]->text.empty()&&fresh->attributes.size()==2);success=true;break;}CHECK(status==AETHER_ERROR_OUT_OF_MEMORY);CHECK(token(b).observation_serial==base.observation_serial&&d->next_style_node_id==ids&&d->editable_nodes.size()==owned&&b->children.empty());}CHECK(success);
    s={};s.kind=AETHER_DOM_INSERT_CHILD;s.target=b;s.child=a;s.index=0;edit(s);CHECK(a->parent==b&&a->identity->disconnect_serial==0);s.kind=AETHER_DOM_REMOVE_CHILD;edit(s);CHECK(!a->parent&&!a->identity->attached&&a->identity->disconnect_serial==1);
    s.kind=AETHER_DOM_SET_ATTRIBUTE;s.target=a;s.name=str("x");const char bad[]={'a',0,'b'};s.value={bad,3};auto t=token(a);e=nullptr;CHECK(aether_dom_prepare_edit_v1(&t,&s,&e)==AETHER_ERROR_INVALID_STATE&&!e&&a->attributes.count("x")==0);
    s={};s.target=b;s.kind=AETHER_DOM_CLEAR_CHILDREN;edit(s);CHECK(b->children.empty());
    // Materialize a new detached parent around the current native subtree.
    // Existing payload specs are opaque and never replay stale managed values.
    CHECK(aether_node_append_child(body,a)==AETHER_OK);
    CHECK(aether_element_set_attribute(a,"x","native R2")==AETHER_OK);
    aether_dom_node_spec_v1_t forest[2]{};forest[0].type=AETHER_NODE_ELEMENT;forest[0].tag=str("section");forest[0].parent_index=-1;forest[1].parent_index=0;forest[1].existing_node=a;
    s={};s.kind=AETHER_DOM_MATERIALIZE_FOREST;s.target=a;s.nodes=forest;s.node_count=2;
    const auto disconnect=a->identity->disconnect_serial;auto materialized=prepare(s);aether_node_t parent=nullptr,result=nullptr;
    CHECK(aether_dom_edit_created_node_v1(materialized,0,&parent)==AETHER_OK&&parent->unpublished);
    CHECK(aether_dom_edit_created_node_v1(materialized,1,&result)==AETHER_OK&&result==a);
    auto state_before=parent->state;aether_element_set_state(parent,13);CHECK(parent->state==state_before&&d->owner_control->sticky_status==AETHER_OK);
    CHECK(aether_element_set_attribute(parent,"bad","write")==AETHER_ERROR_INVALID_STATE);
    uint64_t new_identity=0;CHECK(aether_dom_read_node_identity_v1(parent,&new_identity)==AETHER_OK&&new_identity);
    commit(materialized);CHECK(!parent->unpublished&&!parent->parent&&!parent->identity->attached&&a->parent==parent&&a->attributes.at("x")=="native R2"&&a->identity->disconnect_serial==disconnect+1);
    forest[1].state=99;s.target=a;auto current=token(a);materialized=nullptr;CHECK(aether_dom_prepare_edit_v1(&current,&s,&materialized)==AETHER_ERROR_INVALID_STATE&&!materialized);forest[1].state=0;
    materialized=prepare(s);CHECK(aether_dom_edit_created_node_v1(materialized,0,&result)==AETHER_OK);aether_dom_node_lease_t provisional=nullptr;CHECK(aether_dom_retain_node_v1(result,&provisional)==AETHER_OK);auto old_parent=a->parent;
    aether_dom_edit_destroy_v1(materialized);CHECK(!result->identity->alive&&a->identity->alive&&a->parent==old_parent);aether_dom_node_lease_destroy_v1(provisional);
    aether_doc_destroy(d);std::puts("typed DOM edits PASS");
}

#endif
