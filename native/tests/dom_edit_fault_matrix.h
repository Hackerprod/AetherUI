#pragma once
#include <climits>
// Included by test_dom_edit.cpp so its existing allocator and CTest target
// exercise each preparation family. Capture is outside the injected interval.
#include <aether/aether_dom_interaction.h>
#include <aether/aether_state_style.h>

namespace dom_fault_matrix {
#if defined(AETHER_DOM_FAULT_LINK_UNIT)
static fault_alloc::Counts last_attempt_counts;
#endif
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
    aether_style_runtime_t runtime=nullptr,destination_runtime=nullptr;
    aether_dom_style_binding_t binding=nullptr,destination_binding=nullptr;
    aether_dom_interaction_owner_t owner=nullptr;
    aether_dom_interaction_claim_t claim=nullptr;
    aether_node_t input=nullptr,other=nullptr,style=nullptr,target=nullptr;
    int callbacks=0;
    static void diagnostic(const char*,const char*,const char*,const char*,void* data){++*static_cast<int*>(data);}
    explicit Fixture(int mode=0){
        CHECK(aether_doc_create(&doc)==AETHER_OK&&aether_doc_create(&destination)==AETHER_OK&&aether_doc_create(&empty)==AETHER_OK);
        CHECK(aether_doc_parse_html(doc,"<body><style id='sheet'>#x{opacity:.2}</style><input id='x' value='base' onblur='old route'><div id='other'><span>child</span></div></body>")==AETHER_OK);
        CHECK(aether_doc_parse_html(destination,mode==17||mode==18?"<body><style>section{opacity:.6}</style><section id='target'><input id='x'></section></body>":"<body><section id='target'></section></body>")==AETHER_OK);
        input=aether_doc_get_element_by_id(doc,"x");other=aether_doc_get_element_by_id(doc,"other");style=aether_doc_get_element_by_id(doc,"sheet");target=aether_doc_get_element_by_id(destination,"target");
        CHECK(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&runtime)==AETHER_OK);
        aether_style_runtime_set_diagnostics_callback(runtime,diagnostic,&callbacks);
        CHECK(aether_dom_style_binding_create_v1(doc,runtime,&binding)==AETHER_OK);
        if(mode==17||mode==18){
            destination_runtime=runtime;
            if(mode==18){CHECK(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&destination_runtime)==AETHER_OK);aether_style_runtime_set_diagnostics_callback(destination_runtime,diagnostic,&callbacks);}
            int64_t foreign=0;CHECK(aether_style_runtime_add_stylesheet(runtime,"section{width:37px}",2,"foreign",&foreign,nullptr)==AETHER_OK);
            CHECK(aether_dom_style_binding_create_v1(destination,destination_runtime,&destination_binding)==AETHER_OK);
            CHECK(aether_style_runtime_add_stylesheet(destination_runtime,"section{height:23px}",2,"foreign-destination",&foreign,nullptr)==AETHER_OK);
        }
        CHECK(aether_dom_interaction_owner_create_v1(doc,binding,1,&owner)==AETHER_OK);
        aether_dom_interaction_edit_t e=nullptr;auto t=token(input);aether_dom_token_v1_t out{};
        CHECK(aether_dom_interaction_prepare_acquire_v1(owner,&t,input,AETHER_STATE_FOCUS,11,&e)==AETHER_OK);
        CHECK(aether_dom_interaction_edit_commit_v1(e,&claim,&out)==AETHER_OK);aether_dom_interaction_edit_release_v1(e);
    }
    ~Fixture(){aether_dom_interaction_owner_dispose_v1(owner);aether_dom_interaction_claim_release_v1(claim);aether_dom_interaction_owner_release_v1(owner);aether_dom_style_binding_destroy_v1(binding);aether_dom_style_binding_destroy_v1(destination_binding);if(destination_runtime&&destination_runtime!=runtime)aether_style_runtime_destroy(destination_runtime);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);aether_doc_destroy(destination);aether_doc_destroy(empty);}
};
static aether_dom_edit_spec_v1_t configure(Fixture& f,int mode,aether_dom_node_spec_v1_t (&nodes)[2]){
    aether_dom_edit_spec_v1_t s{};s.target=f.input;s.index=-1;
        nodes[0].type=AETHER_NODE_ELEMENT;nodes[0].tag=str("section");nodes[0].parent_index=-1;
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
        case 16:s.kind=AETHER_DOM_SET_RUNTIME_VALUE;s.value_present=0;break;
        case 17:case 18:s.kind=AETHER_DOM_INSERT_CHILD;s.target=f.target;s.child=f.style;break;
        case 19:case 20:
            {auto parent=f.input->parent;CHECK(aether_node_remove_child(parent,f.input)==AETHER_OK);
            if(mode==20){CHECK(aether_node_append_child(parent,f.input)==AETHER_OK);s.kind=AETHER_DOM_INSERT_CHILD;s.target=f.target;s.child=f.input;}
            else{s.kind=AETHER_DOM_SET_ATTRIBUTE;s.name=str("onblur");s.value=str("new handler must not replace a previously frozen loss route");}}
            break;
        default:CHECK(false);

        }
        if(mode==13||mode==15||mode==16){aether_dom_edit_spec_v1_t initial{};initial.target=f.input;initial.kind=AETHER_DOM_SET_RUNTIME_VALUE;initial.value_present=1;initial.value=str("existing override");edit(initial);}
    return s;
}
// Typed values, not HTML serialization. Raw identities are independently
// verified by DocumentImage; cross-fixture equality uses per-document local IDs.
struct ValueImage {
    int32_t type,state,alive,attached,authored_present,runtime_present;
    int64_t parent;
    uint64_t disconnect;
    std::string tag,text,authored,runtime;
    std::map<std::string,std::string> attributes;
    std::vector<int64_t> children;
    bool operator==(const ValueImage&) const = default;
};
static std::string copied(aether_dom_string_v1_t value){CHECK(value.data);return {value.data,value.length};}
static std::map<int64_t,ValueImage> values(aether_doc_t doc){
    aether_dom_snapshot_t snapshot=nullptr;CHECK(aether_dom_acquire_snapshot_v1(doc,nullptr,0,&snapshot)==AETHER_OK);
    size_t count=0;CHECK(aether_dom_snapshot_count_v1(snapshot,&count)==AETHER_OK);
    std::vector<aether_dom_snapshot_node_v1_t> nodes(count);
    for(size_t i=0;i<count;++i)CHECK(aether_dom_snapshot_node_v1(snapshot,i,&nodes[i])==AETHER_OK);
    std::map<int64_t,ValueImage> result;
    for(size_t i=0;i<count;++i){const auto& n=nodes[i];ValueImage image{n.type,n.state,n.alive,n.attached,n.authored_value_present,n.runtime_value_present,
        n.parent_index<0?-1:nodes[size_t(n.parent_index)].style_node_id,n.disconnect_serial,copied(n.tag),copied(n.text),
        n.authored_value_present?copied(n.authored_value):"",n.runtime_value_present?copied(n.runtime_value):"",{}, {}};
        for(size_t a=0;a<n.attribute_count;++a){aether_dom_attribute_v1_t attribute{};CHECK(aether_dom_snapshot_attribute_v1(snapshot,i,a,&attribute)==AETHER_OK);image.attributes.emplace(copied(attribute.name),copied(attribute.value));}
        for(size_t c=0;c<n.child_count;++c){size_t child=0;CHECK(aether_dom_snapshot_child_v1(snapshot,i,c,&child)==AETHER_OK);image.children.push_back(nodes[child].style_node_id);}
        CHECK(result.emplace(n.style_node_id,std::move(image)).second);
    }
    aether_dom_snapshot_destroy_v1(snapshot);return result;
}
using BoxImage=std::tuple<int64_t,int32_t,float,float,float,float,int32_t>;
static void boxes(aether_layout_box_t box,std::vector<BoxImage>& output){
    if(!box)return;auto node=aether_layout_box_get_element(box);const auto count=aether_layout_box_get_child_count(box);
    output.emplace_back(node?node->style_node_id:-1,aether_layout_box_get_kind(box),aether_layout_box_get_x(box),aether_layout_box_get_y(box),aether_layout_box_get_width(box),aether_layout_box_get_height(box),count);
    for(int32_t i=0;i<count;++i)boxes(aether_layout_box_get_child(box,i),output);
}
struct FrameImage {
    std::map<int64_t,std::tuple<float,int32_t,int32_t,float,int32_t,float>> styles;
    std::vector<BoxImage> layout;
    bool operator==(const FrameImage&) const = default;
};
static FrameImage full_frame(aether_style_runtime_t runtime,aether_doc_t doc){
    aether_style_transaction_t transaction=nullptr;aether_style_revision_t revision=nullptr;
    const aether_style_environment_t environment{400,300,1,16,0,0,0};
    CHECK(aether_style_runtime_prepare_revision(runtime,doc,&environment,&transaction)==AETHER_OK);
    CHECK(aether_style_runtime_commit_revision(runtime,transaction,&revision)==AETHER_OK);
    FrameImage result;
    for(int32_t i=0;i<aether_style_revision_style_count(revision);++i){aether_node_t node=nullptr;aether_computed_style_handle_t handle=nullptr;
        CHECK(aether_style_revision_get_style(revision,i,&node,&handle,nullptr)==AETHER_OK);aether_computed_style_t style{};
        CHECK(aether_computed_style_copy(handle,&style)==AETHER_OK);result.styles.emplace(node->style_node_id,std::tuple{style.opacity,int32_t(style.display),int32_t(style.width.unit),style.width.value,int32_t(style.height.unit),style.height.value});
        aether_computed_style_free(&style);aether_computed_style_handle_destroy(handle);
    }
    aether_style_map_t map=nullptr;CHECK(aether_style_revision_create_map(revision,&map)==AETHER_OK);
    aether_layout_engine_t engine=nullptr;aether_layout_result_t layout=nullptr;CHECK(aether_layout_create(nullptr,nullptr,&engine)==AETHER_OK);
    CHECK(aether_layout_layout(engine,doc,map,400,300,nullptr,nullptr,&layout)==AETHER_OK);boxes(aether_layout_result_get_root(layout),result.layout);
    aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_transaction_destroy(transaction);return result;
}
using Lifetimes=std::vector<std::weak_ptr<EditableNodeStorage>>;
static void remember(const Fixture& f,Lifetimes& lifetimes){
    for(auto doc:{f.doc,f.destination,f.empty}){lifetimes.push_back(doc->editable_storage);for(const auto& entry:doc->editable_nodes)lifetimes.push_back(entry.second);}
}
static bool prepare_attempt(int mode,long point,Lifetimes& lifetimes){
        Fixture f(mode);aether_dom_node_spec_v1_t nodes[2]{};auto s=configure(f,mode,nodes);
        full_style(f.runtime,f.doc);
        remember(f,lifetimes);
        const DocumentImage before(f.doc),destination(f.destination),empty(f.empty);
        int64_t slot_handle=0;int32_t slot_present=0;uint64_t slot_epoch=0;
        CHECK(aether_dom_style_binding_read_slot_v1(f.binding,&slot_handle,&slot_present,&slot_epoch)==AETHER_OK);
        int64_t destination_handle=0;int32_t destination_present=0;uint64_t destination_epoch=0;
        if(f.destination_binding)CHECK(aether_dom_style_binding_read_slot_v1(f.destination_binding,&destination_handle,&destination_present,&destination_epoch)==AETHER_OK);
        int32_t pending=0;aether_dom_interaction_loss_v1_t prior_loss{};aether_dom_interaction_claim_info_v1_t prior_claim{};
        CHECK(aether_dom_interaction_claim_read_loss_v1(f.claim,&pending,&prior_loss)==AETHER_OK);
        CHECK(aether_dom_interaction_claim_read_v1(f.claim,&prior_claim)==AETHER_OK);
        const std::string prior_handler=pending&&prior_loss.handler_present?copied(prior_loss.handler):"";
        aether_style_publication_token_t publication{};publication.struct_size=sizeof(publication);
        CHECK(aether_style_runtime_read_publication_token(f.runtime,&publication)==AETHER_OK&&publication.inputs_current);
        auto unchanged=[&]{before.unchanged();destination.unchanged();empty.unchanged();
            int64_t handle=0;int32_t present_slot=0;uint64_t epoch=0;
            CHECK(aether_dom_style_binding_read_slot_v1(f.binding,&handle,&present_slot,&epoch)==AETHER_OK);
            CHECK(handle==slot_handle&&present_slot==slot_present&&epoch==slot_epoch&&f.callbacks==0);
            aether_style_publication_token_t current{};current.struct_size=sizeof(current);
            CHECK(aether_style_runtime_read_publication_token(f.runtime,&current)==AETHER_OK&&current.inputs_current);
            CHECK(current.revision_identity==publication.revision_identity&&current.generation==publication.generation&&current.sheet_epoch==publication.sheet_epoch&&current.registry_identity==publication.registry_identity);
            if(f.destination_binding){CHECK(aether_dom_style_binding_read_slot_v1(f.destination_binding,&handle,&present_slot,&epoch)==AETHER_OK);CHECK(handle==destination_handle&&present_slot==destination_present&&epoch==destination_epoch);}
            int32_t present=0;aether_dom_interaction_loss_v1_t loss{};CHECK(aether_dom_interaction_claim_read_loss_v1(f.claim,&present,&loss)==AETHER_OK&&present==pending);
            aether_dom_interaction_claim_info_v1_t info{};CHECK(aether_dom_interaction_claim_read_v1(f.claim,&info)==AETHER_OK);
            CHECK(info.current==prior_claim.current&&info.generation==prior_claim.generation&&info.registry_identity==prior_claim.registry_identity&&info.state_bit_write_serial==prior_claim.state_bit_write_serial);
            if(pending){CHECK(loss.record_id==prior_loss.record_id&&loss.record_serial==prior_loss.record_serial&&loss.disconnect_serial==prior_loss.disconnect_serial&&loss.state_bit_write_serial==prior_loss.state_bit_write_serial&&loss.reason==prior_loss.reason&&loss.handler_present==prior_loss.handler_present&&loss.claim.generation==prior_loss.claim.generation&&loss.claim.registry_identity==prior_loss.claim.registry_identity);
                CHECK(!loss.handler_present||copied(loss.handler)==prior_handler);}
        };
        auto t=token(s.target);aether_dom_edit_t candidate=nullptr;failure=point;
        const auto status=aether_dom_prepare_edit_v1(&t,&s,&candidate);failure=-1;
#if defined(AETHER_DOM_FAULT_LINK_UNIT)
        last_attempt_counts=fault_alloc::counts;
#endif
        if(status!=AETHER_OK){CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!candidate);unchanged();return false;}
        CHECK(candidate);unchanged();aether_dom_edit_destroy_v1(candidate);unchanged();commit(prepare(s));
        Fixture twin(mode);aether_dom_node_spec_v1_t twin_nodes[2]{};auto reference=configure(twin,mode,twin_nodes);
        full_style(twin.runtime,twin.doc);commit(prepare(reference));
        CHECK(values(f.doc)==values(twin.doc)&&values(f.destination)==values(twin.destination)&&values(f.empty)==values(twin.empty));
        CHECK(full_frame(f.runtime,f.doc)==full_frame(twin.runtime,twin.doc));
        CHECK(full_frame(f.destination_runtime?f.destination_runtime:f.runtime,f.destination)==full_frame(twin.destination_runtime?twin.destination_runtime:twin.runtime,twin.destination));
        CHECK(full_frame(f.runtime,f.empty)==full_frame(twin.runtime,twin.empty));
        CHECK(f.callbacks==twin.callbacks);
        remember(f,lifetimes);remember(twin,lifetimes);
        return true;
}
static void all_prepare_families(){
#if defined(AETHER_DOM_FAULT_LINK_UNIT)
    for(int mode=0;mode<21;++mode){Lifetimes initial;CHECK(prepare_attempt(mode,LONG_MAX,initial));
        const auto count=last_attempt_counts.requests;CHECK(count>0&&!last_attempt_counts.injected_failures);
        for(const auto& weak:initial)CHECK(weak.expired());uint64_t failures=0,recovered=0;
        for(uint64_t point=0;point<count;++point){Lifetimes lifetimes;const bool completed=prepare_attempt(mode,static_cast<long>(point),lifetimes);
            CHECK(last_attempt_counts.injected_failures==1&&last_attempt_counts.failed_ordinal==point);
            for(const auto& weak:lifetimes)CHECK(weak.expired());if(completed)++recovered;else ++failures;
        }
        CHECK(failures+recovered==count);std::printf("DOM link-unit fault family %d: requests=%llu rejected=%llu recovered=%llu; typed/FULL/layout/leases verified\n",mode,
            (unsigned long long)count,(unsigned long long)failures,(unsigned long long)recovered);
    }
#else
    for(int mode=0;mode<21;++mode){bool success=false;long failures=0;
        for(long point=0;point<3000;++point){Lifetimes lifetimes;const auto completed=prepare_attempt(mode,point,lifetimes);
            // No oracle or fixture pins remain here. Every initial/final known
            // editable storage must have died after its final owner is released.
            for(const auto& weak:lifetimes)CHECK(weak.expired());
            if(completed){success=true;break;}++failures;
        }
        CHECK(success&&failures>0);
        std::printf("typed cold prepare sweep family %d: %ld failures; no-write, last leases, typed values and next FULL/layout match clean twin\n",mode,failures);
    }
#endif
}
static std::vector<ValueImage> snapshot_records(aether_dom_snapshot_t snapshot){
    size_t count=0;CHECK(aether_dom_snapshot_count_v1(snapshot,&count)==AETHER_OK);
    std::vector<ValueImage> records;
    for(size_t i=0;i<count;++i){aether_dom_snapshot_node_v1_t n{};CHECK(aether_dom_snapshot_node_v1(snapshot,i,&n)==AETHER_OK);
        ValueImage value{n.type,n.state,n.alive,n.attached,n.authored_value_present,n.runtime_value_present,n.parent_index,n.disconnect_serial,
            copied(n.tag),copied(n.text),n.authored_value_present?copied(n.authored_value):"",n.runtime_value_present?copied(n.runtime_value):"",{}, {}};
        for(size_t a=0;a<n.attribute_count;++a){aether_dom_attribute_v1_t attribute{};CHECK(aether_dom_snapshot_attribute_v1(snapshot,i,a,&attribute)==AETHER_OK);value.attributes.emplace(copied(attribute.name),copied(attribute.value));}
        for(size_t c=0;c<n.child_count;++c){size_t child=0;CHECK(aether_dom_snapshot_child_v1(snapshot,i,c,&child)==AETHER_OK);value.children.push_back(int64_t(child));}
        records.push_back(std::move(value));
    }
    return records;
}
static bool snapshot_attempt(int mode,long point,Lifetimes& lifetimes,aether_dom_snapshot_t& retained){
    Fixture f;aether_dom_node_lease_t lease=nullptr;CHECK(aether_dom_retain_node_v1(f.input,&lease)==AETHER_OK);
    CHECK(aether_element_set_attribute(f.input,"payload","strict UTF8 é and an explicitly empty attribute")==AETHER_OK);
    CHECK(aether_element_set_attribute(f.input,"empty","")==AETHER_OK);
    aether_dom_edit_spec_v1_t override{};override.kind=AETHER_DOM_SET_RUNTIME_VALUE;override.target=f.input;override.value_present=1;override.value=str("");edit(override);
    CHECK(aether_node_remove_child(f.input->parent,f.input)==AETHER_OK);
    if(mode==1)CHECK(aether_doc_parse_html(f.doc,"<body><p>new source after retained retirement</p></body>")==AETHER_OK);
    if(mode==2)CHECK(aether_node_append_child(f.target,f.input)==AETHER_OK);
    remember(f,lifetimes);lifetimes.push_back(f.input->editable_storage);
    const DocumentImage before(f.doc),destination(f.destination);const NodeImage known(f.input);
    aether_dom_snapshot_t snapshot=nullptr;failure=point;
    const auto status=aether_dom_acquire_snapshot_v1(f.doc,&lease,1,&snapshot);failure=-1;
#if defined(AETHER_DOM_FAULT_LINK_UNIT)
    last_attempt_counts=fault_alloc::counts;
#endif
    before.unchanged();destination.unchanged();known.unchanged();
    if(status!=AETHER_OK){CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!snapshot);aether_dom_node_lease_destroy_v1(lease);return false;}
    CHECK(snapshot);size_t count=0;CHECK(aether_dom_snapshot_count_v1(snapshot,&count)==AETHER_OK);
    CHECK(count==f.doc->editable_nodes.size()+1+(f.doc->editable_nodes.contains(f.input)?0:1));
    const auto records=snapshot_records(snapshot);std::map<aether_node_t,size_t> positions;
    for(size_t i=0;i<count;++i){aether_dom_snapshot_node_v1_t n{};CHECK(aether_dom_snapshot_node_v1(snapshot,i,&n)==AETHER_OK);positions.emplace(n.node,i);}
    CHECK(positions.contains(f.input));
    for(size_t i=0;i<count;++i){aether_dom_snapshot_node_v1_t n{};CHECK(aether_dom_snapshot_node_v1(snapshot,i,&n)==AETHER_OK);auto* node=n.node;const auto& value=records[i];
        CHECK(n.identity==aether_node_identity(node)&&n.style_node_id==node->style_node_id);
        const auto owner=node->document_owner.lock();const auto root=owner?owner->document.lock():nullptr;
        CHECK(n.owner_identity==(root?aether_node_identity(root->node.get()):0));
        CHECK(value.type==node->type&&value.state==node->state&&bool(value.alive)==node->identity->alive&&bool(value.attached)==node->identity->attached);
        CHECK(value.disconnect==node->identity->disconnect_serial&&value.tag==node->tag_name&&value.text==node->text);
        CHECK(bool(value.authored_present)==node->authored_value_present&&bool(value.runtime_present)==node->runtime_value_present);
        CHECK(!value.authored_present||value.authored==node->authored_value);CHECK(!value.runtime_present||value.runtime==node->runtime_value);
        CHECK(value.parent==(positions.contains(node->parent)?int64_t(positions.at(node->parent)):-1));
        std::map<std::string,std::string> attrs(node->attributes.begin(),node->attributes.end());
        if(node->authored_value_present)attrs["value"]=node->authored_value;else attrs.erase("value");
        CHECK(value.attributes==attrs);std::vector<int64_t> children;for(auto* child:node->children)if(positions.contains(child))children.push_back(int64_t(positions.at(child)));
        CHECK(value.children==children);for(size_t bit=0;bit<32;++bit)CHECK(n.state_bit_write_serial[bit]==node->identity->state_bit_write_serial[bit]);
    }
    if(f.input->identity->alive)CHECK(aether_element_set_attribute(f.input,"value","later live mutation")==AETHER_OK);
    CHECK(snapshot_records(snapshot)==records); // Copied payload stays immutable.
    aether_dom_node_lease_destroy_v1(lease);retained=snapshot;return true;
}
static void snapshot_allocations(){
#if defined(AETHER_DOM_FAULT_LINK_UNIT)
    for(int mode=0;mode<3;++mode){Lifetimes initial;aether_dom_snapshot_t calibration=nullptr;
        CHECK(snapshot_attempt(mode,LONG_MAX,initial,calibration));const auto count=last_attempt_counts.requests;
        CHECK(count>0&&!last_attempt_counts.injected_failures);aether_dom_snapshot_destroy_v1(calibration);
        for(const auto& weak:initial)CHECK(weak.expired());uint64_t failures=0,recovered=0;
        for(uint64_t point=0;point<count;++point){Lifetimes lifetimes;aether_dom_snapshot_t snapshot=nullptr;
            const bool complete=snapshot_attempt(mode,static_cast<long>(point),lifetimes,snapshot);
            CHECK(last_attempt_counts.injected_failures==1&&last_attempt_counts.failed_ordinal==point);
            if(complete){CHECK(snapshot&&!lifetimes.front().expired());(void)snapshot_records(snapshot);aether_dom_snapshot_destroy_v1(snapshot);++recovered;}else ++failures;
            for(const auto& weak:lifetimes)CHECK(weak.expired());
        }
        CHECK(failures+recovered==count);std::printf("snapshot link-unit fault family %d: requests=%llu rejected=%llu recovered=%llu; exact data/last leases verified\n",mode,
            (unsigned long long)count,(unsigned long long)failures,(unsigned long long)recovered);
    }
#else
    for(int mode=0;mode<3;++mode){bool succeeded=false;long failures=0;
        for(long point=0;point<1000;++point){Lifetimes lifetimes;aether_dom_snapshot_t snapshot=nullptr;
            const bool complete=snapshot_attempt(mode,point,lifetimes,snapshot);
            if(complete){CHECK(snapshot&&!lifetimes.front().expired());(void)snapshot_records(snapshot);aether_dom_snapshot_destroy_v1(snapshot);}
            for(const auto& weak:lifetimes)CHECK(weak.expired());
            if(complete){succeeded=true;break;}++failures;
        }
        CHECK(succeeded&&failures>0);std::printf("snapshot cold sweep %d: %ld failures; typed payload, known lease and final release verified\n",mode,failures);
    }
#endif
}
static void run(){all_prepare_families();snapshot_allocations();}
}
