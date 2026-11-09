#pragma once
#include "immutable_style_backing.h"
#include "style_revision.h"
class style_revision_s;
namespace aether::style {
enum class MapMode { legacy_partial, legacy_complete, contextual };
// Complete versions validate exact backing/provenance against the final revision owner.
// Tokens must be authentic DOM controls. A dead token cannot dereference its owner.
// Partial legacy may retain heterogeneous records and previous document epochs.
struct MapVersionInput {
    BackingRef backing = {};
    MapMode mode = MapMode::legacy_partial;
    std::shared_ptr<const style_revision_s> revision = {};
    std::shared_ptr<node_identity_s> document_lifetime = {};
    uint64_t document_identity = 0;
    uint64_t generation = 0;
    uint64_t structure_epoch = 0;
    uint64_t input_epoch = 0;
    uint64_t style_tree_epoch = 0;
};
class MapVersion;
using MapVersionRef = std::shared_ptr<const MapVersion>;
aether_status_t make_map_version(const MapVersionInput&, MapVersionRef&) noexcept;
aether_status_t materialize_legacy_revision_map(const StyleRevisionRef&, MapVersionRef&) noexcept;
class MapVersion {
    const BackingRef backing_;
    const MapMode mode_;
    const std::shared_ptr<const style_revision_s> revision_;
    const std::shared_ptr<node_identity_s> document_lifetime_;
    // Required even for an empty legacy backing: pure epoch queries read DOM.
    const std::shared_ptr<EditableNodeStorage> document_lease_;
    const uint64_t document_identity_;
    const uint64_t generation_;
    const uint64_t structure_epoch_;
    const uint64_t input_epoch_;
    const uint64_t style_tree_epoch_;
    explicit MapVersion(const MapVersionInput&);
    friend aether_status_t make_map_version(const MapVersionInput&, MapVersionRef&) noexcept;
    friend aether_status_t materialize_legacy_revision_map(const StyleRevisionRef&, MapVersionRef&) noexcept;
public:
    const BackingRef& backing() const noexcept { return backing_; }
    const MapMode& mode() const noexcept { return mode_; }
    const std::shared_ptr<const style_revision_s>& revision() const noexcept { return revision_; }
    const std::shared_ptr<node_identity_s>& document_lifetime() const noexcept { return document_lifetime_; }
    const uint64_t& document_identity() const noexcept { return document_identity_; }
    const uint64_t& generation() const noexcept { return generation_; }
    const uint64_t& structure_epoch() const noexcept { return structure_epoch_; }
    const uint64_t& input_epoch() const noexcept { return input_epoch_; }
    const uint64_t& style_tree_epoch() const noexcept { return style_tree_epoch_; }
    size_t size()const noexcept{return backing_?backing_->size():revision_?revision_->directory().size():0;}
    BoundNodeView lookup(const aether_node_s* node) const noexcept {
        if(backing_) {
            if(revision_&&revision_->backing()==backing_)return BoundNodeView::find(revision_,node);
            return BoundNodeView::from_legacy(backing_->find_owner(node));
        }
        return revision_?BoundNodeView::find(revision_,node):BoundNodeView{};
    }
    template<class Fn> void for_each_node(Fn&& fn) const {
        if(backing_) {for(const auto& entry:backing_->entries())fn(lookup(entry.first));}
        else if(revision_) {for(const auto& entry:revision_->directory().entries())fn(BoundNodeView::find(revision_,entry.second.payload->node()));}
    }
    bool contextual() const noexcept { return mode_==MapMode::contextual; }
};
bool structure_valid(const MapVersion*) noexcept;
bool inputs_current(const MapVersion*) noexcept;
// This low-level helper treats null as ABSENT, not an explicitly empty wrapper.
// Callers must preserve override_supplied and reject empty explicit overrides
// when the base is contextual.
bool accepts_override(const MapVersion* base, const MapVersion* override_version) noexcept;
}
