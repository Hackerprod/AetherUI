#include "dom_types.h"
#include "html_attribute_name.h"
#include <aether/aether_dom_sync.h>
#include <optional>

namespace {
bool token_equal(const aether_dom_token_v1_t& a,const aether_dom_token_v1_t& b) noexcept {
    return a.owner_identity==b.owner_identity&&a.observation_serial==b.observation_serial&&a.input_epoch==b.input_epoch&&a.structure_epoch==b.structure_epoch&&a.sticky_status==b.sticky_status;
}
bool valid_utf8(aether_dom_string_v1_t s) noexcept {
    if(s.length&&!s.data)return false;
    for(size_t i=0;i<s.length;){auto c=static_cast<unsigned char>(s.data[i++]);if(!c)return false;if(c<0x80)continue;
        unsigned n=0;uint32_t cp=0,min=0;
        if(c>=0xc2&&c<=0xdf){n=1;cp=c&31;min=0x80;}else if(c>=0xe0&&c<=0xef){n=2;cp=c&15;min=0x800;}else if(c>=0xf0&&c<=0xf4){n=3;cp=c&7;min=0x10000;}else return false;
        if(s.length-i<n)return false;while(n--){c=static_cast<unsigned char>(s.data[i++]);if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
    }return true;
}
std::string read(aether_dom_string_v1_t s){if(!valid_utf8(s))throw std::invalid_argument("DOM UTF-8 domain");return s.length?std::string(s.data,s.length):std::string{};}
bool synthetic(const aether_node_s* node)noexcept{for(auto* n=node;n;n=n->parent)if(n->is_generated||n->anonymous_flex_owner)return true;return false;}
void collect(aether_node_s* node,std::vector<aether_node_s*>& out){out.push_back(node);for(auto* c:node->children)collect(c,out);}
using Registry=decltype(aether_doc_s::editable_nodes);
struct Domain {
    std::shared_ptr<EditableNodeStorage> root;
    std::shared_ptr<DomOwnerControl> owner;
    aether_dom_token_v1_t token{};
    std::optional<Registry> registry;
    int64_t next_id=0;
};
struct Links { aether_node_s* node=nullptr;std::shared_ptr<EditableNodeStorage> pin;std::vector<aether_node_s*> children; };
struct Touch { aether_node_s* node; aether::dom::MutationKind kind;bool structure;int32_t before=0,after=0;aether_node_s* subject=nullptr; };
}
struct aether_dom_edit_s {
    std::vector<Domain> domains;
    std::vector<Links> links;
    std::vector<Touch> touches;
    std::vector<std::shared_ptr<EditableNodeStorage>> pins,created;
    std::vector<aether_node_s*> disconnect,attach,adopt;
    aether_node_s* target=nullptr; aether_node_s* child=nullptr;
    aether_node_s* new_parent=nullptr;
    std::optional<decltype(aether_node_s::attributes)> attributes;
    std::optional<std::string> text,authored,runtime;
    std::optional<int32_t> state;
    bool authored_present=false,runtime_present=false,write_authored=false,write_runtime=false;
    bool reparent=false,committed=false,structural=false;
    int64_t adopt_first_id=0;
    size_t adopt_domain=0;
};
namespace {
size_t domain(aether_dom_edit_s& edit,aether_node_s* node){
    auto control=node->document_owner.lock();if(!control||aether_dom_access(node)!=AETHER_OK)throw std::invalid_argument("DOM domain");
    for(size_t i=0;i<edit.domains.size();++i)if(edit.domains[i].owner==control)return i;
    Domain d;d.owner=control;d.root=control->document.lock();if(!d.root)throw std::invalid_argument("DOM root expired");
    if(aether_dom_read_token_v1(node,&d.token)!=AETHER_OK)throw std::invalid_argument("DOM token");d.next_id=static_cast<aether_doc_s*>(d.root->node.get())->next_style_node_id;
    edit.domains.push_back(std::move(d));return edit.domains.size()-1;
}
void pin(aether_dom_edit_s& edit,aether_node_s* node){auto p=aether_editable_lease(node);if(!p)throw std::invalid_argument("DOM noneditable node");edit.pins.push_back(std::move(p));}
Registry& registry(Domain& d){if(!d.registry)d.registry=static_cast<aether_doc_s*>(d.root->node.get())->editable_nodes;return *d.registry;}
Links& links(aether_dom_edit_s& e,aether_node_s* node){for(auto& l:e.links)if(l.node==node)return l;Links l;l.node=node;l.pin=aether_editable_lease(node);if(!l.pin)throw std::invalid_argument("DOM links");l.children=node->children;e.links.push_back(std::move(l));return e.links.back();}
size_t insertion_position(const std::vector<aether_node_s*>& nodes,int64_t index){if(index<0)return nodes.size();size_t authored=0;for(size_t i=0;i<nodes.size();++i)if(!nodes[i]->is_generated){if(authored++==static_cast<uint64_t>(index))return i;}if(authored==static_cast<uint64_t>(index))return nodes.size();throw std::out_of_range("DOM child index");}
void add_disconnect(aether_dom_edit_s& e,aether_node_s* node){std::vector<aether_node_s*> all;collect(node,all);for(auto* n:all){if(n->identity->disconnect_serial==UINT64_MAX)throw std::length_error("DOM disconnect exhausted");e.disconnect.push_back(n);}}
void add_touch(aether_dom_edit_s& e,aether_node_s* node,aether::dom::MutationKind kind,bool structure=false,aether_node_s* subject=nullptr){e.touches.push_back({node,kind,structure,0,0,subject});}
void prepare_create(aether_dom_edit_s& e,const aether_dom_edit_spec_v1_t& s){
    if(s.node_count&&!s.nodes)throw std::invalid_argument("DOM specs");auto& d=e.domains[0];auto& owned=registry(d);e.created.reserve(s.node_count);
    for(size_t i=0;i<s.node_count;++i){const auto& spec=s.nodes[i];if(spec.type<AETHER_NODE_ELEMENT||spec.type>AETHER_NODE_COMMENT||spec.parent_index>=static_cast<int64_t>(i)||spec.parent_index< -1)throw std::invalid_argument("DOM type/parent");
        if(d.next_id==INT64_MAX)throw std::length_error("DOM identity exhausted");auto node=std::make_unique<aether_node_s>();node->type=static_cast<aether_node_type_t>(spec.type);node->tag_name=aether::ascii_lowercase(read(spec.tag));node->text=read(spec.text);
        if(node->type==AETHER_NODE_ELEMENT&&node->tag_name.empty())throw std::invalid_argument("DOM tag");
        node->state=spec.state;node->runtime_value_present=spec.runtime_value_present!=0;node->runtime_value=read(spec.runtime_value);
        if(spec.attribute_count&&!spec.attributes)throw std::invalid_argument("DOM attributes");
        for(size_t j=0;j<spec.attribute_count;++j){auto name=aether::ascii_lowercase(read(spec.attributes[j].name));if(name.empty())throw std::invalid_argument("DOM attribute name");node->attributes[name]=read(spec.attributes[j].value);}
        if(auto found=node->attributes.find("value");found!=node->attributes.end()){node->authored_value_present=true;node->authored_value=found->second;}
        if(node->tag_name=="input"&&node->runtime_value_present)node->attributes["value"]=node->runtime_value;
        node->style_node_id=d.next_id++;node->identity->attached=false;auto storage=std::make_shared<EditableNodeStorage>(std::move(node));auto* raw=storage->node.get();
        raw->editable_storage=storage;raw->identity->storage=storage;raw->document_owner=d.owner;raw->identity->document_owner=d.owner;
        if(spec.parent_index>=0){auto* parent=e.created[spec.parent_index]->node.get();parent->children.push_back(raw);raw->parent=parent;}
        owned.emplace(raw,storage);e.created.push_back(std::move(storage));
    }
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
        if(kind==AETHER_DOM_SET_ATTRIBUTE||kind==AETHER_DOM_REMOVE_ATTRIBUTE){
            if(target->type!=AETHER_NODE_ELEMENT)return AETHER_ERROR_INVALID_STATE;auto name=aether::ascii_lowercase(read(spec->name));if(name.empty())return AETHER_ERROR_INVALID_STATE;
            auto attrs=target->attributes;bool existed=attrs.contains(name);
            if(name=="value")existed=target->authored_value_present;
            if(kind==AETHER_DOM_REMOVE_ATTRIBUTE&&!existed){*out=e.release();return AETHER_OK;}
            if(name=="value"){
                e->authored=kind==AETHER_DOM_SET_ATTRIBUTE?read(spec->value):std::string{};e->write_authored=true;e->authored_present=kind==AETHER_DOM_SET_ATTRIBUTE;
                if(target->tag_name!="input"||!target->runtime_value_present){if(e->authored_present)attrs[name]=*e->authored;else attrs.erase(name);}
            }else if(kind==AETHER_DOM_SET_ATTRIBUTE)attrs[name]=read(spec->value);else attrs.erase(name);
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
                if(source!=0){add_disconnect(*e,child);e->adopt=e->attach;e->adopt_domain=0;e->adopt_first_id=e->domains[0].next_id;
                    auto& src=registry(e->domains[source]);auto& dst=registry(e->domains[0]);for(auto* node:e->adopt)if(auto lease=aether_editable_lease(node)){if(e->domains[0].next_id==INT64_MAX)return AETHER_ERROR_INVALID_STATE;++e->domains[0].next_id;src.erase(node);dst.emplace(node,std::move(lease));}
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
        for(auto* n:e->disconnect){auto* owner=n;while(owner&&!aether_editable_lease(owner))owner=owner->parent;if(!owner)return AETHER_ERROR_INVALID_STATE;pin(*e,owner);} // Generated descendants are retained through their editable host.
        preflight_counters(*e);*out=e.release();return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API aether_status_t aether_dom_edit_created_node_v1(aether_dom_edit_t e,size_t index,aether_node_t* out){if(out)*out=nullptr;if(!e||!out)return AETHER_ERROR_NULL_ARG;if(index>=e->created.size())return AETHER_ERROR_INVALID_STATE;*out=e->created[index]->node.get();return AETHER_OK;}
AETHER_API aether_status_t aether_dom_edit_validate_v1(aether_dom_edit_t e){if(!e)return AETHER_ERROR_NULL_ARG;if(e->committed)return AETHER_ERROR_INVALID_STATE;
    for(const auto& d:e->domains){aether_dom_token_v1_t now{};if(aether_dom_read_token_v1(d.root->node.get(),&now)!=AETHER_OK||!token_equal(now,d.token))return AETHER_ERROR_INVALID_STATE;}return AETHER_OK;}
AETHER_API aether_status_t aether_dom_edit_commit_v1(aether_dom_edit_t e,aether_dom_token_v1_t* out){if(out)*out={};if(!e||!out)return AETHER_ERROR_NULL_ARG;auto status=aether_dom_edit_validate_v1(e);if(status!=AETHER_OK)return status;
    for(const auto& t:e->touches)aether_node_touch_typed(t.node,t.kind,t.structure,t.before,t.after,t.subject);
    auto* target=e->target;if(e->attributes)target->attributes.swap(*e->attributes);if(e->text)target->text.swap(*e->text);
    if(e->write_authored){target->authored_value.swap(*e->authored);target->authored_value_present=e->authored_present;}
    if(e->write_runtime){target->runtime_value.swap(*e->runtime);target->runtime_value_present=e->runtime_present;}
    if(e->state){for(unsigned i=0;i<32;++i)if((uint32_t(target->state)^uint32_t(*e->state))&(uint32_t{1}<<i))++target->identity->state_bit_write_serial[i];target->state=*e->state;}
    for(auto& l:e->links)l.node->children.swap(l.children);
    for(auto* n:e->disconnect){++n->identity->disconnect_serial;n->identity->attached=false;if(n->parent==target)n->parent=nullptr;}
    if(e->reparent)e->child->parent=e->new_parent;
    for(auto& node:e->created)if(!node->node->parent)node->node->parent=target;
    for(auto* n:e->attach)n->identity->attached=target->identity->attached;
    int64_t id=e->adopt_first_id;for(auto* n:e->adopt){n->document_owner=e->domains[e->adopt_domain].owner;n->identity->document_owner=e->domains[e->adopt_domain].owner;if(aether_editable_lease(n))n->style_node_id=id++;}
    for(auto& d:e->domains)if(d.registry){auto* doc=static_cast<aether_doc_s*>(d.root->node.get());doc->editable_nodes.swap(*d.registry);doc->next_style_node_id=d.next_id;}
    e->committed=true;return aether_dom_read_token_v1(target,out);
}
AETHER_API void aether_dom_edit_destroy_v1(aether_dom_edit_t e){if(e&&!e->committed)for(auto& n:e->created)aether_dom_retire_node(n->node.get());delete e;}
