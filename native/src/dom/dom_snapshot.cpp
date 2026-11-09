#include "dom_types.h"
#include <aether/aether_dom_sync.h>
#include <unordered_set>
struct aether_dom_node_lease_s { std::shared_ptr<EditableNodeStorage> storage; std::shared_ptr<node_identity_s> identity; };
struct DomSnapshotNode {
    std::shared_ptr<EditableNodeStorage> storage;
    std::shared_ptr<node_identity_s> identity;
    aether_dom_snapshot_node_v1_t values{};
    std::string tag,text,authored_value,runtime_value;
    std::vector<std::pair<std::string,std::string>> attributes;
    std::vector<size_t> children;
};
struct aether_dom_snapshot_s { aether_dom_token_v1_t token{}; std::vector<DomSnapshotNode> nodes; };
namespace {
aether_dom_string_v1_t view(const std::string& s) noexcept {return {s.data(),s.size()};}
bool same(const aether_dom_token_v1_t& a,const aether_dom_token_v1_t& b) noexcept {
    return a.owner_identity==b.owner_identity&&a.observation_serial==b.observation_serial&&a.input_epoch==b.input_epoch&&a.structure_epoch==b.structure_epoch&&a.sticky_status==b.sticky_status;
}
}
AETHER_API uint32_t aether_dom_sync_version(){return AETHER_DOM_SYNC_VERSION;}
AETHER_API aether_status_t aether_dom_retain_node_v1(aether_node_t node,aether_dom_node_lease_t* out) {
    if(out)*out=nullptr;if(!node||!out)return AETHER_ERROR_NULL_ARG;
    if(aether_dom_access(node)!=AETHER_OK)return AETHER_ERROR_INVALID_STATE;
    auto lease=aether_editable_lease(node);if(!lease)return AETHER_ERROR_INVALID_STATE;
    try{auto result=std::make_unique<aether_dom_node_lease_s>();result->storage=std::move(lease);result->identity=node->identity;*out=result.release();return AETHER_OK;}
    catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API void aether_dom_node_lease_destroy_v1(aether_dom_node_lease_t lease){delete lease;}
AETHER_API aether_status_t aether_dom_node_lease_get_v1(aether_dom_node_lease_t lease,aether_node_t* out){
    if(out)*out=nullptr;if(!lease||!out)return AETHER_ERROR_NULL_ARG;*out=lease->storage->node.get();return AETHER_OK;
}
AETHER_API aether_status_t aether_dom_read_token_v1(aether_node_t node,aether_dom_token_v1_t* out){
    if(out)*out={};if(!node||!out)return AETHER_ERROR_NULL_ARG;
    auto owner=node->document_owner.lock();if(!owner||owner->owner_thread!=std::this_thread::get_id())return AETHER_ERROR_INVALID_STATE;
    if(!owner->alive||!node->identity->alive)return AETHER_ERROR_DISPOSED;
    auto root=owner->document.lock();if(!root)return AETHER_ERROR_DISPOSED;
    const auto identity=root->node->identity;
    *out={aether_node_identity(root->node.get()),owner->observation_serial,identity->mutation_epoch,identity->structure_epoch,owner->sticky_status};
    return owner->sticky_status;
}
AETHER_API aether_status_t aether_dom_acquire_snapshot_v1(aether_doc_t doc,const aether_dom_node_lease_t* known,size_t known_count,aether_dom_snapshot_t* out){
    if(out)*out=nullptr;if(!doc||!out||(known_count&&!known))return AETHER_ERROR_NULL_ARG;
    aether_dom_token_v1_t token{};auto status=aether_dom_read_token_v1(doc,&token);if(status!=AETHER_OK)return status;
    try{
        auto snapshot=std::make_unique<aether_dom_snapshot_s>();snapshot->token=token;
        std::vector<std::shared_ptr<EditableNodeStorage>> leases;leases.reserve(doc->editable_nodes.size()+known_count+1);
        auto root=aether_editable_lease(doc);if(!root)return AETHER_ERROR_INVALID_STATE;leases.push_back(std::move(root));
        for(const auto& [node,storage]:doc->editable_nodes)leases.push_back(storage);
        for(size_t i=0;i<known_count;++i){if(!known[i])return AETHER_ERROR_NULL_ARG;auto owner=known[i]->storage->node->document_owner.lock();if(owner&&owner->owner_thread!=std::this_thread::get_id())return AETHER_ERROR_INVALID_STATE;leases.push_back(known[i]->storage);}
        std::unordered_map<const aether_node_s*,size_t> positions;
        snapshot->nodes.reserve(leases.size());
        for(auto& storage:leases){auto* node=storage->node.get();if(positions.contains(node))continue;positions.emplace(node,snapshot->nodes.size());DomSnapshotNode item;item.storage=std::move(storage);item.identity=node->identity;snapshot->nodes.push_back(std::move(item));}
        for(auto& item:snapshot->nodes){auto* node=item.storage->node.get();auto& v=item.values;
            v.node=node;v.identity=aether_node_identity(node);v.disconnect_serial=node->identity->disconnect_serial;
            if(auto owner=node->document_owner.lock())if(auto pin=owner->document.lock())v.owner_identity=aether_node_identity(pin->node.get());
            v.type=node->type;v.state=node->state;v.alive=node->identity->alive;v.attached=node->identity->attached;v.style_node_id=node->style_node_id;
            auto parent=positions.find(node->parent);v.parent_index=parent==positions.end()?-1:static_cast<int64_t>(parent->second);
            item.tag=node->tag_name;item.text=node->text;item.authored_value=node->authored_value;item.runtime_value=node->runtime_value;
            v.authored_value_present=node->authored_value_present;v.runtime_value_present=node->runtime_value_present;
            std::copy(node->identity->state_bit_write_serial.begin(),node->identity->state_bit_write_serial.end(),v.state_bit_write_serial);
            for(const auto& [name,value]:node->attributes){if(name=="value"){if(node->authored_value_present)item.attributes.emplace_back(name,node->authored_value);}else item.attributes.emplace_back(name,value);}
            for(auto* child:node->children){auto found=positions.find(child);if(found!=positions.end())item.children.push_back(found->second);}
            v.attribute_count=item.attributes.size();v.child_count=item.children.size();
        }
        aether_dom_token_v1_t current{};status=aether_dom_read_token_v1(doc,&current);if(status!=AETHER_OK||!same(token,current))return AETHER_ERROR_INVALID_STATE;
        *out=snapshot.release();return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API void aether_dom_snapshot_destroy_v1(aether_dom_snapshot_t snapshot){delete snapshot;}
AETHER_API aether_status_t aether_dom_snapshot_token_v1(aether_dom_snapshot_t snapshot,aether_dom_token_v1_t* out){if(out)*out={};if(!snapshot||!out)return AETHER_ERROR_NULL_ARG;*out=snapshot->token;return AETHER_OK;}
AETHER_API aether_status_t aether_dom_snapshot_count_v1(aether_dom_snapshot_t snapshot,size_t* out){if(out)*out=0;if(!snapshot||!out)return AETHER_ERROR_NULL_ARG;*out=snapshot->nodes.size();return AETHER_OK;}
AETHER_API aether_status_t aether_dom_snapshot_node_v1(aether_dom_snapshot_t snapshot,size_t index,aether_dom_snapshot_node_v1_t* out){if(out)*out={};if(!snapshot||!out)return AETHER_ERROR_NULL_ARG;if(index>=snapshot->nodes.size())return AETHER_ERROR_INVALID_STATE;const auto& n=snapshot->nodes[index];*out=n.values;out->tag=view(n.tag);out->text=view(n.text);out->authored_value=view(n.authored_value);out->runtime_value=view(n.runtime_value);return AETHER_OK;}
AETHER_API aether_status_t aether_dom_snapshot_attribute_v1(aether_dom_snapshot_t snapshot,size_t node,size_t attribute,aether_dom_attribute_v1_t* out){if(out)*out={};if(!snapshot||!out)return AETHER_ERROR_NULL_ARG;if(node>=snapshot->nodes.size()||attribute>=snapshot->nodes[node].attributes.size())return AETHER_ERROR_INVALID_STATE;const auto& n=snapshot->nodes[node].attributes[attribute];*out={view(n.first),view(n.second)};return AETHER_OK;}
AETHER_API aether_status_t aether_dom_snapshot_child_v1(aether_dom_snapshot_t snapshot,size_t node,size_t child,size_t* out){if(out)*out=0;if(!snapshot||!out)return AETHER_ERROR_NULL_ARG;if(node>=snapshot->nodes.size()||child>=snapshot->nodes[node].children.size())return AETHER_ERROR_INVALID_STATE;*out=snapshot->nodes[node].children[child];return AETHER_OK;}
