#pragma once
// Included by test_dom_edit.cpp so its existing allocator and CTest target
// exercise each preparation family. Capture is outside the injected interval.
#include <aether/aether_dom_interaction.h>
#include <aether/aether_state_style.h>

namespace dom_fault_matrix {
struct NodeImage {
    aether_node_s* node;
    std::shared_ptr<node_identity_s> identity;
    std::shared_ptr<DomOwnerControl> owner;
    aether_node_s* parent;
    std::vector<aether_node_s*> children;
    std::map<std::string,std::string,std::less<>> attributes;
    std::string tag,text,authored,runtime;
    std::array<uint64_t,32> stamps;
    uint64_t disconnect,mutation,structure,style;
    int64_t id;
    int32_t state;
    bool alive,attached,authored_present,runtime_present,unpublished;
    explicit NodeImage(aether_node_s* n):node(n),identity(n->identity),owner(n->document_owner.lock()),
        parent(n->parent),children(n->children),attributes(n->attributes),tag(n->tag_name),text(n->text),
        authored(n->authored_value),runtime(n->runtime_value),stamps(n->identity->state_bit_write_serial),
        disconnect(n->identity->disconnect_serial),mutation(n->identity->mutation_epoch),
        structure(n->identity->structure_epoch),style(n->identity->style_tree_epoch),id(n->style_node_id),
        state(n->state),alive(n->identity->alive),attached(n->identity->attached),
        authored_present(n->authored_value_present),runtime_present(n->runtime_value_present),unpublished(n->unpublished) {}
    void unchanged() const {
        CHECK(node->identity==identity&&node->document_owner.lock()==owner&&identity->document_owner.lock()==owner);
        CHECK(node->parent==parent&&node->children==children&&node->attributes==attributes);
        CHECK(node->tag_name==tag&&node->text==text&&node->authored_value==authored&&node->runtime_value==runtime);
        CHECK(identity->state_bit_write_serial==stamps&&identity->disconnect_serial==disconnect);
        CHECK(identity->mutation_epoch==mutation&&identity->structure_epoch==structure&&identity->style_tree_epoch==style);
        CHECK(node->style_node_id==id&&node->state==state&&identity->alive==alive&&identity->attached==attached);
        CHECK(node->authored_value_present==authored_present&&node->runtime_value_present==runtime_present&&node->unpublished==unpublished);
    }
};
struct DocumentImage {
    aether_doc_t doc;
    decltype(aether_doc_s::editable_nodes) registry;
    decltype(aether_doc_s::id_index) ids;
    std::vector<NodeImage> nodes;
    aether::dom::MutationJournal journal;
    uint64_t observation,interaction,next_generation;
    int64_t next_id,version;
    aether_status_t sticky;
    explicit DocumentImage(aether_doc_t d):doc(d),registry(d->editable_nodes),ids(d->id_index),journal(d->mutation_journal),
        observation(d->owner_control->observation_serial),interaction(d->owner_control->interaction_serial),
        next_generation(d->owner_control->next_interaction_generation),next_id(d->next_style_node_id),version(d->version),sticky(d->owner_control->sticky_status) {
        nodes.emplace_back(d);for(const auto& entry:registry)nodes.emplace_back(entry.first);
    }
    void unchanged() const {
        CHECK(doc->editable_nodes==registry&&doc->id_index==ids&&doc->next_style_node_id==next_id&&doc->version==version);
        CHECK(doc->owner_control->observation_serial==observation&&doc->owner_control->interaction_serial==interaction&&
            doc->owner_control->next_interaction_generation==next_generation&&doc->owner_control->sticky_status==sticky);
        for(const auto& n:nodes)n.unchanged();
        const auto& current=doc->mutation_journal;
        CHECK(current.size()==journal.size()&&current.last_epoch()==journal.last_epoch()&&current.floor_epoch()==journal.floor_epoch());
        CHECK(current.loss()==journal.loss()&&current.monotonicity_lost()==journal.monotonicity_lost());
        for(size_t i=0;i<journal.size();++i){const auto& a=current.begin()[i];const auto& b=journal.begin()[i];
            CHECK(a.subject==b.subject&&a.kind==b.kind&&a.mutation_epoch==b.mutation_epoch&&a.structure_epoch==b.structure_epoch&&a.old_state==b.old_state&&a.new_state==b.new_state);}
    }
};
struct Fixture {
    aether_doc_t doc=nullptr,destination=nullptr,empty=nullptr;
    aether_style_runtime_t runtime=nullptr;
    aether_dom_style_binding_t binding=nullptr;
    aether_dom_interaction_owner_t owner=nullptr;
    aether_dom_interaction_claim_t claim=nullptr;
    aether_node_t input=nullptr,other=nullptr,style=nullptr,target=nullptr;
    int callbacks=0;
    static void diagnostic(const char*,const char*,const char*,const char*,void* data){++*static_cast<int*>(data);}
    Fixture(){
        CHECK(aether_doc_create(&doc)==AETHER_OK&&aether_doc_create(&destination)==AETHER_OK&&aether_doc_create(&empty)==AETHER_OK);
        CHECK(aether_doc_parse_html(doc,"<body><style id='sheet'>#x{opacity:.2}</style><input id='x' value='base' onblur='old route'><div id='other'><span>child</span></div></body>")==AETHER_OK);
        CHECK(aether_doc_parse_html(destination,"<body><section id='target'></section></body>")==AETHER_OK);
        input=aether_doc_get_element_by_id(doc,"x");other=aether_doc_get_element_by_id(doc,"other");style=aether_doc_get_element_by_id(doc,"sheet");target=aether_doc_get_element_by_id(destination,"target");
        CHECK(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&runtime)==AETHER_OK);
        aether_style_runtime_set_diagnostics_callback(runtime,diagnostic,&callbacks);
        CHECK(aether_dom_style_binding_create_v1(doc,runtime,&binding)==AETHER_OK);
        CHECK(aether_dom_interaction_owner_create_v1(doc,binding,1,&owner)==AETHER_OK);
        aether_dom_interaction_edit_t e=nullptr;auto t=token(input);aether_dom_token_v1_t out{};
        CHECK(aether_dom_interaction_prepare_acquire_v1(owner,&t,input,AETHER_STATE_FOCUS,11,&e)==AETHER_OK);
        CHECK(aether_dom_interaction_edit_commit_v1(e,&claim,&out)==AETHER_OK);aether_dom_interaction_edit_release_v1(e);
    }
    ~Fixture(){aether_dom_interaction_owner_dispose_v1(owner);aether_dom_interaction_claim_release_v1(claim);aether_dom_interaction_owner_release_v1(owner);aether_dom_style_binding_destroy_v1(binding);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);aether_doc_destroy(destination);aether_doc_destroy(empty);}
};
static void all_prepare_families(){
    // A fresh fixture per family keeps its successful commit cold with respect
    // to that input, while each failed allocation index leaves the same base.
    for(int mode=0;mode<17;++mode){
        Fixture f;aether_dom_edit_spec_v1_t s{};s.target=f.input;s.index=-1;
        aether_dom_node_spec_v1_t nodes[2]{};nodes[0].type=AETHER_NODE_ELEMENT;nodes[0].tag=str("section");nodes[0].parent_index=-1;
        nodes[1].type=AETHER_NODE_TEXT;nodes[1].text=str("text requiring dynamic allocation beyond small string capacity");nodes[1].parent_index=0;
        switch(mode){
        case 0:s.kind=AETHER_DOM_SET_STATE;s.state=AETHER_STATE_FOCUS|AETHER_STATE_HOVER;break;
        case 1:s.kind=AETHER_DOM_SET_ATTRIBUTE;s.name=str("a-long-attribute-name");s.value=str("new long attribute payload requires a reservation");break;
        case 2:s.kind=AETHER_DOM_REMOVE_ATTRIBUTE;s.name=str("onblur");break;
        case 3:s.kind=AETHER_DOM_SET_RUNTIME_VALUE;s.value_present=1;s.value=str("runtime payload requiring dynamic allocation");break;
        case 4:s.kind=AETHER_DOM_SET_TEXT;s.target=f.style->children.front();s.value=str("#x{opacity:.7;width:calc(1px + 1deg)}");break;
        case 5:s.kind=AETHER_DOM_INSERT_CHILD;s.target=f.other;s.child=f.input;break;
        case 6:s.kind=AETHER_DOM_INSERT_CHILD;s.target=f.target;s.child=f.input;break;
        case 7:s.kind=AETHER_DOM_REMOVE_CHILD;s.target=f.input->parent;s.child=f.input;break;
        case 8:s.kind=AETHER_DOM_CLEAR_CHILDREN;s.target=f.input->parent;break;
        case 9:case 10:s.kind=mode==9?AETHER_DOM_INSERT_SUBTREE:AETHER_DOM_REPLACE_CHILDREN;s.target=f.other;s.nodes=nodes;s.node_count=2;break;
        case 11:case 12:nodes[1]={};nodes[1].existing_node=f.input;nodes[1].parent_index=0;s.kind=mode==11?AETHER_DOM_MATERIALIZE_FOREST:AETHER_DOM_MATERIALIZE_DOCUMENT;s.target=mode==11?f.input:f.empty;s.nodes=nodes;s.node_count=2;break;
        case 13:s.kind=AETHER_DOM_SET_LEGACY_ATTRIBUTE;s.name=str("value");s.value=str("native R2 resets authored and runtime sidecars");break;
        case 14:s.kind=AETHER_DOM_SET_ATTRIBUTE;s.name=str("onblur");s.value=str("new route requiring allocation before disconnect");break;
        case 15:s.kind=AETHER_DOM_REMOVE_ATTRIBUTE;s.name=str("value");break;
        default:s.kind=AETHER_DOM_SET_RUNTIME_VALUE;s.value_present=0;break;
        }
        if(mode==13||mode==15||mode==16){aether_dom_edit_spec_v1_t initial{};initial.target=f.input;initial.kind=AETHER_DOM_SET_RUNTIME_VALUE;initial.value_present=1;initial.value=str("existing override");edit(initial);}
        full_style(f.runtime,f.doc);
        const DocumentImage before(f.doc),destination(f.destination),empty(f.empty);
        int64_t slot_handle=0;int32_t slot_present=0;uint64_t slot_epoch=0;
        CHECK(aether_dom_style_binding_read_slot_v1(f.binding,&slot_handle,&slot_present,&slot_epoch)==AETHER_OK);
        aether_style_publication_token_t publication{};publication.struct_size=sizeof(publication);
        CHECK(aether_style_runtime_read_publication_token(f.runtime,&publication)==AETHER_OK&&publication.inputs_current);
        auto unchanged=[&]{before.unchanged();destination.unchanged();empty.unchanged();
            int64_t handle=0;int32_t present_slot=0;uint64_t epoch=0;
            CHECK(aether_dom_style_binding_read_slot_v1(f.binding,&handle,&present_slot,&epoch)==AETHER_OK);
            CHECK(handle==slot_handle&&present_slot==slot_present&&epoch==slot_epoch&&f.callbacks==0);
            aether_style_publication_token_t current{};current.struct_size=sizeof(current);
            CHECK(aether_style_runtime_read_publication_token(f.runtime,&current)==AETHER_OK&&current.inputs_current);
            CHECK(current.revision_identity==publication.revision_identity&&current.generation==publication.generation&&current.sheet_epoch==publication.sheet_epoch&&current.registry_identity==publication.registry_identity);
            int32_t present=0;aether_dom_interaction_loss_v1_t loss{};CHECK(aether_dom_interaction_claim_read_loss_v1(f.claim,&present,&loss)==AETHER_OK&&!present);aether_dom_interaction_claim_info_v1_t info{};CHECK(aether_dom_interaction_claim_read_v1(f.claim,&info)==AETHER_OK&&info.current);};
        bool succeeded=false;long failures=0;
        for(long point=0;point<3000;++point){auto t=token(s.target);aether_dom_edit_t candidate=nullptr;failure=point;
            const auto status=aether_dom_prepare_edit_v1(&t,&s,&candidate);failure=-1;
            if(status==AETHER_OK){CHECK(candidate);unchanged();aether_dom_edit_destroy_v1(candidate);unchanged();commit(prepare(s));succeeded=true;break;}
            CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!candidate);++failures;unchanged();
        }
        CHECK(succeeded&&failures>0);std::printf("typed prepare allocation sweep family %d: %ld injected failures, abort intact\n",mode,failures);
    }
}
static void snapshot_allocations(){
    Fixture f;CHECK(aether_node_remove_child(f.input->parent,f.input)==AETHER_OK);
    aether_dom_node_lease_t lease=nullptr;CHECK(aether_dom_retain_node_v1(f.input,&lease)==AETHER_OK);
    const DocumentImage before(f.doc);bool succeeded=false;long failures=0;
    for(long point=0;point<1000;++point){aether_dom_snapshot_t snapshot=nullptr;failure=point;
        const auto status=aether_dom_acquire_snapshot_v1(f.doc,&lease,1,&snapshot);failure=-1;
        before.unchanged();
        if(status==AETHER_OK){CHECK(snapshot);size_t count=0;CHECK(aether_dom_snapshot_count_v1(snapshot,&count)==AETHER_OK&&count==f.doc->editable_nodes.size()+1);aether_dom_snapshot_destroy_v1(snapshot);succeeded=true;break;}
        CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!snapshot);++failures;
    }
    CHECK(succeeded&&failures>0);aether_dom_node_lease_destroy_v1(lease);std::printf("snapshot allocation sweep: %ld injected failures, source intact\n",failures);
}
static void run(){all_prepare_families();snapshot_allocations();}
}
