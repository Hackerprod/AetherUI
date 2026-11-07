#pragma once
#include "../css/style_map_version.h"
#include "../css/frozen_measure_read.h"
#include "../dom/dom_types.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace aether::layout {
using namespace aether::style;

// Only value scalars are mutable. Every dynamic style value remains deeply const.
class BoxStyleSnapshot : public StyleScalars {
    FrozenStyleRef style_;
public:
    BoxStyleSnapshot() = default;
    explicit BoxStyleSnapshot(FrozenStyleRef style) : StyleScalars(style->view().scalars()), style_(std::move(style)) {}
    ComputedStyleReadView view() const noexcept { return style_->view().with_scalars(*this); }
    const StyleScalars& scalars() const noexcept { return *this; }
    StyleScalars& mutable_scalars() noexcept { return *this; }
    const FrozenStyleRef& style_owner() const noexcept { return style_; }
};

class SyntheticEntry {
    const std::shared_ptr<aether_node_s> node_;
    const FrozenStyleRef style_;
    const FrozenMeasuresRef measures_;
public:
    SyntheticEntry(std::shared_ptr<aether_node_s> node, FrozenStyleRef style, FrozenMeasuresRef measures)
        : node_(std::move(node)), style_(std::move(style)), measures_(std::move(measures)) {}
    const aether_node_s* node() const noexcept { return node_.get(); }
    const FrozenStyleRef& style() const noexcept { return style_; }
    const FrozenMeasuresRef& measures() const noexcept { return measures_; }
};
using SyntheticRef = std::shared_ptr<const SyntheticEntry>;
using SyntheticIndex = std::unordered_map<const aether_node_s*, SyntheticRef>;
class MutableLayoutOverlay;
class FrozenSyntheticStore {
    const SyntheticIndex entries_;
    const ImmutableArray<SyntheticRef> ordered_;
    FrozenSyntheticStore(SyntheticIndex entries, ImmutableArray<SyntheticRef> ordered)
        : entries_(std::move(entries)), ordered_(std::move(ordered)) {}
    friend class MutableLayoutOverlay;
public:
    const SyntheticEntry* find(const aether_node_s* node) const noexcept {
        const auto it = entries_.find(node); return it == entries_.end() ? nullptr : it->second.get();
    }
    ReadOnlyArrayView<SyntheticRef> ordered_entries() const noexcept { return ordered_.view(); }
};
using FrozenSyntheticStoreRef = std::shared_ptr<const FrozenSyntheticStore>;
class MutableLayoutOverlay {
    SyntheticIndex entries_;
    std::vector<SyntheticRef> ordered_;
public:
    const SyntheticEntry* find(const aether_node_s* node) const noexcept {
        const auto it = entries_.find(node); return it == entries_.end() ? nullptr : it->second.get();
    }
    void insert(std::shared_ptr<aether_node_s> node, FrozenStyleRef style, FrozenMeasuresRef measures) {
        const auto* key = node.get();
        auto entry = std::make_shared<const SyntheticEntry>(std::move(node), std::move(style), std::move(measures));
        // Reserve before changing the index: after insertion the ordered owner push is noexcept.
        if (ordered_.size() == ordered_.capacity()) {
            const auto maximum = ordered_.max_size();
            const auto capacity = ordered_.capacity();
            if (capacity == maximum) throw std::length_error("synthetic order capacity");
            const auto next = capacity == 0 ? size_t{1} :
                capacity > maximum - capacity ? maximum : capacity * 2;
            ordered_.reserve(next);
        }
        if (!entries_.emplace(key, entry).second) throw std::invalid_argument("duplicate synthetic node");
        ordered_.push_back(std::move(entry));
    }
    FrozenSyntheticStoreRef freeze() const {
        // Copy only immutable owners/index. Builder aliases never reach published slots.
        auto ordered = ImmutableArray<SyntheticRef>::generate(ordered_.size(), [&](size_t i) { return ordered_[i]; });
        return FrozenSyntheticStoreRef(new const FrozenSyntheticStore(entries_, std::move(ordered)));
    }
};
class LayoutAssets {
public:
    const MapVersionRef base;
    const FrozenSyntheticStoreRef synthetic;
    const FrozenStyleRef default_style;
    // The actual geometry document is independent of style-map provenance. A
    // null/empty or unbound legacy map still lays out real DOM nodes.
    const std::shared_ptr<node_identity_s> document_lifetime;
    const uint64_t document_structure_epoch;
    LayoutAssets(MapVersionRef version, FrozenSyntheticStoreRef overlay, FrozenStyleRef defaults,
        std::shared_ptr<node_identity_s> document, uint64_t structure_epoch)
        : base(std::move(version)), synthetic(std::move(overlay)), default_style(std::move(defaults)),
          document_lifetime(std::move(document)), document_structure_epoch(structure_epoch) {}
    LayoutAssets(MapVersionRef version, FrozenSyntheticStoreRef overlay, FrozenStyleRef defaults)
        : LayoutAssets(version, std::move(overlay), std::move(defaults),
            version ? version->document_lifetime() : nullptr, version ? version->structure_epoch() : 0) {}
    bool structure_valid() const noexcept {
        return aether::style::structure_valid(base.get()) &&
            (!document_lifetime || (document_lifetime->alive &&
                document_lifetime->structure_epoch == document_structure_epoch));
    }
};
using LayoutAssetsRef = std::shared_ptr<const LayoutAssets>;
} // namespace aether::layout
