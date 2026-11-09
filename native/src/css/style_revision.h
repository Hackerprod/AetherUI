#pragma once
#include <aether/aether_contextual_style.h>
#include "immutable_style_backing.h"
#include "frozen_measure_read.h"
#include "persistent_index.h"
#include "../diagnostics/work_counters.h"
#include <vector>
class style_revision_s;
namespace aether::style {
using StyleRevisionRef=std::shared_ptr<const style_revision_s>;
struct RevisionEntryInput { aether_node_s* node=nullptr; FrozenNodeRef record; uint32_t impact=0; };
struct RetiredEntryInput { FrozenNodeRef previous_record; uint32_t impact=0; };
class FrozenRevisionEntry {
    aether_node_s* node_=nullptr;
    FrozenNodeRef record_;
    FrozenNodePayloadRef payload_;
    uint32_t impact_=0;
public:
    explicit FrozenRevisionEntry(const RevisionEntryInput& in):node_(in.node),record_(in.record),
        payload_(in.record?in.record->payload_owner():FrozenNodePayloadRef{}),impact_(in.impact){}
    FrozenRevisionEntry(aether_node_s* node,FrozenNodePayloadRef payload,uint32_t impact):node_(node),payload_(std::move(payload)),impact_(impact){}
    aether_node_s* node()const noexcept{return node_;}
    const FrozenNodeRef& record()const noexcept{return record_;} // Legacy-import owner, never required for live payload entries.
    const FrozenNodePayloadRef& payload()const noexcept{return payload_;}
    uint32_t impact()const noexcept{return impact_;}
};
class FrozenRetiredNode {
    aether_node_s* const node_;
    const std::shared_ptr<node_identity_s> node_lifetime_;
    const std::shared_ptr<aether_node_s> generated_leaf_;
    const std::shared_ptr<EditableNodeStorage> editable_lease_;
    const uint32_t impact_;
public:
    explicit FrozenRetiredNode(const FrozenNodeRef& record):FrozenRetiredNode(record->payload_owner(),0){}
    FrozenRetiredNode(const FrozenNodePayloadRef& payload,uint32_t impact):node_(payload->node()),
        node_lifetime_(payload->node_lifetime()),generated_leaf_(payload->generated_leaf()),editable_lease_(payload->editable_lease()),impact_(impact){}
    aether_node_s* node()const noexcept{return node_;}
    const auto& node_lifetime()const noexcept{return node_lifetime_;}
    const auto& generated_leaf()const noexcept{return generated_leaf_;}
    uint32_t impact()const noexcept{return impact_;}
};
struct StyleRevisionInput {
    bool contextual=true;
    uint64_t generation=0,document_identity=0,input_epoch=0,structure_epoch=0,style_tree_epoch=0;
    aether_style_environment_t environment{};
    FrozenDocumentRef document;
    std::shared_ptr<node_identity_s> document_lifetime;
    std::shared_ptr<EditableNodeStorage> document_lease;
    BackingRef backing;
    std::vector<RevisionEntryInput> entries;
    std::vector<RetiredEntryInput> retired;
};
struct PayloadRevisionEntry { aether_node_s* node=nullptr; FrozenNodePayloadRef payload; uint32_t impact=0; };
// Committed input is deliberately independent from style payload equality.
// A state bit with no matching rule updates only this persistent directory leaf.
struct PayloadDirectoryEntry {
    FrozenNodePayloadRef payload;
    size_t position=0;
    int32_t committed_state=0;
    bool has_committed_state=false; // Imported snapshots cannot certify input history.
};
struct PayloadManifestEntry { aether_node_s* node=nullptr; FrozenNodePayloadRef payload; };
using PayloadDirectory=PersistentIndex<PayloadDirectoryEntry>;
using PayloadManifest=PersistentIndex<PayloadManifestEntry>;
// Complete freshly prepared output, never an unchecked unchanged-node list.
// Certification traverses every entry; only its private result can enter delta freeze.
struct FullPayloadRevisionInput {
    bool contextual=true;
    uint64_t generation=0,document_identity=0,input_epoch=0,structure_epoch=0,style_tree_epoch=0;
    aether_style_environment_t environment{};
    FrozenDocumentRef document;
    std::shared_ptr<node_identity_s> document_lifetime;
    std::shared_ptr<EditableNodeStorage> document_lease;
    std::vector<PayloadRevisionEntry> entries;
    std::vector<PayloadRevisionEntry> retired; // Full cross-document import only; never dereference retired nodes.
};
class StateRevisionPreparation;
class CertifiedRevisionDelta;
using CertifiedRevisionDeltaRef=std::shared_ptr<const CertifiedRevisionDelta>;
aether_status_t certify_full_payload_revision(const StyleRevisionRef&,const FullPayloadRevisionInput&,CertifiedRevisionDeltaRef&) noexcept;
aether_status_t freeze_certified_revision(const CertifiedRevisionDeltaRef&,StyleRevisionRef&) noexcept;
aether_status_t freeze_style_revision(const StyleRevisionInput&,StyleRevisionRef&) noexcept;
class CertifiedRevisionDelta {
    mutable std::weak_ptr<const CertifiedRevisionDelta> self_;
    struct Change { size_t position; aether_node_s* node; FrozenNodePayloadRef payload; int32_t state; };
    const StyleRevisionRef base_;
    const FullPayloadRevisionInput header_; // entries deliberately empty: no retained N-vector on stable-order delta.
    const bool stable_order_;
    const uint64_t captured_mutation_,captured_structure_,captured_style_tree_;
    const std::vector<Change> changes_;
    const std::vector<std::pair<uintptr_t,int32_t>> state_changes_;
    const std::vector<std::pair<uintptr_t,uint32_t>> impacts_;
    const ImmutableArray<FrozenRetiredNode> retired_;
    CertifiedRevisionDelta(StyleRevisionRef,FullPayloadRevisionInput,bool,std::vector<Change>,
        std::vector<std::pair<uintptr_t,int32_t>>,std::vector<std::pair<uintptr_t,uint32_t>>,ImmutableArray<FrozenRetiredNode>);
    friend aether_status_t certify_full_payload_revision(const StyleRevisionRef&,const FullPayloadRevisionInput&,CertifiedRevisionDeltaRef&) noexcept;
    friend aether_status_t freeze_certified_revision(const CertifiedRevisionDeltaRef&,StyleRevisionRef&) noexcept;
    friend class ::style_revision_s;
    friend class StateRevisionPreparation;
public:
    size_t changed_payloads()const noexcept{return changes_.size();}
    bool stable_order()const noexcept{return stable_order_;}
};
class RevisionEntriesView {
    const style_revision_s* revision_=nullptr;
    bool changes_=false;
public:
    RevisionEntriesView() noexcept=default;
    RevisionEntriesView(const style_revision_s* revision,bool changes)noexcept:revision_(revision),changes_(changes){}
    size_t size()const noexcept;
    bool empty()const noexcept{return size()==0;}
    FrozenRevisionEntry operator[](size_t)const noexcept;
    FrozenRevisionEntry at(size_t i)const {if(i>=size())throw std::out_of_range("revision entry");return (*this)[i];}
    class Iterator {
        const style_revision_s* revision_;
        bool changes_;
        size_t index_;
    public:
        Iterator(const style_revision_s* revision,bool changes,size_t index):revision_(revision),changes_(changes),index_(index){}
        FrozenRevisionEntry operator*()const noexcept{return RevisionEntriesView(revision_,changes_)[index_];}
        Iterator& operator++()noexcept{++index_;return *this;}
        bool operator==(const Iterator& b)const noexcept{return revision_==b.revision_&&changes_==b.changes_&&index_==b.index_;}
    };
    Iterator begin()const noexcept{return {revision_,changes_,0};}
    Iterator end()const noexcept{return {revision_,changes_,size()};}
};
}
class style_revision_s final {
    mutable std::weak_ptr<const style_revision_s> self_;
    const bool contextual_;
    const uint64_t generation_,document_identity_,input_epoch_,structure_epoch_,style_tree_epoch_;
    const aether_style_environment_t environment_;
    const aether::style::FrozenDocumentRef document_;
    const std::shared_ptr<node_identity_s> document_lifetime_;
    const std::shared_ptr<EditableNodeStorage> document_lease_;
    // Present only for imported bound-record snapshots. New runtime revisions do
    // not materialize legacy backing; contextual maps bind the payload directory.
    const aether::style::BackingRef backing_;
    const aether::style::ImmutableArray<aether::style::FrozenRevisionEntry> entries_,changes_;
    const aether::style::ImmutableArray<aether::style::FrozenRetiredNode> retired_nodes_;
    const aether::style::PayloadDirectory directory_;
    const aether::style::PayloadManifest manifest_;
    const aether::style::PersistentIndex<uint32_t> impacts_;
    explicit style_revision_s(const aether::style::StyleRevisionInput&);
    style_revision_s(const aether::style::CertifiedRevisionDelta&,aether::style::PayloadDirectory,aether::style::PayloadManifest,
        aether::style::PersistentIndex<uint32_t>);
    friend aether_status_t aether::style::freeze_style_revision(const aether::style::StyleRevisionInput&,aether::style::StyleRevisionRef&) noexcept;
    friend aether_status_t aether::style::freeze_certified_revision(const aether::style::CertifiedRevisionDeltaRef&,aether::style::StyleRevisionRef&) noexcept;
    friend class aether::style::RevisionEntriesView;
public:
    bool authentic_owner(const aether::style::StyleRevisionRef& owner)const noexcept{
        const auto actual=self_.lock();return actual==owner&&!actual.owner_before(owner)&&!owner.owner_before(actual);
    }
    bool contextual()const noexcept{return contextual_;}
    uint64_t generation()const noexcept{return generation_;}
    uint64_t document_identity()const noexcept{return document_identity_;}
    uint64_t input_epoch()const noexcept{return input_epoch_;}
    uint64_t structure_epoch()const noexcept{return structure_epoch_;}
    uint64_t style_tree_epoch()const noexcept{return style_tree_epoch_;}
    const auto& environment()const noexcept{return environment_;}
    const auto& document()const noexcept{return document_;}
    const auto& document_lifetime()const noexcept{return document_lifetime_;}
    const auto& backing()const noexcept{return backing_;}
    const auto& directory()const noexcept{return directory_;}
    const auto& manifest()const noexcept{return manifest_;}
    auto entries()const noexcept{return aether::style::RevisionEntriesView(this,false);}
    auto changes()const noexcept{return aether::style::RevisionEntriesView(this,true);}
    auto retired_nodes()const noexcept{return retired_nodes_.view();}
};
namespace aether::style {
inline size_t RevisionEntriesView::size()const noexcept{return revision_ ? revision_->manifest_.size()+(changes_?revision_->retired_nodes_.size():0):0;}
inline FrozenRevisionEntry RevisionEntriesView::operator[](size_t i)const noexcept {
    assert(revision_&&i<size());
    if(revision_->backing_)return changes_?revision_->changes_.view()[i]:revision_->entries_.view()[i];
    if(i<revision_->manifest_.size()) {const auto& item=*revision_->manifest_.find(i);const auto* impact=revision_->impacts_.find(i);
        return {item.node,item.payload,impact?*impact:0};}
    const auto& item=revision_->retired_nodes_.view()[i-revision_->manifest_.size()];return {item.node(),{},item.impact()};
}
class BoundNodeView {
    StyleRevisionRef revision_;
    FrozenNodeRef legacy_;
    FrozenNodePayloadRef payload_;
    BoundNodeView(StyleRevisionRef revision,FrozenNodePayloadRef payload,FrozenNodeRef legacy={})noexcept:
        revision_(std::move(revision)),legacy_(std::move(legacy)),payload_(std::move(payload)){}
public:
    BoundNodeView()noexcept=default;
    static BoundNodeView find(StyleRevisionRef revision,const aether_node_s* node)noexcept{
        if(!revision||!revision->authentic_owner(revision))return {};const auto* entry=revision->directory().find(reinterpret_cast<uintptr_t>(node));
        if(!entry)return {};auto legacy=revision->backing()?revision->backing()->find_owner(node):FrozenNodeRef{};
        return {std::move(revision),entry->payload,std::move(legacy)};
    }
    static BoundNodeView from_legacy(FrozenNodeRef record)noexcept{
        if(!record)return {};auto payload=record->payload_owner();return {{},std::move(payload),std::move(record)};
    }
    explicit operator bool()const noexcept{return bool(payload_);}
    const auto& revision_owner()const noexcept{return revision_;}
    const auto& payload_owner()const noexcept{return payload_;}
    const auto& legacy_record()const noexcept{return legacy_;}
    FrozenDocumentRef document_owner()const noexcept{return revision_?revision_->document():legacy_&&legacy_->measures()?legacy_->measures()->document_owner():FrozenDocumentRef{};}
    const FrozenDocumentMeasureContext& document()const noexcept{return *document_owner();}
    bool has_measures()const noexcept{return payload_&&payload_->measures()&&bool(document_owner());}
    ComputedStyleReadView style_view()const noexcept{return payload_->style_view();}
    MeasuresReadView measures_view()const noexcept{return {*payload_->measures(),document()};}
    uint64_t generation()const noexcept{return document().values().generation;}
    aether_status_t export_record(FrozenNodeRef& out)const noexcept{
        if(!payload_)return AETHER_ERROR_INVALID_STATE;
        if(legacy_){out=legacy_;return AETHER_OK;}
        try {auto measures=std::make_shared<const FrozenMeasures>(payload_->measures(),document_owner());
            FrozenNodeRef result(new const FrozenNodeRecord(payload_,std::move(measures)));
            diagnostics::count_work(diagnostics::WorkCounter::revision_records_created);out=std::move(result);return AETHER_OK;}
        catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}
    }
};
}
