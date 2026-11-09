#pragma once
#include "style_lookup.h"
struct aether_layout_result_s;
namespace aether::layout {
class StyleOperationPin {
    StyleLookup lookup_;
public:
    StyleOperationPin(LayoutAssetsRef assets, MapVersionRef override_version, bool override_supplied)
        : lookup_(std::move(assets), std::move(override_version), override_supplied) {}
    const StyleLookup& lookup() const noexcept { return lookup_; }
    const MapVersionRef& selected_version() const noexcept { return lookup_.selected_version(); }
    const MapVersionRef& base_version() const noexcept { return lookup_.base_version(); }
    bool base_contextual() const noexcept { return base_version() && base_version()->contextual(); }
    std::shared_ptr<node_identity_s> document_lifetime() const noexcept { return lookup_.document_lifetime(); }
    bool structure_valid() const noexcept { return lookup_.structure_valid(); }
    bool inputs_current() const noexcept { return aether::style::inputs_current(base_version().get()); }
    bool accepts_override() const noexcept {
        if (!lookup_.override_supplied() || !base_contextual()) return true;
        return selected_version() && aether::style::accepts_override(base_version().get(), selected_version().get());
    }
};
class LayoutOperationPin {
    aether_layout_result_s* result_;
public:
    explicit LayoutOperationPin(aether_layout_result_s*) noexcept;
    ~LayoutOperationPin();
    LayoutOperationPin(const LayoutOperationPin&) = delete;
    LayoutOperationPin& operator=(const LayoutOperationPin&) = delete;
    aether_layout_result_s* geometry() const noexcept { return result_; }
};
// Capture immediately before external code; validate before touching DOM again.
class CallbackEpochGuard {
    std::shared_ptr<node_identity_s> document_;
    std::shared_ptr<EditableNodeStorage> document_lease_;
    uint64_t structure_ = 0, mutation_ = 0, style_ = 0;
public:
    explicit CallbackEpochGuard(const MapVersionRef& version) noexcept
        : CallbackEpochGuard(version ? version->document_lifetime() : nullptr) {}
    explicit CallbackEpochGuard(std::shared_ptr<node_identity_s> document) noexcept
        : document_(std::move(document)), document_lease_(aether_identity_lease(document_)),
          structure_(document_ ? document_->structure_epoch : 0),
          mutation_(document_ ? document_->mutation_epoch : 0),
          style_(document_ ? document_->style_tree_epoch : 0) {}
    bool valid() const noexcept {
        return !document_ || (document_->alive && document_->structure_epoch == structure_ &&
            document_->mutation_epoch == mutation_ && document_->style_tree_epoch == style_);
    }
};
} // namespace aether::layout
