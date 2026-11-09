#pragma once
#include "layout_assets.h"
#include <optional>
struct aether_layout_box_s;
namespace aether::layout {
enum class BoxStylePolicy { snapshot, effective_map };
struct Presentation { aether_bool_t visible; int32_t pointer_events; };
class StyleLookup {
    LayoutAssetsRef assets_;
    MapVersionRef base_, selected_;
    const MutableLayoutOverlay* building_ = nullptr;
    FrozenStyleRef defaults_;
    bool override_supplied_ = false;
    const SyntheticEntry* synthetic(const aether_node_s* node) const noexcept {
        if (override_supplied_) return nullptr;
        if (building_) return building_->find(node);
        return assets_ && assets_->synthetic ? assets_->synthetic->find(node) : nullptr;
    }
public:
    StyleLookup(MapVersionRef base, const MutableLayoutOverlay* building, FrozenStyleRef defaults)
        : base_(std::move(base)), selected_(base_), building_(building), defaults_(std::move(defaults)) {}
    StyleLookup(LayoutAssetsRef assets, MapVersionRef override_version, bool override_supplied)
        : assets_(std::move(assets)), base_(assets_ ? assets_->base : MapVersionRef{}),
          selected_(override_supplied ? std::move(override_version) : base_),
          defaults_(assets_ ? assets_->default_style : FrozenStyleRef{}), override_supplied_(override_supplied) {}
    const MapVersionRef& selected_version() const noexcept { return selected_; }
    const MapVersionRef& base_version() const noexcept { return base_; }
    std::shared_ptr<node_identity_s> document_lifetime() const noexcept {
        return assets_ ? assets_->document_lifetime : base_ ? base_->document_lifetime() : nullptr;
    }
    bool structure_valid() const noexcept {
        return assets_ ? assets_->structure_valid() : aether::style::structure_valid(base_.get());
    }
    bool override_supplied() const noexcept { return override_supplied_; }
    BoundNodeView lookup_record(const aether_node_s* node) const noexcept {
        if (synthetic(node)) return {};
        return selected_ ? selected_->lookup(node) : BoundNodeView{};
    }
    BoundNodeView lookup_record_owner(const aether_node_s* node) const noexcept {
        return lookup_record(node);
    }
    FrozenStyleRef lookup_node_owner(const aether_node_s* node) const noexcept {
        if (const auto* entry = synthetic(node)) return entry->style();
        const auto record = lookup_record(node);
        return record ? record.payload_owner()->style() : FrozenStyleRef{};
    }
    std::optional<ComputedStyleReadView> lookup_node(const aether_node_s* node) const noexcept {
        if (const auto* entry = synthetic(node)) return entry->style()->view();
        const auto record = lookup_record(node);
        return record ? std::optional<ComputedStyleReadView>(record.style_view()) : std::nullopt;
    }
    struct MeasureBinding {
        FrozenMeasurePayloadRef payload;
        FrozenDocumentRef document;
        explicit operator bool() const noexcept { return payload && document; }
    };
    // Pins both owners without allocating a per-read FrozenMeasures wrapper.
    // Synthetic layout construction may deliberately copy this local data.
    MeasureBinding lookup_measure_binding(const aether_node_s* node) const noexcept {
        if (const auto* entry = synthetic(node)) {
            const auto& measures = entry->measures();
            return measures ? MeasureBinding{measures->payload_owner(), measures->document_owner()} : MeasureBinding{};
        }
        const auto record = lookup_record(node);
        return record && record.has_measures()
            ? MeasureBinding{record.payload_owner()->measures(), record.document_owner()} : MeasureBinding{};
    }
    // Compatibility reader for already-bound legacy/synthetic owners only.
    // Contextual consumers use lookup_measures or lookup_measure_binding.
    FrozenMeasuresRef lookup_measures_owner(const aether_node_s* node) const noexcept {
        if (const auto* entry = synthetic(node)) return entry->measures();
        const auto record = lookup_record(node);
        return record && record.legacy_record() ? record.legacy_record()->measures() : FrozenMeasuresRef{};
    }
    std::optional<MeasuresReadView> lookup_measures(const aether_node_s* node) const noexcept {
        if (const auto* entry = synthetic(node)) {
            const auto& measures = entry->measures();
            return measures ? std::optional<MeasuresReadView>(MeasuresReadView(*measures)) : std::nullopt;
        }
        const auto record = lookup_record(node);
        // selected_ (or the synthetic owner above) keeps this borrowed view alive.
        return record && record.has_measures()
            ? std::optional<MeasuresReadView>(record.measures_view()) : std::nullopt;
    }
    const FrozenStyleRef& default_owner() const noexcept { return defaults_; }
    ComputedStyleReadView default_style() const noexcept { return defaults_->view(); }
    ComputedStyleReadView lookup_box(const aether_layout_box_s&, BoxStylePolicy) const noexcept;
    Presentation presentation(const aether_layout_box_s&, BoxStylePolicy) const noexcept;
};
} // namespace aether::layout
