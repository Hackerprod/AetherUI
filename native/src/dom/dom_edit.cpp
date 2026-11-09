#include "dom_types.h"
#include "html_attribute_name.h"
#include "dom_strings.h"
#include <aether/aether_dom_sync.h>
#include <optional>
#include "../style_runtime/aether_dom_style_binding.h"

namespace {
bool token_equal(const aether_dom_token_v1_t& a,const aether_dom_token_v1_t& b) noexcept {
    return a.owner_identity==b.owner_identity&&a.observation_serial==b.observation_serial&&a.input_epoch==b.input_epoch&&a.structure_epoch==b.structure_epoch&&a.sticky_status==b.sticky_status;
}
bool valid_utf8(aether_dom_string_v1_t s) noexcept {return (!s.length||s.data)&&aether_dom_valid_string({s.data?s.data:"",s.length});}
std::string read(aether_dom_string_v1_t s){if(!valid_utf8(s))throw std::invalid_argument("DOM UTF-8 domain");return s.length?std::string(s.data,s.length):std::string{};}
bool synthetic(const aether_node_s* node)noexcept{for(auto* n=node;n;n=n->parent)if(n->is_generated||n->anonymous_flex_owner)return true;return false;}
void collect(aether_node_s* node,std::vector<aether_node_s*>& out){out.push_back(node);for(auto* c:node->children)collect(c,out);}
using Registry=decltype(aether_doc_s::editable_nodes);
struct Domain {
    std::shared_ptr<EditableNodeStorage> root;
    std::shared_ptr<DomOwnerControl> owner;
    aether_dom_token_v1_t token{};
    uint64_t style_tree_epoch=0,interaction_serial=0;
    std::shared_ptr<DomInteractionRegistry> interaction;
    std::optional<Registry> registry;
    int64_t next_id=0;
};
struct Links { aether_node_s* node=nullptr;std::shared_ptr<EditableNodeStorage> pin;std::vector<aether_node_s*> children; };
struct Touch { aether_node_s* node; aether::dom::MutationKind kind;bool structure;int32_t before=0,after=0;aether_node_s* subject=nullptr; };
}
struct aether_dom_edit_s {
    DomStyleSlotBatchRef style_batch;
    std::vector<DomInteractionStageRef> interaction_stages;
    std::vector<Domain> domains;
    std::vector<Links> links;
    std::vector<Touch> touches;
    std::vector<std::shared_ptr<EditableNodeStorage>> pins,created,results;
    std::vector<std::pair<aether_node_s*,aether_node_s*>> materialized_parents;
    std::vector<aether_node_s*> disconnect,attach,adopt;
    std::vector<std::shared_ptr<DomNodeOwnerPin>> adopt_relays;
    std::vector<std::shared_ptr<aether_node_s>> generated_pins;
    aether_node_s* target=nullptr; aether_node_s* child=nullptr;
    aether_node_s* new_parent=nullptr;
    std::optional<decltype(aether_node_s::attributes)> attributes;
    std::optional<std::string> text,authored,runtime;
    std::optional<int32_t> state;
    bool authored_present=false,runtime_present=false,write_authored=false,write_runtime=false;
    bool reparent=false,committed=false,structural=false,materialize=false,materialize_document=false;
    std::vector<std::pair<aether_node_s*,int64_t>> adopted_ids;
    size_t adopt_domain=0;
};
namespace {
size_t domain(aether_dom_edit_s& edit,aether_node_s* node){
    auto control=node->document_owner.lock();if(!control||aether_dom_access(node)!=AETHER_OK)throw std::invalid_argument("DOM domain");
    for(size_t i=0;i<edit.domains.size();++i)if(edit.domains[i].owner==control)return i;
    Domain d;d.owner=control;d.interaction=control->interaction.lock();d.interaction_serial=control->interaction_serial;d.root=control->document.lock();if(!d.root)throw std::invalid_argument("DOM root expired");
    if(aether_dom_read_token_v1(node,&d.token)!=AETHER_OK)throw std::invalid_argument("DOM token");d.style_tree_epoch=d.root->node->identity->style_tree_epoch;d.next_id=static_cast<aether_doc_s*>(d.root->node.get())->next_style_node_id;
    edit.domains.push_back(std::move(d));return edit.domains.size()-1;
}
void pin(aether_dom_edit_s& edit,aether_node_s* node){auto p=aether_editable_lease(node);if(!p)throw std::invalid_argument("DOM noneditable node");edit.pins.push_back(std::move(p));}
Registry& registry(Domain& d){if(!d.registry)d.registry=static_cast<aether_doc_s*>(d.root->node.get())->editable_nodes;return *d.registry;}
Links& links(aether_dom_edit_s& e,aether_node_s* node){for(auto& l:e.links)if(l.node==node)return l;Links l;l.node=node;l.pin=aether_editable_lease(node);if(!l.pin)throw std::invalid_argument("DOM links");l.children=node->children;e.links.push_back(std::move(l));return e.links.back();}
size_t insertion_position(const std::vector<aether_node_s*>& nodes,int64_t index){if(index<0)return nodes.size();size_t authored=0;for(size_t i=0;i<nodes.size();++i)if(!nodes[i]->is_generated){if(authored++==static_cast<uint64_t>(index))return i;}if(authored==static_cast<uint64_t>(index))return nodes.size();throw std::out_of_range("DOM child index");}
void add_disconnect(aether_dom_edit_s& e,aether_node_s* node){std::vector<aether_node_s*> all;collect(node,all);for(auto* n:all){if(n->identity->disconnect_serial==UINT64_MAX)throw std::length_error("DOM disconnect exhausted");e.disconnect.push_back(n);}}
void add_touch(aether_dom_edit_s& e,aether_node_s* node,aether::dom::MutationKind kind,bool structure=false,aether_node_s* subject=nullptr){e.touches.push_back({node,kind,structure,0,0,subject});}
void prepare_create(aether_dom_edit_s& e,const aether_dom_edit_spec_v1_t& s){
    if(s.node_count&&!s.nodes)throw std::invalid_argument("DOM specs");auto& d=e.domains[0];auto& owned=registry(d);e.created.reserve(s.node_count);e.results.reserve(s.node_count);
    for(size_t i=0;i<s.node_count;++i){const auto& spec=s.nodes[i];if(spec.existing_node)throw std::invalid_argument("existing subtree requires MATERIALIZE");if(spec.type<AETHER_NODE_ELEMENT||spec.type>AETHER_NODE_COMMENT||spec.parent_index>=static_cast<int64_t>(i)||spec.parent_index< -1)throw std::invalid_argument("DOM type/parent");
        if(d.next_id==INT64_MAX)throw std::length_error("DOM identity exhausted");auto node=std::make_unique<aether_node_s>();node->type=static_cast<aether_node_type_t>(spec.type);node->tag_name=aether::ascii_lowercase(read(spec.tag));node->text=read(spec.text);
        if(node->type==AETHER_NODE_ELEMENT&&node->tag_name.empty())throw std::invalid_argument("DOM tag");
        node->state=spec.state;node->runtime_value_present=spec.runtime_value_present!=0;node->runtime_value=read(spec.runtime_value);
        if(spec.attribute_count&&!spec.attributes)throw std::invalid_argument("DOM attributes");
        for(size_t j=0;j<spec.attribute_count;++j){auto name=aether::ascii_lowercase(read(spec.attributes[j].name));if(name.empty())throw std::invalid_argument("DOM attribute name");node->attributes[name]=read(spec.attributes[j].value);}
        if(auto found=node->attributes.find("value");found!=node->attributes.end()){node->authored_value_present=true;node->authored_value=found->second;}
        if(node->tag_name=="input"&&node->runtime_value_present)node->attributes["value"]=node->runtime_value;
        node->style_node_id=d.next_id++;node->identity->attached=false;node->unpublished=true;auto storage=std::make_shared<EditableNodeStorage>(std::move(node));auto* raw=storage->node.get();
        raw->editable_storage=storage;raw->identity->storage=storage;raw->document_owner=d.owner;raw->identity->document_owner=d.owner;
        if(spec.parent_index>=0){auto* parent=e.created[spec.parent_index]->node.get();parent->children.push_back(raw);raw->parent=parent;}
        owned.emplace(raw,storage);e.results.push_back(storage);e.created.push_back(std::move(storage));
    }
}
void prepare_materialize(aether_dom_edit_s& e,const aether_dom_edit_spec_v1_t& s) {
    if((s.node_count&&!s.nodes)||(!s.node_count&&s.kind!=AETHER_DOM_MATERIALIZE_DOCUMENT))throw std::invalid_argument("empty materialization");
    e.materialize=true;e.materialize_document=s.kind==AETHER_DOM_MATERIALIZE_DOCUMENT;e.structural=true;
    if(e.materialize_document) {
        auto* doc=dynamic_cast<aether_doc_s*>(e.target);
        if(!doc||!doc->children.empty()||!doc->editable_nodes.empty()||doc->version||doc->owner_control->observation_serial||doc->next_style_node_id!=1)
            throw std::invalid_argument("materialization requires fresh document");
        links(e,doc);add_touch(e,doc,aether::dom::MutationKind::structure,true);
    }
    e.results.reserve(s.node_count);e.created.reserve(s.node_count);
    std::vector<aether_node_s*> existing_roots;
    for(size_t i=0;i<s.node_count;++i) {
        const auto& spec=s.nodes[i];
        if(spec.parent_index< -1||spec.parent_index>=static_cast<int64_t>(i))throw std::invalid_argument("materialized parent");
        if(spec.parent_index>=0&&s.nodes[spec.parent_index].existing_node)throw std::invalid_argument("existing subtree is opaque");
        auto* parent=spec.parent_index>=0?e.results[spec.parent_index]->node.get():(e.materialize_document?e.target:nullptr);
        if(auto* node=spec.existing_node) {
            if(spec.type||spec.state||spec.tag.length||spec.text.length||spec.runtime_value.length||spec.runtime_value_present||spec.attribute_count||spec.attributes)
                throw std::invalid_argument("existing subtree payload");
            if(node->type==AETHER_NODE_DOCUMENT||synthetic(node))throw std::invalid_argument("existing subtree type");
            for(auto* other:existing_roots){for(auto* n=node;n;n=n->parent)if(n==other)throw std::invalid_argument("overlapping existing subtree");for(auto* n=other;n;n=n->parent)if(n==node)throw std::invalid_argument("overlapping existing subtree");}
            const auto source=domain(e,node);auto storage=aether_editable_lease(node);if(!storage)throw std::invalid_argument("existing storage");
            existing_roots.push_back(node);e.results.push_back(std::move(storage));
            auto* old_parent=node->parent;
            if(old_parent){auto& previous=links(e,old_parent).children;auto found=std::find(previous.begin(),previous.end(),node);if(found==previous.end())throw std::invalid_argument("nonreciprocal existing subtree");previous.erase(found);add_touch(e,old_parent,aether::dom::MutationKind::structure,true,node);}
            if(node->identity->attached||source!=0)add_disconnect(e,node);
            if(source!=0) {
                std::vector<aether_node_s*> moving;collect(node,moving);
                if(!old_parent)add_touch(e,node,aether::dom::MutationKind::structure,true,node);
                for(auto* moved:moving) {
                    e.adopt.push_back(moved);
                    if(auto lease=aether_editable_lease(moved)) {
                    if(e.domains[0].next_id==INT64_MAX)throw std::length_error("DOM identity exhausted");
                    e.adopted_ids.emplace_back(moved,e.domains[0].next_id++);registry(e.domains[source]).erase(moved);registry(e.domains[0]).emplace(moved,std::move(lease));
                    }
                }
            }
            e.materialized_parents.emplace_back(node,parent);
            std::vector<aether_node_s*> all;collect(node,all);e.attach.insert(e.attach.end(),all.begin(),all.end());
            if(parent){if(parent==e.target&&e.materialize_document)links(e,parent).children.push_back(node);else parent->children.push_back(node);}
        } else {
            if(spec.type<AETHER_NODE_ELEMENT||spec.type>AETHER_NODE_COMMENT)throw std::invalid_argument("materialized node type");
            if(e.domains[0].next_id==INT64_MAX)throw std::length_error("DOM identity exhausted");
            auto created=std::make_unique<aether_node_s>();created->type=static_cast<aether_node_type_t>(spec.type);created->tag_name=aether::ascii_lowercase(read(spec.tag));created->text=read(spec.text);
            if(created->type==AETHER_NODE_ELEMENT&&created->tag_name.empty())throw std::invalid_argument("DOM tag");
            created->state=spec.state;created->runtime_value_present=spec.runtime_value_present!=0;created->runtime_value=read(spec.runtime_value);
            if(spec.attribute_count&&!spec.attributes)throw std::invalid_argument("DOM attributes");
            for(size_t j=0;j<spec.attribute_count;++j){auto name=aether::ascii_lowercase(read(spec.attributes[j].name));if(name.empty())throw std::invalid_argument("DOM attribute name");created->attributes[name]=read(spec.attributes[j].value);}
            if(auto found=created->attributes.find("value");found!=created->attributes.end()){created->authored_value_present=true;created->authored_value=found->second;}
            if(created->tag_name=="input"&&created->runtime_value_present)created->attributes["value"]=created->runtime_value;
            created->style_node_id=e.domains[0].next_id++;created->identity->attached=false;created->unpublished=true;
            auto storage=std::make_shared<EditableNodeStorage>(std::move(created));auto* raw=storage->node.get();raw->editable_storage=storage;raw->identity->storage=storage;
            raw->document_owner=e.domains[0].owner;raw->identity->document_owner=e.domains[0].owner;raw->parent=parent;if(parent){if(parent==e.target&&e.materialize_document)links(e,parent).children.push_back(raw);else parent->children.push_back(raw);}
            registry(e.domains[0]).emplace(raw,storage);e.results.push_back(storage);e.created.push_back(std::move(storage));e.attach.push_back(raw);
            if(!parent)add_touch(e,raw,aether::dom::MutationKind::structure,true);
        }
    }
}
const std::vector<aether_node_s*>& candidate_children(const aether_dom_edit_s& e,aether_node_s* node) noexcept {
    for(const auto& l:e.links)if(l.node==node)return l.children;return node->children;
}
void candidate_text(const aether_dom_edit_s& e,aether_node_s* node,std::string& source) {
    if(node->is_generated)return;
    if(node->type==AETHER_NODE_TEXT)source+=(node==e.target&&e.text)?*e.text:node->text;
    else for(auto* child:candidate_children(e,node))candidate_text(e,child,source);
}
void candidate_source(const aether_dom_edit_s& e,aether_node_s* node,std::string& source,bool& first) {
    if(node->is_generated)return;
    if(node->type==AETHER_NODE_ELEMENT&&node->tag_name=="style"){if(!first)source+='\n';first=false;candidate_text(e,node,source);}
    for(auto* child:candidate_children(e,node))candidate_source(e,child,source,first);
}
aether_status_t prepare_slots(aether_dom_edit_s& e) {
    if(!e.structural&&!e.text)return AETHER_OK;
    std::vector<DomStyleSlotStageRef> stages;
    for(const auto& d:e.domains)if(auto binding=d.owner->binding.lock()) {
        std::string source;bool first=true;candidate_source(e,d.root->node.get(),source,first);
        DomStyleSlotStageRef stage;auto status=aether_dom_style_prepare_slot(binding,source,stage);if(status!=AETHER_OK)return status;stages.push_back(std::move(stage));
    }
    return stages.empty()?AETHER_OK:aether_dom_style_finalize_slots(stages,e.style_batch);
}
// Staged links and attachment lists may retain generated nodes replaced by a
// style-only FULL publication. Pin the owning generated allocation separately
// from the editable host, which does not keep its old generated_storage alive.
// Only inspect staged structural lists; scalar State remains O(1).
void pin_generated_path(aether_dom_edit_s& e,aether_node_s* node) {
    for(auto* current=node;current;current=current->parent) {
        for(const auto& generated:current->generated_storage)e.generated_pins.push_back(generated);
        if(aether_editable_lease(current))break;
    }
}
void pin_staged_generated(aether_dom_edit_s& e) {
    for(const auto& link:e.links) {
        pin_generated_path(e,link.node);
        for(auto* child:link.children)pin_generated_path(e,child);
    }
    for(auto* node:e.disconnect)pin_generated_path(e,node);
    for(auto* node:e.attach)pin_generated_path(e,node);
    for(auto* node:e.adopt)pin_generated_path(e,node);
}
void preflight_counters(const aether_dom_edit_s& e){
    for(const auto& d:e.domains){uint64_t n=0;for(const auto& t:e.touches)if(t.node->document_owner.lock()==d.owner)++n;if(d.owner->observation_serial>UINT64_MAX-n)throw std::length_error("DOM observation exhausted");}
    for(const auto& t:e.touches){auto* root=aether_dom_root(t.node);uint64_t n=0,structural=0;for(const auto& t2:e.touches)if(aether_dom_root(t2.node)==root){++n;if(t2.structure)++structural;}if(root->identity->mutation_epoch>UINT64_MAX-n||root->identity->structure_epoch>UINT64_MAX-structural)throw std::length_error("DOM epoch exhausted");}
}
}
AETHER_API aether_status_t aether_dom_prepare_edit_v1(const aether_dom_token_v1_t* expected,const aether_dom_edit_spec_v1_t* spec,aether_dom_edit_t* out){
    if(out)*out=nullptr;if(!expected||!spec||!out||!spec->target)return AETHER_ERROR_NULL_ARG;
    if(synthetic(spec->target))return AETHER_ERROR_INVALID_STATE;
    try{
        auto e=std::make_unique<aether_dom_edit_s>();e->target=spec->target;domain(*e,spec->target);pin(*e,spec->target);if(!token_equal(*expected,e->domains[0].token))return AETHER_ERROR_INVALID_STATE;
        auto* target=spec->target;const auto kind=spec->kind;
        if(kind==AETHER_DOM_MATERIALIZE_FOREST||kind==AETHER_DOM_MATERIALIZE_DOCUMENT){prepare_materialize(*e,*spec);}
        else if(kind==AETHER_DOM_SET_ATTRIBUTE||kind==AETHER_DOM_REMOVE_ATTRIBUTE||kind==AETHER_DOM_SET_LEGACY_ATTRIBUTE){
            if(target->type!=AETHER_NODE_ELEMENT)return AETHER_ERROR_INVALID_STATE;auto name=aether::ascii_lowercase(read(spec->name));if(name.empty())return AETHER_ERROR_INVALID_STATE;
            auto attrs=target->attributes;bool existed=attrs.contains(name);
            if(name=="value")existed=target->authored_value_present;
            if(kind==AETHER_DOM_REMOVE_ATTRIBUTE&&!existed){*out=e.release();return AETHER_OK;}
            if(name=="value"){
                e->authored=kind!=AETHER_DOM_REMOVE_ATTRIBUTE?read(spec->value):std::string{};e->write_authored=true;e->authored_present=kind!=AETHER_DOM_REMOVE_ATTRIBUTE;
                if(kind==AETHER_DOM_SET_LEGACY_ATTRIBUTE){e->runtime=std::string{};e->write_runtime=true;e->runtime_present=false;}
                if(kind==AETHER_DOM_SET_LEGACY_ATTRIBUTE||target->tag_name!="input"||!target->runtime_value_present){if(e->authored_present)attrs[name]=*e->authored;else attrs.erase(name);}
            }else if(kind!=AETHER_DOM_REMOVE_ATTRIBUTE)attrs[name]=read(spec->value);else attrs.erase(name);
            e->attributes=std::move(attrs);add_touch(*e,target,aether::dom::MutationKind::attribute);
        }else if(kind==AETHER_DOM_SET_STATE){
            if(target->type!=AETHER_NODE_ELEMENT)return AETHER_ERROR_INVALID_STATE;
            if(target->state!=spec->state){for(unsigned i=0;i<32;++i)if(((uint32_t(target->state)^uint32_t(spec->state))&(uint32_t{1}<<i))&&target->identity->state_bit_write_serial[i]==UINT64_MAX)return AETHER_ERROR_INVALID_STATE;
                e->state=spec->state;e->touches.push_back({target,aether::dom::MutationKind::state,false,target->state,spec->state,nullptr});}
        }else if(kind==AETHER_DOM_SET_RUNTIME_VALUE){
            if(target->type!=AETHER_NODE_ELEMENT)return AETHER_ERROR_INVALID_STATE;auto value=read(spec->value);bool present=spec->value_present!=0;
            if(present!=target->runtime_value_present||(present&&value!=target->runtime_value)){
                e->runtime=std::move(value);e->write_runtime=true;e->runtime_present=present;
                if(target->tag_name=="input"){e->attributes=target->attributes;if(present)(*e->attributes)["value"]=*e->runtime;else if(target->authored_value_present)(*e->attributes)["value"]=target->authored_value;else e->attributes->erase("value");}
                add_touch(*e,target,aether::dom::MutationKind::attribute);
            }
        }else if(kind==AETHER_DOM_SET_TEXT){
            if(target->type!=AETHER_NODE_TEXT&&target->type!=AETHER_NODE_COMMENT)return AETHER_ERROR_INVALID_STATE;e->text=read(spec->value);add_touch(*e,target,aether::dom::MutationKind::text);
        }else if(kind==AETHER_DOM_INSERT_CHILD||kind==AETHER_DOM_REMOVE_CHILD){
            auto* child=spec->child;if(!child||synthetic(child)||child->type==AETHER_NODE_DOCUMENT)return AETHER_ERROR_INVALID_STATE;
            pin(*e,child);size_t source=domain(*e,child);e->child=child;e->reparent=true;e->structural=true;auto* old=child->parent;
            if(kind==AETHER_DOM_REMOVE_CHILD){if(old!=target)return AETHER_ERROR_INVALID_STATE;auto& next=links(*e,target).children;auto found=std::find(next.begin(),next.end(),child);if(found==next.end())return AETHER_ERROR_INVALID_STATE;next.erase(found);add_disconnect(*e,child);add_touch(*e,target,aether::dom::MutationKind::structure,true,child);}
            else{
                for(auto* a=target;a;a=a->parent)if(a==child)return AETHER_ERROR_INVALID_STATE;
                if(old){auto& previous=links(*e,old).children;auto found=std::find(previous.begin(),previous.end(),child);if(found==previous.end())return AETHER_ERROR_INVALID_STATE;previous.erase(found);add_touch(*e,old,aether::dom::MutationKind::structure,true,child);}
                auto& next=links(*e,target).children;next.insert(next.begin()+insertion_position(next,spec->index),child);e->new_parent=target;collect(child,e->attach);add_touch(*e,target,aether::dom::MutationKind::structure,true,child);
                if(source==0&&child->identity->attached&&!target->identity->attached)add_disconnect(*e,child);
                if(source!=0){add_disconnect(*e,child);e->adopt=e->attach;e->adopt_domain=0;
                    auto& src=registry(e->domains[source]);auto& dst=registry(e->domains[0]);for(auto* node:e->adopt)if(auto lease=aether_editable_lease(node)){if(e->domains[0].next_id==INT64_MAX)return AETHER_ERROR_INVALID_STATE;e->adopted_ids.emplace_back(node,e->domains[0].next_id++);src.erase(node);dst.emplace(node,std::move(lease));}
                    if(!old)add_touch(*e,child,aether::dom::MutationKind::structure,true,child);
                }
            }
        }else if(kind==AETHER_DOM_CLEAR_CHILDREN||kind==AETHER_DOM_REPLACE_CHILDREN||kind==AETHER_DOM_INSERT_SUBTREE){
            e->structural=true;auto& next=links(*e,target).children;
            if(kind!=AETHER_DOM_INSERT_SUBTREE){for(auto* child:next)if(!child->is_generated)add_disconnect(*e,child);next.erase(std::remove_if(next.begin(),next.end(),[](auto* n){return !n->is_generated;}),next.end());}
            if(kind!=AETHER_DOM_CLEAR_CHILDREN){prepare_create(*e,*spec);auto at=insertion_position(next,spec->index);
                for(auto& storage:e->created){auto* node=storage->node.get();if(!node->parent){next.insert(next.begin()+at++,node);}e->attach.push_back(node);}
            }
            add_touch(*e,target,aether::dom::MutationKind::structure,true);
        }else return AETHER_ERROR_INVALID_STATE;
        for(auto* n:e->disconnect){auto* owner=n;while(owner&&!aether_editable_lease(owner))owner=owner->parent;if(!owner)return AETHER_ERROR_INVALID_STATE;pin(*e,owner);} // Retain editable owners; old generated allocations are pinned separately below.
        e->adopt_relays.reserve(e->adopt.size());for(auto* n:e->adopt)e->adopt_relays.push_back(aether_live_owner_pin(n));
        pin_staged_generated(*e);preflight_counters(*e);
        for(const auto& d:e->domains) {
            DomInteractionStageRef stage;
            const auto status=aether_dom_interaction_prepare_stage(d.owner,e->disconnect,e->target,e->attributes?&*e->attributes:nullptr,stage);
            if(status!=AETHER_OK)return status;if(stage)e->interaction_stages.push_back(std::move(stage));
        }
        const auto slots=prepare_slots(*e);if(slots!=AETHER_OK)return slots;*out=e.release();return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API aether_status_t aether_dom_edit_created_node_v1(aether_dom_edit_t e,size_t index,aether_node_t* out){if(out)*out=nullptr;if(!e||!out)return AETHER_ERROR_NULL_ARG;if(index>=e->results.size())return AETHER_ERROR_INVALID_STATE;*out=e->results[index]->node.get();return AETHER_OK;}
AETHER_API aether_status_t aether_dom_edit_validate_v1(aether_dom_edit_t e){if(!e)return AETHER_ERROR_NULL_ARG;if(e->committed)return AETHER_ERROR_INVALID_STATE;for(const auto& stage:e->interaction_stages)if(!aether_dom_interaction_validate_stage(stage))return AETHER_ERROR_INVALID_STATE;if(e->style_batch&&!aether_dom_style_validate_batch(e->style_batch))return AETHER_ERROR_INVALID_STATE;
    for(const auto& d:e->domains){aether_dom_token_v1_t now{};if(aether_dom_read_token_v1(d.root->node.get(),&now)!=AETHER_OK||!token_equal(now,d.token)||d.root->node->identity->style_tree_epoch!=d.style_tree_epoch||d.owner->interaction_serial!=d.interaction_serial||d.owner->interaction.lock()!=d.interaction)return AETHER_ERROR_INVALID_STATE;}return AETHER_OK;}
AETHER_API aether_status_t aether_dom_edit_commit_v1(aether_dom_edit_t e,aether_dom_token_v1_t* out){if(out)*out={};if(!e||!out)return AETHER_ERROR_NULL_ARG;auto status=aether_dom_edit_validate_v1(e);if(status!=AETHER_OK)return status;
    for(auto& n:e->created)n->node->unpublished=false;
    for(const auto& t:e->touches)aether_node_touch_typed(t.node,t.kind,t.structure,t.before,t.after,t.subject);
    auto* target=e->target;if(e->attributes)target->attributes.swap(*e->attributes);if(e->text)target->text.swap(*e->text);
    if(e->write_authored){target->authored_value.swap(*e->authored);target->authored_value_present=e->authored_present;}
    if(e->write_runtime){target->runtime_value.swap(*e->runtime);target->runtime_value_present=e->runtime_present;}
    if(e->state){for(unsigned i=0;i<32;++i)if((uint32_t(target->state)^uint32_t(*e->state))&(uint32_t{1}<<i))++target->identity->state_bit_write_serial[i];target->state=*e->state;}
    for(auto& l:e->links)l.node->children.swap(l.children);
    for(const auto& stage:e->interaction_stages)aether_dom_interaction_commit_stage(stage);
    for(auto* n:e->disconnect){++n->identity->disconnect_serial;n->identity->attached=false;if(n->parent==target)n->parent=nullptr;}
    if(e->reparent)e->child->parent=e->new_parent;
    if(!e->materialize)for(auto& node:e->created)if(!node->node->parent)node->node->parent=target;
    for(const auto& [node,parent]:e->materialized_parents)node->parent=parent;
    for(auto* n:e->attach)n->identity->attached=e->materialize&&!e->materialize_document?false:target->identity->attached;
    for(const auto& relay:e->adopt_relays)if(relay)relay->current_root=e->domains[e->adopt_domain].root;
    for(auto* n:e->adopt){n->document_owner=e->domains[e->adopt_domain].owner;n->identity->document_owner=e->domains[e->adopt_domain].owner;}
    for(const auto& [node,id]:e->adopted_ids)node->style_node_id=id;
    for(auto& d:e->domains)if(d.registry){auto* doc=static_cast<aether_doc_s*>(d.root->node.get());doc->editable_nodes.swap(*d.registry);doc->next_style_node_id=d.next_id;}
    if(e->style_batch)aether_dom_style_commit_batch(e->style_batch);
    e->committed=true;return aether_dom_read_token_v1(target,out);
}
AETHER_API void aether_dom_edit_destroy_v1(aether_dom_edit_t e){if(e&&!e->committed)for(auto& n:e->created)aether_dom_retire_node(n->node.get());delete e;}
