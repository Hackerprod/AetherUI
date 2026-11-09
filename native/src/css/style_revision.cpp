#include "style_revision.h"
#include "../dom/dom_types.h"
#include "../style_runtime/frozen_runtime_metadata.h"
#include "../style_runtime/state_semantics.h"
#include <climits>
#include <unordered_set>
#include <cstring>
using namespace aether::style;
namespace {
template<class T> bool same_owner(const std::shared_ptr<T>& a,const std::shared_ptr<T>& b)noexcept{
    return a==b&&!a.owner_before(b)&&!b.owner_before(a);
}
PayloadDirectory import_directory(const StyleRevisionInput& in){
    std::vector<PayloadDirectory::Entry> entries;entries.reserve(in.entries.size());
    for(size_t i=0;i<in.entries.size();++i)entries.push_back({reinterpret_cast<uintptr_t>(in.entries[i].node),{in.entries[i].record->payload_owner(),i}});
    return PayloadDirectory::build(std::move(entries));
}
PayloadManifest import_manifest(const StyleRevisionInput& in){
    std::vector<PayloadManifest::Entry> entries;entries.reserve(in.entries.size());
    for(size_t i=0;i<in.entries.size();++i)entries.push_back({i,{in.entries[i].node,in.entries[i].record->payload_owner()}});
    return PayloadManifest::build(std::move(entries));
}
bool payload_equivalent(const FrozenNodePayload& a,const FrozenNodePayload& b)noexcept {
    return node_semantically_equal(a,b);
}
bool context_equal(const DocumentMeasureValues& a,const DocumentMeasureValues& b)noexcept{
    return a.identity==b.identity&&a.viewport_width==b.viewport_width&&a.viewport_height==b.viewport_height&&
        a.initial_font_size==b.initial_font_size&&a.root_font_size==b.root_font_size;
}
}
style_revision_s::style_revision_s(const StyleRevisionInput& in):
    contextual_(in.contextual),generation_(in.generation),document_identity_(in.document_identity),input_epoch_(in.input_epoch),
    structure_epoch_(in.structure_epoch),style_tree_epoch_(in.style_tree_epoch),environment_(in.environment),document_(in.document),
    document_lifetime_(in.document_lifetime),backing_(in.backing),
    entries_(ImmutableArray<FrozenRevisionEntry>::generate(in.entries.size(),[&](size_t i){return FrozenRevisionEntry(in.entries[i]);})),
    changes_(ImmutableArray<FrozenRevisionEntry>::generate(in.entries.size()+in.retired.size(),[&](size_t i){
        if(i<in.entries.size())return FrozenRevisionEntry(in.entries[i]);const auto& old=in.retired[i-in.entries.size()];
        return FrozenRevisionEntry(RevisionEntryInput{old.previous_record->node(),{},old.impact});})),
    retired_nodes_(ImmutableArray<FrozenRetiredNode>::generate(in.retired.size(),[&](size_t i){return FrozenRetiredNode(in.retired[i].previous_record);})),
    directory_(import_directory(in)),manifest_(import_manifest(in)),impacts_(){}
style_revision_s::style_revision_s(const CertifiedRevisionDelta& in,PayloadDirectory directory,PayloadManifest manifest,PersistentIndex<uint32_t> impacts):
    contextual_(in.header_.contextual),generation_(in.header_.generation),document_identity_(in.header_.document_identity),
    input_epoch_(in.header_.input_epoch),structure_epoch_(in.header_.structure_epoch),style_tree_epoch_(in.header_.style_tree_epoch),
    environment_(in.header_.environment),document_(in.header_.document),document_lifetime_(in.header_.document_lifetime),
    backing_(),entries_(),changes_(),retired_nodes_(in.retired_),directory_(std::move(directory)),manifest_(std::move(manifest)),impacts_(std::move(impacts)){}
namespace aether::style {
CertifiedRevisionDelta::CertifiedRevisionDelta(StyleRevisionRef base,FullPayloadRevisionInput header,bool stable,std::vector<Change> changes,
    std::vector<std::pair<uintptr_t,int32_t>> states,std::vector<std::pair<uintptr_t,uint32_t>> impacts,ImmutableArray<FrozenRetiredNode> retired):
    base_(std::move(base)),header_(std::move(header)),stable_order_(stable),captured_mutation_(header_.document_lifetime->mutation_epoch),
    captured_structure_(header_.document_lifetime->structure_epoch),captured_style_tree_(header_.document_lifetime->style_tree_epoch),changes_(std::move(changes)),state_changes_(std::move(states)),impacts_(std::move(impacts)),retired_(std::move(retired)){}
aether_status_t certify_full_payload_revision(const StyleRevisionRef& base,const FullPayloadRevisionInput& in,CertifiedRevisionDeltaRef& out)noexcept{
    if(!in.document||!in.document_lifetime||!in.generation||!in.document_lifetime->alive||!in.document_lifetime->owner||
        in.document_identity!=reinterpret_cast<uintptr_t>(in.document_lifetime.get())||in.document->values().identity!=in.document_identity||
        in.document->values().generation!=in.generation||in.entries.size()>INT32_MAX)return AETHER_ERROR_INVALID_STATE;
    if(!same_owner(static_cast<const aether_node_s*>(in.document_lifetime->owner)->identity,in.document_lifetime))return AETHER_ERROR_INVALID_STATE;
    if(base&&(!base->authentic_owner(base)||!same_owner(base->document_lifetime(),in.document_lifetime)||base->document_identity()!=in.document_identity||in.generation<=base->generation()))return AETHER_ERROR_INVALID_STATE;
    if(in.input_epoch!=in.document_lifetime->mutation_epoch||in.structure_epoch!=in.document_lifetime->structure_epoch||
        in.style_tree_epoch!=in.document_lifetime->style_tree_epoch+1|| (base&&!in.retired.empty()))return AETHER_ERROR_INVALID_STATE;
    try {
        std::unordered_set<aether_node_s*> seen;seen.reserve(in.entries.size());
        bool stable=base&&base->entries().size()==in.entries.size();
        for(size_t i=0;i<in.entries.size();++i){
            const auto& entry=in.entries[i];const auto& p=entry.payload;
            if(!entry.node||!p||p->node()!=entry.node||p->origin()!=RecordOrigin::runtime||!seen.insert(entry.node).second||
                !p->node_lifetime()||!p->node_lifetime()->alive||p->node_lifetime()->owner!=entry.node||!same_owner(entry.node->identity,p->node_lifetime())||
                !same_owner(p->document_lifetime(),in.document_lifetime)||p->document_identity()!=in.document_identity||!p->measures()||
                !p->custom_properties()||!p->color_references())return AETHER_ERROR_INVALID_STATE;
            if(stable&&base->manifest().find(i)->node!=entry.node)stable=false;
        }
        const bool same_context=base&&context_equal(base->document()->values(),in.document->values())&&
            std::memcmp(&base->environment(),&in.environment,sizeof(in.environment))==0;
        std::vector<CertifiedRevisionDelta::Change> changes;
        std::vector<std::pair<uintptr_t,int32_t>> states;
        std::vector<std::pair<uintptr_t,uint32_t>> impacts;
        for(size_t i=0;i<in.entries.size();++i){const auto& entry=in.entries[i];
            const auto* old=stable?base->directory().find(reinterpret_cast<uintptr_t>(entry.node)):nullptr;
            if(!old||!same_context||!payload_equivalent(*old->payload,*entry.payload))changes.push_back({i,entry.node,entry.payload,entry.node->state});
            else if(!old->has_committed_state||old->committed_state!=entry.node->state)
                states.push_back({reinterpret_cast<uintptr_t>(entry.node),entry.node->state});
            if(entry.impact)impacts.push_back({i,entry.impact});
        }
        std::vector<FrozenNodePayloadRef> retired;
        if(base)for(const auto& item:base->manifest().entries())if(!seen.count(item.second.node))retired.push_back(item.second.payload);
        if(!base)for(const auto& old:in.retired){const auto& p=old.payload;
            if(!p||p->node()!=old.node||p->origin()!=RecordOrigin::runtime||!p->node_lifetime()||
                p->node_lifetime()->owner!=old.node||p->node_identity()!=reinterpret_cast<uintptr_t>(p->node_lifetime().get())||
                seen.count(old.node)||!seen.insert(old.node).second)return AETHER_ERROR_INVALID_STATE;
            retired.push_back(p);
        }
        if(retired.size()>static_cast<size_t>(INT32_MAX)-in.entries.size())return AETHER_ERROR_INVALID_STATE;
        constexpr uint32_t all=AETHER_STYLE_IMPACT_LAYOUT|AETHER_STYLE_IMPACT_PAINT|AETHER_STYLE_IMPACT_HITTEST|
            AETHER_STYLE_IMPACT_COMPOSITE|AETHER_STYLE_IMPACT_TEXT_LAYOUT|AETHER_STYLE_IMPACT_CURSOR|AETHER_STYLE_IMPACT_INHERITED;
        auto retired_nodes=ImmutableArray<FrozenRetiredNode>::generate(retired.size(),[&](size_t i){return FrozenRetiredNode(retired[i],all);});
        FullPayloadRevisionInput header;header.contextual=in.contextual;header.generation=in.generation;header.document_identity=in.document_identity;
        header.input_epoch=in.input_epoch;header.structure_epoch=in.structure_epoch;header.style_tree_epoch=in.style_tree_epoch;
        header.environment=in.environment;header.document=in.document;header.document_lifetime=in.document_lifetime;
        CertifiedRevisionDeltaRef result(new const CertifiedRevisionDelta(base,std::move(header),stable,std::move(changes),std::move(states),std::move(impacts),std::move(retired_nodes)));
        result->self_=result;out=std::move(result);return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}
}
aether_status_t freeze_certified_revision(const CertifiedRevisionDeltaRef& certificate,StyleRevisionRef& out)noexcept{
    if(!certificate)return AETHER_ERROR_INVALID_STATE;
    const auto& in=*certificate;
    if(!same_owner(in.self_.lock(),certificate))return AETHER_ERROR_INVALID_STATE;
    if(!in.header_.document_lifetime->alive||in.header_.document_lifetime->mutation_epoch!=in.captured_mutation_||
        in.header_.document_lifetime->structure_epoch!=in.captured_structure_||
        in.header_.document_lifetime->style_tree_epoch!=in.captured_style_tree_)return AETHER_ERROR_INVALID_STATE;
    try {
        PayloadDirectory directory;PayloadManifest manifest;
        if(in.stable_order_){directory=in.base_->directory();manifest=in.base_->manifest();
            for(const auto& change:in.changes_){directory=directory.set(reinterpret_cast<uintptr_t>(change.node),{change.payload,change.position,change.state,true});
                manifest=manifest.set(change.position,{change.node,change.payload});}
            for(const auto& change:in.state_changes_) {
                const auto* old=directory.find(change.first);
                if(!old)return AETHER_ERROR_INVALID_STATE;
                auto updated=*old;updated.committed_state=change.second;updated.has_committed_state=true;
                directory=directory.set(change.first,updated);
            }
        }
        else {
            std::vector<PayloadDirectory::Entry> nodes;std::vector<PayloadManifest::Entry> order;
            nodes.reserve(in.changes_.size());order.reserve(in.changes_.size());
            for(const auto& change:in.changes_){nodes.push_back({reinterpret_cast<uintptr_t>(change.node),{change.payload,change.position,change.state,true}});
                order.push_back({change.position,{change.node,change.payload}});}
            directory=PayloadDirectory::build(std::move(nodes));manifest=PayloadManifest::build(std::move(order));
        }
        auto impacts=PersistentIndex<uint32_t>::build(in.impacts_);
        StyleRevisionRef result(new const style_revision_s(in,std::move(directory),std::move(manifest),std::move(impacts)));
        result->self_=result;out=std::move(result);return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}
}
aether_status_t freeze_style_revision(const StyleRevisionInput& in, StyleRevisionRef& out) noexcept {
    if (!in.document || !in.backing || !in.document_lifetime || !in.generation ||
        !in.document_lifetime->alive || !in.document_lifetime->owner ||
        in.document_identity != reinterpret_cast<uintptr_t>(in.document_lifetime.get()) ||
        in.document->values().identity != in.document_identity || in.document->values().generation != in.generation ||
        in.entries.size() != in.backing->size() || in.entries.size() > INT32_MAX ||
        in.retired.size() > static_cast<size_t>(INT32_MAX) - in.entries.size())
        return AETHER_ERROR_INVALID_STATE;
    // Liveness is checked before touching the DOM observer's raw owner.
    if (!same_owner(static_cast<const aether_node_s*>(in.document_lifetime->owner)->identity,in.document_lifetime))
        return AETHER_ERROR_INVALID_STATE;
    try {
        std::unordered_set<const aether_node_s*> seen;
        seen.reserve(in.entries.size() + in.retired.size());
        for (const auto& entry : in.entries) {
            const auto& r = entry.record;
            if (!entry.node || !r || r->node() != entry.node || !seen.insert(entry.node).second ||
                !same_owner(in.backing->find_owner(entry.node), r) || r->origin() != RecordOrigin::runtime ||
                !r->node_lifetime() || !r->node_lifetime()->alive || r->node_lifetime()->owner != entry.node ||
                !same_owner(r->document_lifetime(), in.document_lifetime) || r->document_identity() != in.document_identity ||
                !r->measures() || !same_owner(r->measures()->document_owner(), in.document))
                return AETHER_ERROR_INVALID_STATE;
            if (!same_owner(entry.node->identity,r->node_lifetime())) return AETHER_ERROR_INVALID_STATE;
        }
        for (const auto& retired : in.retired) {
            const auto& r = retired.previous_record;
            // A previous factory-produced record supplies provenance. Never
            // dereference retired raw DOM: normal nodes may already be dead.
            if (!r || !r->node() || r->origin() != RecordOrigin::runtime || !r->node_lifetime() ||
                r->node_lifetime()->owner != r->node() ||
                r->node_identity() != reinterpret_cast<uintptr_t>(r->node_lifetime().get()) ||
                (r->generated_leaf() && r->generated_leaf().get() != r->node()) ||
                in.backing->find(r->node()) || !seen.insert(r->node()).second)
                return AETHER_ERROR_INVALID_STATE;
        }
        StyleRevisionRef result(new const style_revision_s(in));
        result->self_=result;out = std::move(result);
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
}
