#include "style_revision.h"
#include "../dom/dom_types.h"
#include "../style_runtime/frozen_runtime_metadata.h"
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
bool scopes_equal(const FrozenCustomPropertyScope* a,const FrozenCustomPropertyScope* b)noexcept{
    while(a!=b){
        if(!a||!b||a->local_count()!=b->local_count())return false;
        for(const auto& entry:a->local_entries()) {const auto* other=b->find_local(entry.name());
            if(!other||entry.status()!=other->status()||entry.value()!=other->value())return false;}
        a=a->parent();b=b->parent();
    }
    return true;
}
bool colors_equal(const FrozenColorMetadata& a,const FrozenColorMetadata& b)noexcept{
    if(a.background()!=b.background()||a.text_shadow_count()!=b.text_shadow_count()||a.box_shadow_count()!=b.box_shadow_count())return false;
    for(size_t i=0;i<4;++i)if(a.border(i)!=b.border(i))return false;
    for(size_t i=0;i<a.text_shadow_count();++i)if(a.text_shadow_at(i)!=b.text_shadow_at(i))return false;
    for(size_t i=0;i<a.box_shadow_count();++i)if(a.box_shadow_at(i)!=b.box_shadow_at(i))return false;
    return true;
}
bool measures_equal(const FrozenMeasurePayload& a,const FrozenMeasurePayload& b)noexcept{
    // Byte equality is only used for pointer-free scalar values. Padding can
    // cause a conservative replacement, never a false equivalence.
    if(std::memcmp(&a.scalars(),&b.scalars(),sizeof(MeasureScalarValues)))return false;
    const auto ap=a.background_positions(),bp=b.background_positions();
    const auto az=a.background_sizes(),bz=b.background_sizes();
    if(ap.size()!=bp.size()||az.size()!=bz.size()||a.property_dependencies().size()!=b.property_dependencies().size())return false;
    for(size_t i=0;i<ap.size();++i)if(std::memcmp(&ap[i],&bp[i],sizeof(MeasureBackgroundPosition)))return false;
    for(size_t i=0;i<az.size();++i)if(std::memcmp(&az[i],&bz[i],sizeof(MeasureBackgroundSize)))return false;
    const auto ad=a.property_dependencies(),bd=b.property_dependencies();
    // Preserve even noncanonical imported sequence metadata. A different order
    // conservatively replaces; no assumption of unique imported names is needed.
    for(size_t i=0;i<ad.size();++i)if(ad[i].flags()!=bd[i].flags()||
        !ad[i].name().equals(std::string_view(bd[i].name().c_str(),bd[i].name().size())))return false;
    return true;
}
bool payload_equivalent(const FrozenNodePayload& a,const FrozenNodePayload& b)noexcept{
    return a.origin()==b.origin()&&a.node()==b.node()&&same_owner(a.style(),b.style())&&
        same_owner(a.node_lifetime(),b.node_lifetime())&&same_owner(a.document_lifetime(),b.document_lifetime())&&
        same_owner(a.generated_leaf(),b.generated_leaf())&&a.node_identity()==b.node_identity()&&a.document_identity()==b.document_identity()&&
        a.declared_impact()==b.declared_impact()&&a.declared_inherited_impact()==b.declared_inherited_impact()&&
        a.inherited_change_impact()==b.inherited_change_impact()&&a.measures()&&b.measures()&&
        measures_equal(*a.measures(),*b.measures())&&scopes_equal(a.custom_properties().get(),b.custom_properties().get())&&
        scopes_equal(a.backdrop_custom_properties().get(),b.backdrop_custom_properties().get())&&
        a.color_references()&&b.color_references()&&colors_equal(*a.color_references(),*b.color_references());
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
    std::vector<std::pair<uintptr_t,uint32_t>> impacts,ImmutableArray<FrozenRetiredNode> retired):
    base_(std::move(base)),header_(std::move(header)),stable_order_(stable),captured_mutation_(header_.document_lifetime->mutation_epoch),
    captured_structure_(header_.document_lifetime->structure_epoch),captured_style_tree_(header_.document_lifetime->style_tree_epoch),changes_(std::move(changes)),impacts_(std::move(impacts)),retired_(std::move(retired)){}
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
        std::vector<std::pair<uintptr_t,uint32_t>> impacts;
        for(size_t i=0;i<in.entries.size();++i){const auto& entry=in.entries[i];
            const auto* old=stable?base->directory().find(reinterpret_cast<uintptr_t>(entry.node)):nullptr;
            if(!old||!same_context||!payload_equivalent(*old->payload,*entry.payload))changes.push_back({i,entry.node,entry.payload});
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
        CertifiedRevisionDeltaRef result(new const CertifiedRevisionDelta(base,std::move(header),stable,std::move(changes),std::move(impacts),std::move(retired_nodes)));
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
            for(const auto& change:in.changes_){directory=directory.set(reinterpret_cast<uintptr_t>(change.node),{change.payload,change.position});
                manifest=manifest.set(change.position,{change.node,change.payload});}}
        else {
            std::vector<PayloadDirectory::Entry> nodes;std::vector<PayloadManifest::Entry> order;
            nodes.reserve(in.changes_.size());order.reserve(in.changes_.size());
            for(const auto& change:in.changes_){nodes.push_back({reinterpret_cast<uintptr_t>(change.node),{change.payload,change.position}});
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
