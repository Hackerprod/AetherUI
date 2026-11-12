#pragma once
#include "../css/frozen_c_string.h"
#include "../css/immutable_array.h"
#include "custom_property_scope_internal.h"
#include <aether/aether.h>
#include <array>
#include <string_view>

struct color_resolution_state_s;
namespace aether::style {
class FrozenCustomPropertyScope;
class FrozenColorMetadata;
using FrozenScopeRef = std::shared_ptr<const FrozenCustomPropertyScope>;
using FrozenColorRef = std::shared_ptr<const FrozenColorMetadata>;
using CustomPropertyInput = std::unordered_map<std::string, computed_custom_property_s>;
class FrozenCustomProperty {
    const FrozenCString name_, value_;
    const custom_property_status status_;
public:
    FrozenCustomProperty(std::string_view name, const computed_custom_property_s& value)
        : name_(name), value_(value.value), status_(value.status) {}
    std::string_view name() const noexcept { return name_.view(); }
    std::string_view value() const noexcept { return value_.view(); }
    custom_property_status status() const noexcept { return status_; }
};
struct FrozenScopeControl;
class FrozenCustomPropertyScope {
    using Index = std::unordered_map<std::string_view, size_t>;
    // Reverse destruction destroys the index before its owner-held key bytes.
    const ImmutableArray<FrozenCustomProperty> local_;
    const Index index_;
    const FrozenScopeRef parent_;
    FrozenCustomPropertyScope(ImmutableArray<FrozenCustomProperty>, Index, FrozenScopeRef) noexcept;
    friend struct FrozenScopeControl;
    friend aether_status_t freeze_custom_scope(const CustomPropertyInput&, FrozenScopeRef, FrozenScopeRef&) noexcept;
public:
    FrozenCustomPropertyScope(const FrozenCustomPropertyScope&) = delete;
    FrozenCustomPropertyScope& operator=(const FrozenCustomPropertyScope&) = delete;
    const FrozenCustomProperty* find_local(std::string_view name) const noexcept;
    const FrozenCustomPropertyScope* parent() const noexcept { return parent_.get(); }
    size_t local_count() const noexcept { return local_.size(); }
    ReadOnlyArrayView<FrozenCustomProperty> local_entries() const noexcept { return local_.view(); }
};
aether_status_t freeze_custom_scope(const CustomPropertyInput&, FrozenScopeRef parent, FrozenScopeRef& output) noexcept;
class FrozenColorMetadata {
    const std::array<bool, 4> border_;
    const bool background_;
    const ImmutableArray<uint8_t> text_, box_;
public:
    FrozenColorMetadata(std::array<bool,4> border, ImmutableArray<uint8_t> text, ImmutableArray<uint8_t> box, bool background) noexcept
        : border_(border), background_(background), text_(std::move(text)), box_(std::move(box)) {}
    bool border(size_t i) const { return border_.at(i); }
    bool background() const noexcept { return background_; }
    size_t text_shadow_count() const noexcept { return text_.size(); }
    bool text_shadow_at(size_t i) const { return text_.view().at(i) != 0; }
    size_t box_shadow_count() const noexcept { return box_.size(); }
    bool box_shadow_at(size_t i) const { return box_.view().at(i) != 0; }
};
aether_status_t freeze_color_metadata(const color_resolution_state_s&, FrozenColorRef&) noexcept;
// Borrowed readers: callers retain the selected builder/final owner.
class ColorMetadataReadView {
    const color_resolution_state_s* builder_ = nullptr;
    const FrozenColorMetadata* frozen_ = nullptr;
public:
    explicit ColorMetadataReadView(const color_resolution_state_s& value) noexcept : builder_(&value) {}
    explicit ColorMetadataReadView(const FrozenColorMetadata& value) noexcept : frozen_(&value) {}
    bool border(size_t i) const;
    bool background() const noexcept;
    size_t text_shadow_count() const noexcept;
    bool text_shadow_at(size_t i) const;
    size_t box_shadow_count() const noexcept;
    bool box_shadow_at(size_t i) const;
};
}

namespace aether::style {
FrozenScopeRef compute_frozen_custom_property_scope(const custom_property_sources_s&, FrozenScopeRef, int32_t, int32_t, css_variable_budget* = nullptr);
bool resolve_frozen_computed_value(const char*, const FrozenCustomPropertyScope*, int32_t, int32_t, std::string&, css_variable_budget* = nullptr);
computed_custom_property_s resolve_frozen_custom_property_root_with_budget(const std::string&, const custom_property_sources_s&, const FrozenCustomPropertyScope*, int32_t, int32_t, css_variable_budget&);
}
